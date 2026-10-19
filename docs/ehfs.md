# eHFS：68k_HAL 的檔案系統

> **狀態（P11 K4，進行中）**：核心已實作（`kernel/fs/ehfs/`）：名稱的比較形式（NFC + 大小寫折疊）、metadata 日誌、bitmap、mkfs、fsck、B-tree、catalog、檔案內容（fork、extent）、延伸屬性；Linux 工具 `mkfs.ehfs`、`fsck.ehfs`、`ehfs-cp`（`make ehfs-tools`）；kernel 掛在檔案樹的 `/ehfs`（11.5 節）。Mac 端（`docs/fsm.md`）接著做。使用者 2026-10-19 決定了第 9 節的 E1–E9，並同意 P11 自動實作、事後審核。第 3–5 節是設計；實作的細節與和設計不同的地方在第 11 節。

## 1. 要解決什麼

Mac 現在用的 HFS（1985 年的設計）有很多限制：

| HFS 的限制 | 原因 | eHFS |
|---|---|---|
| 一個卷宗最多 65535 個配置區塊 → 2 GB 的卷宗配置區塊要 32 KB，小檔案很浪費 | 區塊編號只有 16 位元 | 64 位元區塊編號，固定 4 KiB 區塊 |
| 檔案最大 2 GB | 32 位元長度 | 64 位元長度 |
| 檔名 31 個 Mac Roman 字元 | 1 位元組長度、單位元組字元集 | UTF-8，最多 255 位元組 |
| 時間只到秒、1904–2040 年、當地時間 | 32 位元秒數 | 64 位元奈秒、UTC |
| 斷電可能讓目錄（catalog）壞掉 | 沒有日誌 | metadata 日誌 + CRC32C |
| 檔案 ID 32 位元 | — | 64 位元 CNID（Mac 端看到的是 32 位元，第 7 節） |

比喻：HFS 是一本紙本目錄卡片櫃，抽屜數量、卡片欄位都是 1985 年訂的。eHFS 保留「卡片櫃」的組織方式（B-tree 目錄、每個檔案有固定的號碼、兩個 fork、Finder 資訊），但把每一欄放大，並加上「改卡片前先寫在草稿本上」（日誌）與「每張卡片有驗算碼」（CRC32C）。

**不是 HFS+ 的複製**：HFS+ 有很多為了相容與歷史包袱的設計（UTF-16、Unicode 2.0 的大小寫表、512 位元組的節點、扁平的時間戳）。eHFS 只借概念。

## 2. 已經決定的事（使用者 2026-10-18 同意「依建議」）

- B-tree 目錄，固定的檔案 ID（CNID；Mac 的別名 alias 靠它找檔案）。
- 空間管理：bitmap（一個位元一個區塊）＋ extent（起點, 長度）；4 KiB 區塊。
- 所有數字 64 位元。
- 檔名 UTF-8，最多 255 位元組，**不分大小寫**；Mac 端看到轉換成 Mac Roman、最多 31 字元的名稱。
- data fork、resource fork、type/creator、Finder 資訊是內建欄位；另外有通用的延伸屬性（extended attributes）。
- metadata 日誌 + metadata 的 CRC32C。
- 64 位元奈秒 UTC 時間戳。
- 不做快照（系統碟的疊加層已經涵蓋「回到之前」的需求）。
- Linux 端的 mkfs／fsck／複製工具，與 kernel 用同一份程式碼。
- Mac 透過 System 7.5 File System Manager 的外部檔案系統外掛存取（研究筆記：`docs/fsm.md`）。

## 3. 卷宗配置

```
區塊 0（4 KiB）
  0–1023      保留（開機碼的位置，像 HFS 的 boot blocks；eHFS 不用）
  1024–4095   主 superblock（3 KiB）
區塊 1 ..      其餘的一切都是「檔案」：bitmap、目錄 B-tree、extent B-tree、屬性 B-tree、日誌
最後一個區塊   備份 superblock（mkfs 與 fsck 寫；平常不更新位置以外的欄位）
```

所有 metadata 結構本身也是有 CNID 的特殊檔案（像 HFS+），所以它們可以長大、可以放在任何地方，不需要固定位置。

