# HFS 唯讀檔案系統（kernel/fs/hfs、kernel/fs/apm）

> 這份文件是自動開發時寫的（使用者沒有逐步參與），尚未審核。欄位偏移是憑記憶寫的
> Inside Macintosh: Files 第 2 章（Data Organization on Volumes）與 Devices 的 SCSI Manager 章
> （Apple Partition Map），沒有原文可對照；已用 hfsutils 產生的磁碟映像驗證過，但請審核時留意。

## 1. 要解決的問題

Classic Mac OS 的磁碟是 **HFS** 格式（不是 HFS+，那是 Mac OS 8.1 之後）。要開機到 Finder，
kernel 先要能**讀**磁碟上的檔案（System 檔、字型、應用程式）。這份文件說明磁碟怎麼組織、
我們怎麼讀。目前只讀不寫。

比喻：磁碟像一座圖書館。

- **分割區表**（Apple Partition Map）是大樓導覽：哪幾層是書庫、哪幾層是機房。
- **MDB**（Master Directory Block）是書庫的總目錄卡：有多少書架、每個書架多大、目錄本身放在哪裡。
- **Allocation block**（配置區塊）是書架上的一格，檔案以格為單位佔用。
- **Catalog**（目錄 B*-tree）是索引卡櫃：用「資料夾編號 + 檔名」查到檔案的資料。
- **Extents**（區段）：檔案不一定放在連續的格子裡，用「從第幾格開始、連續幾格」的清單記錄。

## 2. 磁碟佈局

```
區塊 0 ─ 1        開機區塊（boot blocks，我們不讀）
區塊 2            MDB（簽名 'BD' = 0x4244）
區塊 3 ─ ...      volume bitmap（哪些 allocation block 已使用，唯讀不需要）
alloc_start ─ ... allocation blocks（資料、catalog、extents tree 都放在這裡）
```

單位有兩種，不要混淆：

| 名稱 | 大小 | 例 |
|---|---|---|
| 邏輯區塊 | 固定 512 位元組 | MDB 在邏輯區塊 2 |
| allocation block | MDB 的 `drAlBlkSiz`，512 的倍數 | 800 KiB 的測試磁碟是 512，大磁碟更大 |

allocation block n 的位元組位置 = `drAlBlSt × 512 + n × drAlBlkSiz`（從 volume 開頭算起）。

### MDB 的欄位（位元組偏移，全部 big-endian）

| 偏移 | 欄位 | 用途 |
|---|---|---|
| 0 | drSigWord | 簽名 0x4244 |
| 18 | drNmAlBlks | allocation block 總數 |
| 20 | drAlBlkSiz | 每個 allocation block 的位元組數 |
| 28 | drAlBlSt | 第一個 allocation block 的邏輯區塊號 |
| 34 | drFreeBks | 空的 allocation block 數 |
| 36 | drVN | 磁碟名稱（Pascal 字串：長度位元組 + 最多 27 字元） |
| 84 / 88 | drFilCnt / drDirCnt | 檔案數 / 資料夾數 |
| 130 | drXTFlSize | extents overflow tree 檔案長度 |
| 134 | drXTExtRec | 上述檔案的前 3 個 extent（每個 4 位元組：起始、數量） |
| 146 | drCTFlSize | catalog 檔案長度 |
| 150 | drCTExtRec | catalog 檔案的前 3 個 extent |

`hfs_mount` 讀 MDB，檢查簽名、allocation block 大小、整個 volume 是否放得進裝置，再讀兩棵樹的
header node。

## 3. B*-tree

catalog 與 extents overflow 都是 **B\*-tree**，存成一般的「檔案」（所以也有 extent 清單）。
樹由固定 512 位元組的 **node** 組成：

```
node（512 位元組）
┌──────────────────────────────┐ 0
│ 前向連結 (4) 後向連結 (4)      │  同一層 node 的雙向鏈
│ 類型 (1) 高度 (1) 記錄數 (2)  │  類型：-1 葉、0 索引、1 header、2 map
│ 保留 (2)                      │ 14
├──────────────────────────────┤
│ 記錄 0                        │
│ 記錄 1                        │
│ ...        (空白)             │
├──────────────────────────────┤
│ ... offset 1  offset 0  (2 位元組 × (記錄數 + 1)，從 node 尾端往前放)
└──────────────────────────────┘ 512
```

每筆記錄 = **key**（第一位元組是 key 長度，不含自己）+ 補成偶數位址的**資料**。
node 0 是 header node，裡面有「第一個葉 node」的編號。所有資料記錄都在**葉**，
葉依 key 排序，並用前向連結串起來。

## 4. Catalog：用名字找檔案

key = `{長度, 保留, 父資料夾 ID (4), 名稱 (Pascal 字串, 最多 31)}`。每個檔案或資料夾一筆，
另有「thread」記錄用來從 ID 反查名字（我們不用）。資料記錄的第一位元組是類型：

| 類型 | 意義 | 我們讀的欄位 |
|---|---|---|
| 1 | 資料夾 | valence（項目數，偏移 4）、資料夾 ID（偏移 6） |
| 2 | 檔案 | 類型/創作者（4、8）、檔案編號（20）、資料 fork 邏輯長度（26）、resource fork 邏輯長度（36）、資料 fork 前 3 個 extent（74）、resource fork 前 3 個 extent（86） |
| 3、4 | thread | 忽略 |

根資料夾的 ID 固定是 2（父是 1）。路徑解析：從 ID 2 開始，每個元件用 `(目前資料夾 ID, 名稱)` 查一次。

