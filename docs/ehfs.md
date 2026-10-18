# eHFS：設計草稿（第二階段，尚未實作）

> **狀態：草稿。** 由 P10 的自動排程依使用者 2026-10-18 已同意的決策寫成，**沒有任何程式碼**。eHFS 是協作模式的新元件：實作前要逐項跟使用者確認（第 9 節列出還沒決定的事）。

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

超過 8 段的檔案，其餘的 extent 放在 extents B-tree，鍵 =（CNID, fork 種類, 該段在檔案中的起始區塊）。

### 4.4 延伸屬性（attributes B-tree）

鍵 =（CNID, 屬性名稱 UTF-8）；值小的（≤ 3 KiB）直接放在記錄裡，大的用 fork 描述。用途：Linux 的 xattr、之後的其他 metadata。Finder 資訊與 type/creator **不**放這裡（內建欄位，Mac 每次都要用）。

## 5. 日誌（只記 metadata）

- 物理區塊日誌（像 ext4 的 jbd2、HFS+ 的 journal）：一個交易 = 描述區塊（要改哪些區塊）＋ 那些 metadata 區塊的新內容 ＋ 提交區塊（含 CRC32C）。提交後再寫到真正的位置（checkpoint）。掛載時若有提交完整、還沒 checkpoint 的交易就重做；不完整的丟掉。
- **ordered 模式**：新配置給檔案的資料區塊先寫，metadata 交易後提交——斷電後不會看到別人的舊資料。
- 資料本身不寫日誌、不算 checksum（效能；第 9 節）。
- 日誌是 CNID 7 的檔案，mkfs 時配置（預設 64 MiB）。

## 6. 名稱：UTF-8 與 Mac 端的 31 字元

- 磁碟上只存 UTF-8 原名。
- Mac 端（FSM 外掛）顯示轉換後的名稱：UTF-8 → Mac Roman（不能表示的字元換成 `_` 或近似字），超過 31 字元就截斷並加上 `#` 與 CNID 的十六進位（例：`A very long file name th#1A2B`），保證同一資料夾裡不重複。Mac 端用轉換後的名稱開檔時，外掛反查：先找完全相符的原名，再解 `#CNID`。
- Mac 端建立或改名的檔案：Mac Roman → UTF-8 存回去。

## 7. 與 Mac 的介面（摘要；細節在 `docs/fsm.md`）

- System 7.5 的 File System Manager（FSM）讓外部的檔案系統以 HFS 呼叫（`PBGetCatInfo`、`PBHOpen`……）的形式出現在 Finder 裡。
- Mac 的 dirID／fileID 是 32 位元：外掛直接用 CNID 的低 32 位元；CNID 依序配置，40 億個以內不會衝突（超過時的處理見第 9 節）。
- 卷宗的資料在 HAL 端讀寫（kernel 裡的 eHFS 程式碼），Mac 端的外掛只是轉接：一個模擬 opcode 把參數區塊交給 HAL（和 `.Disk` 驅動相同的手法，`docs/mac.md` 第 6 節）。

## 8. 工具與程式碼的組織（預定）

```
kernel/fs/ehfs/      freestanding 的核心（B-tree、bitmap、日誌、CRC32C）：kernel 與工具共用
tools/ehfs/          Linux 端：mkfs.ehfs、fsck.ehfs、ehfs-cp（複製進出，fork 與 Finder 資訊用 AppleDouble 或 xattr）
emu/mac/fsm_*        Mac 端的 FSM 外掛（68k 碼）與它的模擬 opcode
```

host 單元測試照現在的做法（`tests/host/`）；fsck 是測試的主力（每個寫入測試後跑一次 fsck）。

## 9. 還沒決定的事（實作前要問使用者）

| # | 問題 | 選項 | 建議（理由） |
|---|---|---|---|
| E1 | Unicode 正規化 | NFC（Linux、Windows 的習慣）／NFD（舊 macOS 的習慣）／不正規化只比較 | NFC：和 Linux 工具互通最少意外 |
| E2 | 大小寫比較用哪個版本的 Unicode 表 | 固定某版（例如 15.0）並寫進 superblock／跟著程式更新 | 固定並記錄版本：表一改，舊卷宗的排序會壞 |
| E3 | B-tree 節點大小 | 4 KiB／16 KiB | 16 KiB：目錄大時樹比較矮；4 KiB 實作簡單 |
| E4 | 資料要不要 checksum | 不要（只有 metadata）／可選 | 不要：先求簡單，之後可用 ro_compat 旗標加 |
| E5 | CNID 超過 2^32 時 Mac 端怎麼辦 | 拒絕建立／Mac 端只看得到低位元不衝突的 | 拒絕（40 億個檔案在這台機器上不會發生）並在 fsck 報告 |
| E6 | 區塊大小只允許 4 KiB 嗎 | 只 4 KiB／4–64 KiB 可選 | 先只 4 KiB；superblock 留著欄位 |
| E7 | Linux 端複製工具的 fork 格式 | AppleDouble（`._name`）／xattr（`com.apple.ResourceFork`）／兩者 | xattr：ext4 支援，不會多出檔案 |
| E8 | 系統碟的疊加層變成 eHFS 裡的檔案（記憶中的第二階段計畫）時，斷電安全怎麼和日誌配合 | 疊加層檔案預先配置、只覆寫（不改 metadata）／走日誌 | 預先配置：疊加層自己已經有斷電安全的順序 |
| E9 | 名字：eHFS 確定嗎 | — | 使用者提議時帶問號 |

## 10. 為什麼這樣設計（參考）

- HFS：Inside Macintosh: Files（MDB、catalog B-tree、thread 記錄、extents overflow）。
- HFS+：Apple Technical Note TN1150（fork 描述內含 8 個 extent、特殊檔案也有 CNID、日誌）。只參考結構，不複製程式碼。
- ext4／jbd2：日誌的交易格式（描述區塊、提交區塊）、feature 旗標的 compat／ro_compat／incompat 三分法。只參考概念（Linux 的程式碼是 GPL-2.0-only，不能引入）。
- exFAT：bitmap 配置與簡單的 metadata checksum。
