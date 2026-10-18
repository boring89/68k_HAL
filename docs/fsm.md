# File System Manager 外部檔案系統外掛：研究筆記（第二階段的準備，尚未實作）

> **狀態：研究筆記。** P10 的自動排程整理，**沒有程式碼**。給第二階段「Mac 透過外掛使用 eHFS（`docs/ehfs.md`）」做準備；實際的設計決策要跟使用者確認（第 6 節）。

## 1. 要解決什麼

Mac 的 File Manager（`PBHOpen`、`PBGetCatInfo`……所有檔案呼叫的入口）只懂 HFS 與 MFS。eHFS 的格式 Mac 不認得，所以要有人把「Mac 的檔案呼叫」翻成「eHFS 的操作」。

比喻：File Manager 是郵局櫃台，只會處理國內信（HFS）。File System Manager（FSM）是 System 7.5 加的「國際郵件窗口」：外國的郵政（外部檔案系統）來登記，櫃台遇到寄往那個國家（那顆卷宗）的信，就轉給它處理。

## 2. FSM 是什麼（System 7.5 起內建）

- System 7.5 起 FSM 是系統的一部分（之前是 FSM 1.2 的擴充檔，System 7.0／7.1 用）。檢查：Gestalt `gestaltFSAttr` 的 `gestaltHasFileSystemManager` 位元（Apple Q&A FL10）。
- 以前寫外部檔案系統要懂 File Manager 沒有公開的低記憶體變數與資料結構；FSM 提供登記的方法與一組工具函式，讓外部檔案系統不必自己動那些結構（Technote OV 21「System 7.5」）。完整的介面文件是 MacOS SDK 光碟上的「File System Manager」資料夾，標頭是 Universal Interfaces 的 `FSM.h`。
- 每個登記的檔案系統有一個 **file system descriptor（FSD，`FSDRec`）**，FSM 把它們放在自己的佇列裡；`GetFSInfo`／`SetFSInfo` 讀寫它（Apple 範例 `IsPCExchangeInstalled.c` 用 `GetFSInfo` 查 PC Exchange 的 MS-DOS 檔案系統）。

## 3. 一個外部檔案系統要提供什麼（以 Basilisk II 的 ExtFS 為例）

Basilisk II（`BasiliskII/src/extfs.cpp`）把主機的資料夾變成 Mac 上的一顆卷宗，正是用 FSM：

```
登記（開機時，InstallExtFS）
  1. Gestalt 確認有 FSM 1.2 以上
  2. 在 Mac 記憶體裡放幾段 68k 小程式（stub）：每段是一個模擬 opcode，或呼叫 FSM 的 trap $A824
  3. FSMDispatch selector 0 = InstallFS(FSD)：
       fsdVersion = 1、fileSystemFSID = 自己的 FSID、fileSystemName
       fileSystemCommProc = 通訊程序的 stub
       HFS component interface：compInterfProc = HFS 程序的 stub、自己的堆疊（64 KiB）、
                                 compInterfMask |= 啟用、資源已載入、「處理 HFS 呼叫」
  4. FSMDispatch selector 5 = SetFSInfo：啟用 HFS component
  5. _AddDrive 一台假的固定磁碟（dsQFSID = 自己的 FSID）
  6. PBVolumeMount（HFSDispatch $A260，selector $41）掛上卷宗

執行時
  File Manager 遇到 vcbFSID = 我們的 FSID 的卷宗 → FSM → HFS 程序
    (vcb, selectCode, paramBlock, globalsPtr, fsid)，68k stub 執行模擬 opcode 後 RTD #16 返回
  通訊程序 (message, paramBlock, globalsPtr)：載入／卸載、磁碟圖示、
    「這台磁碟／這個 VolumeMount 是不是你的」
```

- HFS 程序依 selectCode（遮掉旗標後）分派：開檔／關檔、讀寫、取得／設定卷宗與檔案資訊（`GetCatInfo`／`SetCatInfo`）、建立、刪除、改名、搬移（`CatMove`）、建立資料夾、EOF／檔案位置、working directory、FCB 資訊、`GetVolParms`、flush；鎖定範圍是 no-op；不認得的回 `paramErr`。（System 7.0 起 HFSDispatch 不限制 selector 範圍，外部檔案系統自己判斷能不能處理並回傳錯誤，Technote FL520。）
- 它用的 FSM 工具函式（trap $A824）：`UTAllocateVCB`、`UTAddNewVCB`、`UTDisposeVCB`、`UTDetermineVol`、`UTGetDefaultVol`、`UTSetDefaultVol`、`UTParsePathname`、`UTGetPathComponentName`、`UTAllocateFCB`、`UTReleaseFCB`、`UTIndexFCB`、`UTResolveFCB`、`UTAdjustEOF`、`UTAllocateWDCB`、`UTReleaseWDCB`、`UTResolveWDCB`、`UTCheckWDRefNum`。Apple 的建議：能用 File Manager 呼叫就不要直接碰 FCB，必要時用這些存取函式（Q&A FL10）。
- 對照：檔案與資料夾各有一個 CNID（根目錄 2、它的父親 1，其他依序配置）、父 CNID、主機名稱與 Mac Roman 的名稱；名稱以 `.` 開頭的隱藏；`:` 與 `/` 互換；改名與搬移時交換 CNID 讓檔案 ID 不變。resource fork 與 Finder 資訊由主機端的函式提供（不支援的平台回報大小 0 的假 fork）。VCB：`vcbSigWord = $4244`（'BD'，看起來像 HFS）、`vcbFSID` = 自己的 FSID。

