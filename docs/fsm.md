# File System Manager 外部檔案系統外掛

> **狀態（P11 K3）**：已實作唯讀的版本：HAL 的檔案樹在 Mac 桌面上是一顆唯讀的「HAL」卷宗。第 1–3 節是 P10 的研究筆記（原理），第 4 節起是實作。K5 會接上 eHFS（`docs/ehfs.md`）並加入寫入。

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

## 4. 本專案的實作（P11 K3）

```
Mac（模擬的 68k）                          HAL（x86-64 或 host）
  File Manager
    └─ FSM ─► emu/mac/fsm.s 的 hfs／comm 程序（stub 視窗 0x4F000800）
                 │ 模擬 opcode $713B／$713C／$713D／$713E
                 ▼
              emu/mac/fsm.c：參數區塊 ↔ C、FCB、VCB、路徑解析
                 │ mac_fs_ops_t（以 ID 為主：get、lookup、child、read）
                 ▼
              emu/mac/fs_paths.c：路徑 ↔ ID 的對照（宿主的名字、Mac 的名字）
                 │ mac_fs_host_t（以路徑為主：entry、stat、read）
         ┌───────┴────────┐
 kernel/mac/fs_hal.c   tools/mac_run.c -A <資料夾>
   HAL 的 VFS             host 的一個資料夾
```

### 4.1 登記（`install`）

Finder 開始執行以後，模型在一個安全的時間點（supervisor、沒有在處理中斷、PC 不在 ROM 裡、**File Manager 沒有在處理請求**：低記憶體 `$360` FSBusy 與 `$362` 佇列頭都是 0）把 `fsm.s` 的 `install` 注入執行（和 P10 D3 掛上其他磁碟同一個機制；先掛磁碟，再登記檔案系統）。它依序：

1. Gestalt `'fs  '` 的 bit 2（有 FSM）、`'fsm '` ≥ 1.2。
2. NewPtrSysClear：外掛的堆疊（16 KiB）與資料區（FSDRec、假磁碟的 DrvSts、VolumeMountInfoHeader、參數區塊、每層呼叫的暫存區）。
3. AddDrive：一台假的固定磁碟（drive number = 佇列裡最大的再加一，驅動程式是 .Disk，`dsQFSID` = 我們的 FSID `'HH'`）。
4. FSMDispatch 0 = InstallFS；設定 HFS component 的 mask（啟用、資源已載入、處理 HFS 呼叫）後 FSMDispatch 5 = SetFSInfo。
5. HFSDispatch `$41` = PBVolumeMount（media type `'68KH'`）：FSM 先問我們的通訊程序「這是你的嗎」，再呼叫我們的 VolumeMount：UTAllocateVCB → 填 VCB（簽名 `'BD'`、硬體鎖定 `vaHardLock`）→ UTAddNewVCB → PostEvent(diskEvt)，Finder 就在桌面上顯示這顆卷宗。

**踩過的坑**：注入時如果正好打斷一個進行中的 File Manager 請求，我們的同步呼叫（PBVolumeMount）會排在它後面等它完成，而它永遠不會完成（它在我們底下被暫停了）：Mac 停在手錶游標。所以注入前要確認檔案系統佇列是空的（host 上用 128 MB 加一顆額外磁碟重現、修正；這個檢查也保護 P10 D3 的磁碟掛載）。

### 4.2 原生程式不能呼叫 Mac OS：重播

FSM 的工具函式（UT…，trap `$A824`）是 68k 程式；模擬 opcode 的處理程式裡不能再執行 68k 程式（Musashi 不支援巢狀執行）。做法：

```
fsm.s: hfs ─► $713B 開始 ─► run: $713D step ──(完成：D5 = -1，D0 = 結果)──► 結果寫到堆疊、RTD #16
                                  │
                                  └─(要一個 trap 呼叫：D5 = 0)─► call：把 C 準備好的參數複製到堆疊，
                                        執行 trap_slot（C 寫入的 trap 字）─► $713E 記下 D0/A0 ─► 回到 step
```