### 3.1 superblock（小端序；以 HFS 的 MDB 為藍本，欄位全部放大）

| 偏移 | 大小 | 欄位 |
|---|---|---|
| 0 | 8 | magic `"eHFS\0\r\n\x1a"`（含換行與 EOF 字元，被文字工具改壞時看得出來） |
| 8 | 4 | 版本（1） |
| 12 | 4 | 區塊大小（4096；之後若允許其他大小，見第 9 節） |
| 16 | 8 | 區塊總數 |
| 24 | 8 | 空閒區塊數 |
| 32 | 8 | 下一個 CNID |
| 40 | 8 | 檔案數、48：資料夾數 |
| 56 | 4+4+4 | feature 旗標：compat、ro_compat、incompat（不認得 incompat 的位元就不掛載；不認得 ro_compat 就唯讀） |
| 68 | 4 | 狀態：clean／dirty（掛載中）／errors |
| 72 | 16 | 卷宗 UUID |
| 88 | 8×4 | 建立、修改、最後檢查、最後掛載的時間（奈秒 UTC） |
| 120 | 6 × 144 | bitmap、catalog、extents、attributes、journal、bad blocks 檔各自的 fork 描述（長度與前 8 個 extent，見 4.3） |
| 984 | 256 | 卷宗名稱（UTF-8，第一個位元組是長度） |
| 1240 | 1828 | 保留（0） |
| 3068 | 4 | CRC32C（前 3068 位元組） |

## 4. 檔案與目錄

### 4.1 CNID

64 位元，從 16 開始依序配置、不重複使用。保留：1 = 根目錄的父親、2 = 根目錄、3 = extents 樹、4 = catalog 樹、5 = bitmap、6 = attributes 樹、7 = 日誌、8 = 壞區塊、9–15 保留。

### 4.2 catalog B-tree

- **鍵**：（父資料夾的 CNID, 名稱）。名稱用「比較用的形式」排序：Unicode 正規化（第 9 節決定 NFC 或 NFD）後做 simple case folding，再按位元組比較。原本的大小寫保存在記錄裡。
- **記錄**：
  - 資料夾記錄：CNID、旗標、時間戳（建立、內容修改、屬性修改、存取、備份）、子項目數、Finder 的 DInfo/DXInfo（32 位元組）。
  - 檔案記錄：CNID、旗標（鎖定等）、時間戳、Finder 的 FInfo/FXInfo（32 位元組，type/creator 在其中）、data fork 與 resource fork 各一個 fork 描述。
  - thread 記錄：鍵是（CNID, 空名稱），內容是（父 CNID, 名稱）——用 CNID 找回檔案的位置（別名、Mac 的 `PBResolveFileIDRef`）。
- 節點：16 KiB（4 個區塊；第 9 節），節點標頭含種類（index／leaf）、層級、記錄數、前後兄弟節點編號（64 位元）、CRC32C。

### 4.3 fork 描述與 extent

```
fork 描述（144 位元組）
  邏輯長度（8）       位元組
  實際區塊數（8）
  extent[8]          每個 =（起始區塊 8, 區塊數 8）
```

超過 8 段的檔案，其餘的 extent 放在 extents B-tree，鍵 =（CNID, fork 種類, 該段在檔案中的起始區塊）。實作：鍵 17 位元組（大端序 CNID 8、fork 1（0 = 資料、1 = 資源）、起始區塊 8），值 = 8 段（和 fork 描述裡的一樣，沒用到的是 0）。

### 4.4 延伸屬性（attributes B-tree）

鍵 =（CNID, 屬性名稱 UTF-8）；值小的（≤ 3 KiB）直接放在記錄裡，大的用 fork 描述（實作：只有直接放的，值 = u16 種類（1）、u32 長度、內容；超過 3 KiB 的拒絕。名稱分大小寫，不正規化）。用途：Linux 的 xattr、之後的其他 metadata。Finder 資訊與 type/creator **不**放這裡（內建欄位，Mac 每次都要用）。

## 5. 日誌（只記 metadata）