### 授權

Basilisk II 是 **GPL 第 2 版「或（依你的選擇）任何更新的版本」**（`extfs.cpp` 的標頭；Debian 的 copyright 檔也是 GPL-2+）。和本專案的 GPL-3.0-or-later 相容，可以參考，必要時也可以移植（保留原授權聲明）。但它的資料結構偏向「主機的檔案系統」，eHFS 有自己的 CNID 與 B-tree，**建議只參考流程，自己照 FSM 文件寫**（第 6 節 F1）。

## 4. 套到本專案

```
Mac（模擬的 68k）                         HAL（x86-64）
  File Manager
    └─ FSM ─ HFS 程序 stub（stub 視窗）── 模擬 opcode ──► emu/mac：fsm 轉接（參數區塊 ↔ C 結構）
                                                              └─ 回呼 ──► kernel/fs/ehfs（eHFS 本體）
```

- 手法和 `.Disk` 驅動完全一樣（`docs/mac.md` 第 6 節）：stub 視窗裡的 68k 程式碼、`$71xx` 模擬 opcode、`emu/mac` 不知道硬體，透過 `mac_t` 上的回呼找 kernel。注意 P10 D3 學到的：兩個模擬 opcode 之間要隔一個一般指令。
- 登記的時機：要在 FSM 與 File Manager 都準備好之後（Basilisk II 在 `InstallExtFS` 被呼叫時；我們可以用和 P10 D3 相同的方法——Finder 啟動後注入一段程式，或在開機流程的某個 patch 點）。
- CNID：eHFS 是 64 位元，Mac 的 dirID／fileID 是 32 位元（`docs/ehfs.md` 第 7 節、第 9 節 E5）。
- 名稱：UTF-8 ↔ Mac Roman 31 字元的轉換與 `#CNID` 後綴（`docs/ehfs.md` 第 6 節）。
- 非同步呼叫：File Manager 的呼叫可以是非同步的（ioCompletion）；外掛在模擬 opcode 裡同步做完，再照非同步的規則回呼（和 `.Disk` 驅動的 IODone 一樣）。

## 5. 另一條路（比較用）

| 方式 | 做法 | 優點 | 缺點 |
|---|---|---|---|
| A. FSM 外掛（上面） | Mac 的檔案呼叫直接翻成 eHFS 操作 | 沒有 HFS 的任何限制（除了 Mac 端的 31 字元與 32 位元 ID）；Mac 與 HAL 可以同時用同一顆卷宗 | 要實作的呼叫很多；Finder 的桌面資料庫（Desktop Manager，`PBDTxxx`）要另外處理或交給 Finder 自己建 |
| B. 假的 HFS 磁碟 | `.Disk` 驅動即時「合成」HFS 的區塊（MDB、catalog）給 Mac | Mac 端什麼都不用改 | HFS 的限制全部回來（2 GB、65535 個配置區塊、31 字元）；合成 B-tree 很複雜；寫入要反解析 |

建議 A（這也是使用者同意的方向）。

## 6. 實作前要決定的事

| # | 問題 | 建議 |
|---|---|---|
| F1 | 自己照 FSM 文件寫，還是從 Basilisk II 的 extfs.cpp 移植 | 自己寫（eHFS 的結構不同）；授權上兩者都可以 |
| F2 | 需要 FSM 的正式文件（MacOS SDK 的「File System Manager」與 `FSM.h`） | 使用者若有 SDK 光碟或 Universal Interfaces，提供路徑；否則以 Basilisk II 的用法與 Apple 的 Technote 為準 |
| F3 | 桌面資料庫（Desktop Manager） | 先讓 `GetVolParms` 回報不支援 Desktop Manager（Finder 應該會改用卷宗上舊式的 Desktop 檔——**未驗證**，實作時先試）；之後再加 |
| F4 | HAL 端與 Mac 同時寫同一顆 eHFS | 先不允許（Mac 執行時 HAL 只讀），之後再加鎖 |

## 參考

- Apple Q&A FL10（FSM 的 FCB 存取函式、Gestalt 檢查）：https://developer.apple.com/library/archive/qa/fl/fl10.html
- Technote OV 21「System 7.5」（FSM 的由來；文件在 MacOS SDK）
- Technote FL520「File Manager Overview Q&As」（外部檔案系統與 selector 範圍）：https://developer.apple.com/library/archive/technotes/fl/fl_520.html
- Apple 範例 `IsPCExchangeInstalled.c`（`GetFSInfo` 與 `FSDRec`）：https://developer.apple.com/library/archive/samplecode/Is_PC_Exchange_Installed/Listings/IsPCExchangeInstalled_c.html
- Basilisk II `extfs.cpp`（GPL-2.0-or-later）：https://github.com/cebix/macemu/blob/master/BasiliskII/src/extfs.cpp
- Debian 的 basilisk2 copyright 檔（GPL-2+）：https://metadata.ftp-master.debian.org/changelogs/contrib/b/basilisk2/stable_copyright