C 的處理程式每次 step 都**從頭執行**：已經做過的呼叫直接拿到記下的結果（「重播」），遇到還沒做的就請 68k 去做、先返回。所以處理程式寫成一般的直線程式，只要遵守一條規則：**對 Mac 記憶體的修改放在它依賴的呼叫之後，或者是重複做也沒關係的修改**。每層呼叫（install 裡的 PBVolumeMount 會再進到 HFS 程序）有自己的一組記錄（最多 4 層、每層 32 個呼叫）與暫存區。

### 4.3 處理的請求

| 請求 | 做法 |
|---|---|
| VolumeMount、MountVol、UnmountVol（UTDisposeVCB）、Eject、Offline、Flush | 見 4.1；其餘回 noErr |
| GetVolInfo（H 與非 H）、GetVolParms | 卷宗資訊；`vMAttrib` = kNoMiniFndr、kNoVNEdit、kNoLclSync、kTrshOffLine、kNoSwitchTo、kNoBootBlks、kNoSysDir、kHasExtFSVol（**沒有** Desktop Manager） |
| GetVol、SetVol、OpenWD、CloseWD、GetWDInfo | UTGetDefaultVol、UTSetDefaultVol、UTCheckWDRefNum、UTAllocateWDCB、UTReleaseWDCB、UTResolveWDCB |
| GetCatInfo、GetFileInfo（H 與非 H） | 依 ioFDirIndex：< 0 依 ID、0 依名稱、> 0 第幾個（GetFileInfo 只數檔案）。名稱只在依索引或 ID 查時寫回 |
| Open、OpenDF、OpenRF | 只給讀取：要求寫入的權限回 **permErr**（-54）。實測 SimpleText 收到 permErr 會改用唯讀重開；收到 wPrErr、vLckdErr、wrPermErr 就放棄。UTAllocateFCB 後填 FCB（`fcbFlNm` = 檔案 ID，資料或資源 fork，鎖定） |
| Read、GetEOF、GetFPos、SetFPos、Close、GetFCBInfo | 位置的四種模式、換行模式（ioPosMode bit 7）、讀到結尾回 eofErr；UTResolveFCB、UTIndexFCB、UTReleaseFCB |
| Write、Create、Delete、Rename、SetFileInfo、SetCatInfo、DirCreate、CatMove、SetVolInfo、鎖定 | wPrErr（卷宗是鎖定的） |
| 其他 | paramErr（FSMakeFSSpec 等由 File Manager 自己用上面的呼叫完成） |

路徑解析由 C 自己做：UTDetermineVol 告訴我們卷宗是怎麼指定的（完整路徑、vRefNum、working directory、預設卷宗），得到起點的資料夾；之後 `a:b:c`、`:a`、`::`（上一層）、結尾的 `:`（資料夾本身）照 Inside Macintosh: Files 的規則走。

### 4.4 名字、ID 與檔案類型（`fs_paths.c`）

- 每個 Mac 看過的項目有固定的 ID（從 16 開始，`fsUsrCNID`），表格最多 2048 個；根目錄是 2。
- 名字：ASCII 照用，`:` 換成 `/`，其他字元換成 `_`；換過字或超過 31 字元的名字以 `#<ID>` 結尾（保持唯一）。`.` 開頭的名字不顯示。完整的 UTF-8 ↔ Mac Roman 在 K4（eHFS）。
- 檔案類型依副檔名：`.txt`、`.c`、`.h`、`.md`、`.lua` 等是 `TEXT`／`ttxt`（SimpleText 文件），其他是 `????`。宿主的檔案沒有資源 fork（長度 0）。
- kernel：HAL 的 VFS 整棵樹，頂層的 `dev` 與 `proc` 不給 Mac；`68k_hal.cfg` 的 `mac_hal_volume = off` 關掉。host：`mac_run -A <資料夾>`。

### 4.5 Desktop Manager（F3）