- 物理區塊日誌（像 ext4 的 jbd2、HFS+ 的 journal）：一個交易 = 描述區塊（要改哪些區塊）＋ 那些 metadata 區塊的新內容 ＋ 提交區塊（含 CRC32C）。提交後再寫到真正的位置（checkpoint）。掛載時若有提交完整、還沒 checkpoint 的交易就重做；不完整的丟掉。
- **ordered 模式**：新配置給檔案的資料區塊先寫，metadata 交易後提交——斷電後不會看到別人的舊資料。
- 資料本身不寫日誌、不算 checksum（效能；第 9 節）。
- 日誌是 CNID 7 的檔案，mkfs 時配置（實作：預設是卷宗的 1/256，260–16384 個區塊，也就是 1–64 MiB）。

## 6. 名稱：UTF-8 與 Mac 端的 31 字元

- 磁碟上只存 UTF-8 原名。
- Mac 端（FSM 外掛）顯示轉換後的名稱：UTF-8 → Mac Roman（不能表示的字元換成 `_` 或近似字），超過 31 字元就截斷並加上 `#` 與 CNID 的十六進位（例：`A very long file name th#1A2B`），保證同一資料夾裡不重複。Mac 端用轉換後的名稱開檔時，外掛反查：先找完全相符的原名，再解 `#CNID`。
- Mac 端建立或改名的檔案：Mac Roman → UTF-8 存回去。

## 7. 與 Mac 的介面（摘要；細節在 `docs/fsm.md`）

- System 7.5 的 File System Manager（FSM）讓外部的檔案系統以 HFS 呼叫（`PBGetCatInfo`、`PBHOpen`……）的形式出現在 Finder 裡。
- Mac 的 dirID／fileID 是 32 位元：外掛直接用 CNID 的低 32 位元；CNID 依序配置，40 億個以內不會衝突（超過時的處理見第 9 節）。
- 卷宗的資料在 HAL 端讀寫（kernel 裡的 eHFS 程式碼），Mac 端的外掛只是轉接：一個模擬 opcode 把參數區塊交給 HAL（和 `.Disk` 驅動相同的手法，`docs/mac.md` 第 6 節）。

## 8. 工具與程式碼的組織

```
kernel/fs/ehfs/      freestanding 的核心（B-tree、bitmap、日誌、CRC32C）：kernel 與工具共用
tools/ehfs/          Linux 端：mkfs.ehfs、fsck.ehfs、ehfs-cp（複製進出，fork 與 Finder 資訊用 xattr，E7）
emu/mac/fsm_*        Mac 端的 FSM 外掛（68k 碼）與它的模擬 opcode
```

host 單元測試照現在的做法（`tests/host/`）；fsck 是測試的主力（每個寫入測試後跑一次 fsck）。

## 9. 決定（使用者 2026-10-19「全部採用建議」）

| # | 問題 | 決定 |
|---|---|---|
| E1 | Unicode 正規化 | NFC |
| E2 | 大小寫比較的表 | 固定 Unicode 15.0，版本寫在 superblock（偏移 1240） |
| E3 | B-tree 節點大小 | 16 KiB |
| E4 | 資料的 checksum | 不做（只有 metadata 有 CRC32C） |
| E5 | CNID 超過 2^32 | 拒絕建立新項目；fsck 回報 |
| E6 | 區塊大小 | 只有 4 KiB（superblock 有欄位） |
| E7 | Linux 端的 fork 格式 | xattr（`com.apple.ResourceFork`、`com.apple.FinderInfo`） |
| E8 | 疊加層變成 eHFS 檔案時 | 預先配置、只覆寫（之後的事） |
| E9 | 名字 | 暫定 eHFS |

## 10. 為什麼這樣設計（參考）

- HFS：Inside Macintosh: Files（MDB、catalog B-tree、thread 記錄、extents overflow）。
- HFS+：Apple Technical Note TN1150（fork 描述內含 8 個 extent、特殊檔案也有 CNID、日誌）。只參考結構，不複製程式碼。
- ext4／jbd2：日誌的交易格式（描述區塊、提交區塊）、feature 旗標的 compat／ro_compat／incompat 三分法。只參考概念（Linux 的程式碼是 GPL-2.0-only，不能引入）。
- exFAT：bitmap 配置與簡單的 metadata checksum。