### 設計決定

| 決定 | 選擇 | 原因 / 代價 |
|---|---|---|
| 查詢方式 | 沿著葉 node 的鏈**逐筆比對**，不從根往下走樹 | B*-tree 的排序依 Mac 的字元比較表（含重音字母的順序）；我沒有那張表。逐筆比對不依賴順序，不會因為排序規則不同而找錯。代價：O(n)，對測試磁碟（約 1000 筆）無感，大磁碟會慢 |
| 名稱比較 | ASCII 字母不分大小寫，其他位元組（Mac Roman 0x80 以上）完全相同才算相同 | 真正的 HFS 對重音字母也不分大小寫（`É` = `é`）；這裡會區分。之後有了正確的比較表再改 `hfs_names_equal` |
| 路徑 | 以 `:` 分隔，相對於 volume 根；開頭的 `:` 與空元件忽略；不支援 `::`（上一層）也不支援以磁碟名稱開頭 | 磁碟名稱可能與資料夾同名，有歧義；先不做 |
| 列舉順序 | catalog 順序（依 key，ASCII 名稱大致是字母序） | 不另外排序 |
| 檔名編碼 | 原樣保留 Mac Roman 位元組 | 轉換留給顯示端（shell 把 0x80 以上顯示成 `?`） |

## 5. Extents：檔案放在哪裡

一個 fork（data 或 resource）的內容 = 一串 extent。catalog 記錄只放前 **3** 個。
更多的放在 **extents overflow B\*-tree**：

- key = `{長度=7, fork 類型 (0x00 data / 0xFF resource), 檔案編號 (4), 這組第一個區塊在 fork 內的編號 (2)}`
- 資料 = 3 個 extent

讀 fork 的第 n 個 allocation block：先看 catalog 的 3 個 extent；超過就用
「已涵蓋的區塊數」當作起始編號去查 overflow tree，取得下一組 3 個，直到涵蓋 n。

**特例**：catalog 檔案本身（保留檔案編號 4）超過 3 個 extent 時，其餘也放在 overflow tree。
extents 檔案本身（編號 3）只有 MDB 裡的 3 個，不能再擴充（否則要靠自己找自己）。
我一開始漏了這點，測試磁碟的 catalog 有 36 個以上的 allocation block，讀到後半段就錯了。

## 6. Apple Partition Map（kernel/fs/apm）

整顆 Mac 磁碟（不是單獨的 HFS volume）前面有分割區表：

```
區塊 0     Driver Descriptor Map，簽名 'ER'（我們不解析，也不要求存在）
區塊 1..N  每個分割區一筆，簽名 'PM'，每筆 512 位元組
```

每筆（big-endian）：簽名 'PM'（0）、`pmMapBlkCnt` 表總筆數（4）、起始區塊（8）、區塊數（12）、
名稱（16，32 位元組）、類型（48，32 位元組，例如 `Apple_HFS`）、狀態（88）。
`apm_find(dev, "Apple_HFS", &p)` 找到第一個 HFS 分割區，`hfs_mount(vol, dev, p.start, p.block_count)` 掛載。
只支援 512 位元組區塊的裝置；每筆的 `pmMapBlkCnt` 必須一致，否則視為損壞。

## 7. 介面

```c
hfs_mount(&vol, dev, start, block_count)      // 掛載
hfs_lookup(&vol, "Docs:Note", &entry)         // 路徑 → 檔案或資料夾
hfs_list(&vol, folder_id, fn, ctx)            // 列舉資料夾
hfs_read(&vol, &entry, HFS_FORK_DATA, off, buf, len)  // 讀 fork；也可讀 HFS_FORK_RESOURCE
```

錯誤一律回傳 -1；讀取超過 fork 長度會被截斷。樹的走訪有「node 數上限」，被竄改成迴圈的磁碟會回傳錯誤。

## 8. 測試

- `tests/host/test_apm.c`：用 `tools/make_apm_image.py` 合成的映像（`build/images/apm.img`）。
- `tests/host/test_hfs.c`：用 `tools/make_hfs_image.sh` 以 hfsutils 建的磁碟（`build/images/hfs.img`，
  800 KiB，是 `hformat` 允許的最小值）。內容：多層資料夾、大小寫混合的檔名、空檔案、
  有 resource fork 的檔案（用 python 寫的 MacBinary 檔經 `hcopy -m` 放入）、100000 位元組的檔案、
  以及被塞進 30 個一格的空洞而需要 extents overflow 的 `Frag`（先把磁碟填滿再刪掉一半的小檔案）。
  還有被竄改的 catalog 連結（確認不會無窮迴圈）。
- **hfsutils 會把「目前的磁碟」記在 `~/.hcwd`**。腳本在暫時的 `HOME` 底下執行，不會動到你的 `~/.hcwd`。
  建置時需要 `hfsutils` 套件（`apt install hfsutils`；CI 也有裝）。

## 9. 已知限制

- 只讀。不檢查 volume bitmap，不處理壞掉的 volume 的修復。
- 名稱比較見第 4 節；查詢是線性的。
- 不支援 HFS+、嵌入式 HFS+（HFS Wrapper）、MFS（400K 的舊磁碟）。
- allocation block 數量在 MDB 是 16 位元，所以 volume 最多 65535 個 allocation block。
- 不解析 DDM 與 volume bitmap；不處理 `drAtrb` 的旗標（例如沒有正常卸載）。