我們沒有 Desktop Manager（GetVolParms 沒有 kHasDesktopMgr，也不處理 DT 呼叫）。實測 Finder 7.5.3：唯讀的卷宗照常顯示圖示；`TEXT`／`ttxt` 的檔案顯示 SimpleText 文件的圖示、雙擊會用 SimpleText 打開（應用程式的資訊來自系統碟的 Desktop DB）；卷宗本身用標準的磁碟圖示（通訊程序的 GetIcon 回 afpItemNotFound）。

### 4.6 測試

- `tests/host/test_fs_paths.c`：名字的轉換、ID 不變、不分大小寫的查詢、隱藏檔、讀取。
- `make test-mac-fsm`（本機）：host 的資料夾掛成 HAL 卷宗 → Finder 掛上、打開 → 拖一個檔案到系統碟（用 hfsutils 從存下的磁碟讀回比對）→ SimpleText 打開另一個（先要寫入被拒、再唯讀打開、讀取）。
- `make test-rom-qemu`（本機）：kernel 與 host 掛上 HAL 卷宗後畫面相同（第三組 `hal`）。
- `make test-mac-disk`（本機）：68k_HAL 磁碟上 `dmesg` 有 `the HAL volume is mounted`；第二次開機用鍵盤（H、A、L、Command-O）打開它，截圖 `build/mac/test_disk_hal.ppm`。

### 4.7 限制

- 唯讀（K5 加寫入）。宿主的檔案沒有資源 fork 與 Finder 資訊（圖示位置由 Finder 決定）。
- 名字只有 ASCII（K4 之後改成 Mac Roman）。
- ID 表格最多 2048 個項目，超過的看不到。
- 一個資料夾的項目每次都從宿主重新列出（大資料夾較慢）。

## 5. 另一條路（比較用）

| 方式 | 做法 | 優點 | 缺點 |
|---|---|---|---|
| A. FSM 外掛（上面） | Mac 的檔案呼叫直接翻成 eHFS 操作 | 沒有 HFS 的任何限制（除了 Mac 端的 31 字元與 32 位元 ID）；Mac 與 HAL 可以同時用同一顆卷宗 | 要實作的呼叫很多；Finder 的桌面資料庫（Desktop Manager，`PBDTxxx`）要另外處理或交給 Finder 自己建 |
| B. 假的 HFS 磁碟 | `.Disk` 驅動即時「合成」HFS 的區塊（MDB、catalog）給 Mac | Mac 端什麼都不用改 | HFS 的限制全部回來（2 GB、65535 個配置區塊、31 字元）；合成 B-tree 很複雜；寫入要反解析 |

建議 A（這也是使用者同意的方向）。

## 6. 決定（使用者 2026-10-19「全部採用建議」）

| # | 問題 | 決定 |
|---|---|---|
| F1 | 自己寫或移植 Basilisk II | 自己寫；Basilisk II 只參考流程與結構的偏移 |
| F2 | FSM 的正式文件 | 沒有 SDK：依 Apple 的 Technote、Q&A 與 Basilisk II 的用法 |
| F3 | Desktop Manager | 先不支援（第 4.5 節：Finder 照常運作） |
| F4 | HAL 與 Mac 同時寫同一顆卷宗 | Mac 執行時 HAL 只讀（K5） |

## 參考

- Apple Q&A FL10（FSM 的 FCB 存取函式、Gestalt 檢查）：https://developer.apple.com/library/archive/qa/fl/fl10.html
- Technote OV 21「System 7.5」（FSM 的由來；文件在 MacOS SDK）
- Technote FL520「File Manager Overview Q&As」（外部檔案系統與 selector 範圍）：https://developer.apple.com/library/archive/technotes/fl/fl_520.html
- Apple 範例 `IsPCExchangeInstalled.c`（`GetFSInfo` 與 `FSDRec`）：https://developer.apple.com/library/archive/samplecode/Is_PC_Exchange_Installed/Listings/IsPCExchangeInstalled_c.html
- Basilisk II `extfs.cpp`（GPL-2.0-or-later）：https://github.com/cebix/macemu/blob/master/BasiliskII/src/extfs.cpp
- Debian 的 basilisk2 copyright 檔（GPL-2+）：https://metadata.ftp-master.debian.org/changelogs/contrib/b/basilisk2/stable_copyright