## 11. 實作（P11 K4）

### 11.1 程式

| 檔案 | 內容 |
|---|---|
| `format.h` | 磁碟上的格式：所有偏移與常數 |
| `name.c` | 名稱：UTF-8 檢查（1–255 位元組，不含 NUL 與 `/`）；比較形式 = NFC（UAX #15：標準分解、標準排序、標準組合，含韓文音節的演算法）再做 simple case folding（CaseFolding.txt 的 C 與 S）。表格 `unicode_data.h` 由 `tools/gen_ehfs_unicode.py` 從 Unicode 15.0 的資料檔產生（檢查 SHA-256；保留 Unicode License V3）。官方 NormalizationTest.txt 的 95,360 個 NFC 案例全部通過（手動驗證；`make test-ehfs-unicode` 檢查表格與產生器一致） |
| `io.c` | 4 KiB 區塊的讀寫、交易、日誌、superblock、mount（重做日誌）、unmount |
| `bitmap.c` | 配置與釋放（從提示的位置往後找第一段空的，再盡量延長；繞回開頭） |
| `mkfs.c` | 建立空的卷宗與根資料夾 |
| `btree.c` | B+-tree |
| `catalog.c` | 目錄：記錄、thread、建立、刪除、搬移與改名、列出 |
| `fork.c` | fork 的區塊：檔案區塊 → 裝置區塊、加長（盡量接在最後一段後面）、縮短（連同用不到的 extents 記錄） |
| `file.c` | 給 kernel、Mac 端、Linux 工具用的操作：建立、刪除（連同 fork 的區塊與屬性）、搬移、資訊（旗標、時間、Finder 資訊）、讀、寫、設定長度、延伸屬性 |
| `check.c` | fsck（只檢查，不修復） |

`kernel/lib/k_crc32c.c`：CRC-32C（Castagnoli，RFC 3720 附錄 B）。

### 11.2 和設計草稿不同或補上的細節

- **位元組順序**：磁碟上的結構是小端序；**B-tree 的鍵**裡的整數用**大端序**，這樣三棵樹都直接用位元組比較（短的在前），B-tree 的程式只有一份。
- **superblock**：保留區的偏移 1240 = Unicode 版本（`0x0F0000`），1244 = 節點大小（16384）。副本在最後一個區塊的位元組 1024。
- **B-tree 節點**：標頭 32 位元組（CRC32C、種類、高度、記錄數、前後節點、空閒的起點）；記錄從前往後放（u16 鍵長、鍵、u16 值長、值），偏移表從節點尾端往前放。節點 0 是標頭節點（根、深度、節點數、空閒數、記錄數、第一與最後的 leaf、**節點的使用 bitmap**：16 KiB 的標頭最多管 130,304 個節點 = 2 GiB 的樹）。同一層的節點（包括索引節點）都有前後的連結，fsck 一層一層檢查。
- **分隔鍵**：子節點的分隔鍵是它被分出時的第一個鍵；新根的最左邊子節點用空鍵；刪掉索引節點的第 0 筆時，新的第 0 筆沿用舊的鍵——所以每個索引節點的第一個鍵永遠是它整棵子樹的下界。不合併節點，空了才釋放；根只剩一個子節點就降一層。
- **樹的成長**：樹的檔案用完時加倍，新的區塊接在 superblock 的 fork 描述後面（最多 8 段）。
- **catalog**：鍵 = 父 CNID（大端序 8 位元組）＋ 名稱的比較形式。thread 的鍵只有 CNID 的 8 位元組，所以排在那個資料夾的子項目之前。記錄裡存原本的名稱（UTF-8）。根資料夾（CNID 2）的父親是 1，名稱是卷宗名稱。superblock 的資料夾數不算根資料夾。
- **日誌**：一次只有一個交易：描述區塊（要改的區塊號碼，最多 256 個）、新內容、提交區塊（CRC32C 涵蓋描述與內容）。提交後馬上寫回原位，再更新日誌的標頭。mount 時若有完整、比標頭新的交易就重做；唯讀 mount 不寫，交易的區塊只在記憶體裡讀取。
- **檔案的寫入**：ordered——資料先寫到區塊，再提交把區塊交給檔案（並改長度）的交易。fork 新得到的區塊沒寫到的部分補 0，看不到舊資料；寫在檔案結尾之後時，中間的空隙也補 0。大的寫入、縮短與刪除切成每個交易最多 256 個區塊（1 MiB）；在兩個交易之間斷電，結果是一致的、做了一部分的狀態（例：檔案只寫了前 1 MiB）。
- **時間**：1970 年起的奈秒（UTC），每個項目 5 個：建立、修改、屬性修改、存取、備份。
- **fsck**：superblock 與副本、特殊檔案、每棵樹的結構（排序、範圍、鏈結、計數、節點 bitmap）、catalog（記錄與 thread 互相對應、鍵與名稱一致、子項目數、檔案與資料夾數、CNID 範圍與 E5）、fork（每段在卷宗內、加起來等於區塊數、長度不超過區塊數、extents 記錄的鍵與內容相符、沒有孤兒記錄）、屬性（擁有者存在、格式）、bitmap 與所有擁有者（逐一 bitmap 區塊比對，記憶體固定，大卷宗較慢）。

### 11.3 測試（`make test` 的 host 測試）

- `test_ehfs`：CRC-32C 的標準值、名稱（大小寫、NFC、韓文、希臘文、ß 不折成 ss、不合法的 UTF-8）、mkfs／mount／唯讀、配置與釋放、在提交過程中的每一個寫入點斷電（重新 mount 後要嘛全有、要嘛全無；唯讀 mount 也看得到已提交的交易）、損壞的 superblock、不認得的 incompat 旗標、bitmap 與標頭節點被改壞時 fsck 抓得到。
- `test_ehfs_btree`：4000 個鍵的 12,000 次隨機插入、刪除、替換（值從幾個位元組到 3 KiB），每 1500 次用 fsck 與參考模型比對；樹的檔案成長；全部刪光；遞增與遞減的順序；左邊界；太大的記錄。
- `test_ehfs_catalog`：根資料夾、巢狀的資料夾與檔案、不分大小寫與 NFC 的查詢、用 CNID 找、列出的順序、改名、搬移（不能搬進自己）、刪除（不能刪有東西的資料夾）、2000 個檔案、建立的交易在每一個寫入點斷電。
- `test_ehfs_file`：讀寫（跨區塊、空隙補 0、超過 8 段後進 extents 樹、幾十段的碎片檔案）、縮短與加長、資源 fork、刪除後區塊歸還、Finder 資訊與時間、延伸屬性（建立、替換、列出、刪除、3 KiB 上限）、加長檔案的寫入在每一個寫入點斷電（舊的部分不變，長度是舊的或新的）。每個測試之後跑 fsck。
- `make test-ehfs-tools`（`tools/ehfs_tools_check.py`）：在映像檔上用 Linux 工具：mkfs、複製一個資料夾樹進去（UTF-8 名稱、3 MB 的檔案、資源 fork、Finder 資訊、其他 xattr）、列出、fsck、複製出來比對內容與 xattr、刪除、建立資料夾、在分割區偏移上操作（之前的位元組不變）、改壞 catalog 節點後 fsck 回報、拒絕不是一般檔案的目標。

### 11.4 Linux 工具（`make ehfs-tools` → `build/host/ehfs/`）

| 工具 | 用法 |
|---|---|
| `mkfs.ehfs` | `mkfs.ehfs [-L 名稱] [--offset 磁區] [--sectors N] [--journal 區塊] [--device] 目標` |
| `fsck.ehfs` | `fsck.ehfs [--offset 磁區] [--device] 目標`：只檢查；結束碼 0 = 乾淨、1 = 有問題、2 = 讀不了 |
| `ehfs-cp` | `ehfs-cp [--offset 磁區] [--device] 目標 in 本機路徑 eHFS路徑`、`out eHFS路徑 本機路徑`、`ls [eHFS路徑]`、`mkdir eHFS路徑`、`rm eHFS路徑` |

- 目標預設只能是映像檔；不是一般檔案（`/dev/sdX`）時要加 `--device` 才會動手——先在映像檔上用。`--offset` 是分割區的第一個磁區（512 位元組）。
- Mac 的部分在 Linux 端是 xattr（E7）：`user.com.apple.ResourceFork` = 資源 fork，`user.com.apple.FinderInfo` = 32 位元組的 Finder 資訊（type、creator、旗標……）。Linux 的 xattr 要有命名空間，所以加上 `user.`。其他 xattr 以同樣的名字存成 eHFS 的屬性（3 KiB 以內）。
- 和 kernel 共用 `kernel/fs/ehfs/` 的程式碼；用 `-O2` 編譯，不開 sanitizer（host 測試另外有 ASan/UBSan）。

### 11.5 kernel：檔案樹的 `/ehfs`（`kernel/vfs/ehfsfs.c`）

- **找卷宗**：開機時（macdisk 之後、檔案樹之前）依序看每個區塊裝置：sector 0 開始的裸卷宗，或 GPT 每個分割區的開頭——只認 superblock 的 magic，不看類型 GUID。第一個找到的以讀寫掛載（裝置不能寫就唯讀；區塊大小必須是 512）。開機訊息：`eHFS: "名稱" at /ehfs (read/write, 64 MiB)`。一次只有一個卷宗。
- **能做的事**：讀、寫、建立（`O_CREAT`、`O_EXCL`）、`O_APPEND`、`O_TRUNC`、`ftruncate`、`mkdir`、`rmdir`、`unlink`、`rename`。shell 裡用工具箱：`box ls /ehfs`、`box cat /ehfs/檔案`、`cp`、`mkdir`、`>`／`>>` 轉向（內建的 `ls`、`cat` 仍然是 HFS 卷宗的）。
- **名稱**：UTF-8 原樣；比較照 eHFS（NFC、不分大小寫），所以 `/ehfs/READ ME.TXT` 找得到 `Read Me.txt`；只改大小寫的 `rename` 可以。
- **權限**：沒有 Unix 的擁有者與權限；顯示成資料夾 0755、檔案 0644。鎖定的項目（旗標 bit 0）顯示 0555／0444，開來寫回 `EACCES`，刪除回 `EPERM`。
- **`rename` 的目標已經存在**時，先刪掉目標（檔案、或空的資料夾）再搬：兩個交易，中間斷電的話目標已經不在、來源還在原處。
- **F4**：`ehfsfs_set_mac_running(true)` 期間，會改變卷宗的操作都回 `EROFS`（讀照常）；K5 在 Mac 執行時用。
- **開著的檔案**記住卷宗的「代」：卸載或換了卷宗之後讀寫回 `EIO`。檔案被刪掉之後，開著的也讀不到（沒有 Unix「刪了還能讀到關檔為止」的語意）。
- **關機**（`poweroff`、`reboot`、電源鍵、Mac 的 Shut Down／Restart）前卸載，superblock 標成乾淨。不卸載也不會壞（有日誌），只是 `fsck.ehfs` 會說沒有正常卸載。
- **時間**：寫入的時間和使用者程式的 `clock_gettime(CLOCK_REALTIME)` 同一個來源（RTC 加單調時鐘）。
- **限制**：`readdir` 每次從資料夾的開頭走（很大的資料夾列出來是 O(n²)）；資源 fork 與延伸屬性在檔案樹裡看不到（Mac 端與 `ehfs-cp` 才用）。
- **測試**：host `test_ehfsfs`（檔案、資料夾、名稱、rename、F4、沒有卷宗與唯讀裝置）；`make test-ehfs-qemu`：`sgdisk` 在 `build/` 的映像檔建一個 GPT 分割區、`mkfs.ehfs`、`ehfs-cp` 放一個檔案，QEMU 裡用 shell 讀、建資料夾、複製、附加、`poweroff`，再在 host 上 `fsck.ehfs`（乾淨、正常卸載、檔案與資料夾數）並取出檔案比對。
