# 虛擬 NuBus 顯示卡（emu/mac）

這份文件說明 Mac IIci 模型裡的**虛擬 NuBus 顯示卡**：Mac 以為 slot `$C` 插了一張顯示卡，可以選 640x480 到 3840x2160 的解析度與 1／2／4／8／16／32 位元色深；kernel 把卡的畫面依 HiDPI 比例放大到實體螢幕。這部分由自動排程（P9）完成，**使用者尚未審核**。第 1–6 節是設計（P9 N0，已依實作更新：卡最後放在 slot `$C`，原因見第 7 節），第 7 節以後是實作結果。

## 1. 為什麼要一張「卡」

IIci 的內建視訊（RBV＋512 KiB 的 VRAM，第 3 節的 `0xFBB08000`）只支援 Apple 的固定顯示器（感測線決定，最大 640x480、8 位元）。真的 Mac II 系列要更大的螢幕就插一張 NuBus 顯示卡。卡上有一顆 **declaration ROM**（宣告 ROM）：一份「自我介紹」，告訴 Mac 的 Slot Manager「我是誰、我是顯示卡、我的 VRAM 在哪、支援哪些模式、驅動程式在這裡」。Mac 開機時 Slot Manager 讀每個 slot 的 declaration ROM，System 再依它建立 GDevice（QuickDraw 的「螢幕」）。

比喻：declaration ROM 是卡的「名片＋使用說明書」，驅動程式是「翻譯員」：QuickDraw 說「換成 256 色」，驅動把它變成對卡的硬體動作。我們的卡沒有硬體——翻譯員把要求直接交給原生 C 程式（模擬 opcode，同 `.Disk` 驅動）。

做法參考 Basilisk II（`slot_rom.cpp`、`video.cpp`；GPL-2.0-or-later，**只參考作法**，程式碼自己寫）與 *Designing Cards and Drivers for the Macintosh Family*（第 3 版）、*Inside Macintosh: Devices*（Slot Manager）、*Display Device Driver Guide*。

## 2. 位址配置（32 位元定址）

NuBus 每個 slot `$s`（`$9`–`$E`）有兩段位址：

```
 標準 slot 空間  0xFs000000 – 0xFsFFFFFF   16 MiB；頂端放 declaration ROM
 super slot 空間 0xs0000000 – 0xsFFFFFFF   256 MiB；放 VRAM
```

我們的卡（slot `$C`，`MAC_CARD_SLOT`）：

```
 0xC0000000 ┌──────────────────────────┐
            │ VRAM 32 MiB              │  3840x2160x32 位元 = 33,177,600 位元組 ≤ 32 MiB
 0xC2000000 ├──────────────────────────┤
            │ （空）                    │
 0xFC000000 ├──────────────────────────┤
            │ （空）                    │
 0xFCFFFBxx │ declaration ROM（約 1.1 KiB）│ 四條 byte lane 都用（ByteLanes = 0x0F），
 0xFCFFFFFF └──────────────────────────┘  所以 ROM 的位元組在位址上是連續的
```

- 只有 slot `$C` 回應。P8 修好的「空 slot 不能看起來像有卡」（`docs/mac.md` 第 4 節）仍成立：`SLOT_PROBE` 只對 slot `$C` 的位址回報「有卡」（真的讀一個位元組），其他 slot 照舊跳到 bus error 的復原位址。
- VRAM 用 sResource 的 **MajorBaseOS**（`0x0C`）＋ `sRsrcFlags` 的 `f32BitMode`；結果：ROM 的開機視訊碼把 `ScrnBase` 設成 `0xC0000000`（未知 1 解決）。驅動的 Open 也把 `dCtlDevBase` 寫成同一個值（保險）。

## 3. declaration ROM 的結構

依 *Designing Cards and Drivers*（第 8 章 "Firmware"）。ROM 從**尾端**讀起：最後 20 位元組是 format block，它指向 sResource directory。

```
 sResource directory          ID → 偏移（每筆 4 位元組：ID 1 byte＋24 位元有號偏移）
   ├─ 0x01 board sResource
   │     sRsrcType   (1) = 類別 1 Board, cType 0, DrvrSW 0, DrvrHW 0
   │     sRsrcName   (2) = "68k_HAL Video Card"
   │     BoardId     (32) = 0x6848
   │     PRAMInitData(33) → 2 個保留位元組＋vendorUse1..6（vendorUse1 = 開機時的 video mode）
   │     VendorInfo  (36) → VendorId、RevLevel、PartNum、Date
   └─ 0x80 video sResource
         sRsrcType   (1) = 類別 3 Display, cType 1 Video, DrvrSW 1 (Apple), DrvrHW 'hH'
         sRsrcName   (2) = "Display_Video_Apple_68kHAL"
         sRsrcDrvrDir(4) → sMacOS68020 (2) → DRVR（第 4 節）
         sRsrcFlags  (7) = fOpenAtStart | f32BitMode（第 7.5 節）
         sRsrcHWDevId(8) = 1
         MinorLength(11) = 32 MiB（ROM 的開機視訊碼一定要讀它，見第 7 節）
         MajorBaseOS(12) = 0、MajorLength(13) = 32 MiB
         GammaDir   (64) → 一個線性 gamma 表
         0x80–0x85   每種色深一個 mode list：
                      ├ mVidParams(1) → VPBlock（rowBytes、bounds、pixelType/Size、cmpCount/Size）
                      ├ mPageCnt (3) = 1
                      └ mDevType (4) = 0 CLUT / 2 直接色
 format block（最後 20 位元組）
   DirectoryOffset(4)  Length(4)  CRC(4)  RevLevel=1  Format=1  TestPattern=0x5A932BC7  Reserved=0  ByteLanes=0x0F
```

- 色深的 mode 編號：`0x80`=1、`0x81`=2、`0x82`=4、`0x83`=8、`0x84`=16、`0x85`=32 位元（Apple 的慣例：同一張卡的色深依序編號；計畫原本把 16 位元列為「可選」，因為只差一種像素格式的轉換，所以一起做）。
- **VPBlock 只能描述一種解析度**。多解析度的作法跟 Basilisk 一樣：驅動切換解析度時，原生端直接改 ROM 緩衝區裡的 VPBlock（rowBytes、bounds），重算 CRC，再由驅動呼叫 `_SUpdateSRT` 讓 Slot Manager 更新。ROM 在記憶體裡是我們自己的緩衝區，所以可以改。
- 產生器：`emu/mac/nubus_rom.c` 在執行時（`mac_init` 之後）產生，內容完全自己定義。比起 build 時用 Python 產生，執行時產生的好處是解析度清單與預設模式可以由 kernel 參數決定。CRC 演算法（*Designing Cards* 的 "CRC"）：每個位元組先把 32 位元的和左旋 1 位再加上該位元組，CRC 欄位本身算成 0。

## 4. 驅動程式（DRVR）

68k 組合語言 `emu/mac/nubus_driver.s`（像 `stub.s`：Makefile 組譯，位元組放在 `nubus_driver_code.h`，`make test` 檢查兩者一致）。驅動被 Slot Manager 從 declaration ROM 複製到 system heap 執行，所以只用 PC 相對定址。

```
 QuickDraw / Display Manager ──_Control/_Status──► Device Manager ──► DRVR（system heap）
                                                                      │ $71xx 模擬 opcode
                                                                      ▼
                                                     emu/mac/nubus_video.c（原生）
```

| 呼叫 | csCode | 原生端做什麼 |
|---|---|---|
| Control `cscSetMode` | 2 | 換色深（同一解析度）；回傳 base address |
| Control `cscSetEntries`／`cscDirectSetEntries` | 3／8 | 寫調色盤（循序或索引） |
| Control `cscSetGamma` | 4 | 接受（線性） |
| Control `cscGrayPage` | 5 | 畫 50% 灰的抖動圖樣 |
| Control `cscSetGray` | 6 | 灰階對應開關 |
| Control `cscSetInterrupt` | 7 | VBL 中斷開關 |
| Control `cscSetDefaultMode` | 9 | 記住 |
| Control `cscSwitchMode` | 10 | 換解析度＋色深（第 5 節） |
| Control `cscSavePreferredConfiguration` | 16 | 記住 |
| Status `cscGetMode`、`cscGetEntries`、`cscGetPages`、`cscGetBaseAddr`、`cscGetGray`、`cscGetInterrupt`、`cscGetGamma`、`cscGetDefaultMode` | 2–9 | 回報 |
| Status `cscGetCurMode`、`cscGetConnection`、`cscGetModeTiming`、`cscGetPreferredConfiguration`、`cscGetNextResolution`、`cscGetVideoParameters` | 10、12、13、16、17、18 | Display Manager 用來列出解析度 |

**VBL 中斷**：游標與 VBL task 由「主螢幕的垂直消隱中斷」驅動。真的卡拉 NuBus 的 /NMRQ，IIci 的 RBV 在 slot 中斷旗標（`0x02`，低電位有效）的 bit 0（= slot `$9`）反映，ROM 的 level 2 處理程式呼叫該 slot 的中斷佇列。所以：

- 模型每幀（60 Hz）讓卡的中斷成立（`cscSetInterrupt` 打開時），RBV 的 SIFR bit 3（slot `$C`）變 0。
- 驅動的 Open 用 `_SIntInstall` 掛一個 slot 中斷處理程式（`SQElem`），它用模擬 opcode 清掉卡的中斷，再呼叫 `JVBLTask`（低記憶體 `0xD28`，D0 = slot 9）執行這個 slot 的 VBL task，回傳 D0 = 1。
- 未知 2（已解決）：SIFR 的 bit 0–5 = slot `$9`–`$E`（Linux `via.c` 的對應），ROM 的 level 2 分派照這個呼叫 slot 的中斷佇列；但 slot 的中斷遮罩（RBV `0x12`）要由 InitHW 打開，而我們跳過了 InitHW，所以 `mac_reset` 在有卡時把 slot 位元打開（`RBV_SIER_SLOTS`）。

## 5. 多解析度與 Display Manager

- 解析度清單（顯示模式 ID `0x80` 起）：640x480、800x600、1024x768、1152x870、1280x720、1280x960、1280x1024、1440x900、1600x1200、1920x1080、1920x1200、2560x1440、3840x2160。
- `cscGetNextResolution`：上一個 ID 是 `0xFFFFFFFE` 時回第一個；`0` 回目前的；最後一個之後回 `0xFFFFFFFD`。`cscGetVideoParameters`（ID＋色深 → VPBlock）讓 Display Manager 不必讀 ROM。`cscGetConnection` 回報 "modeless connection"（`kModelessConnect` = 8，所有模式都 valid/safe）。
- `cscSwitchMode`：原生端換模式、改 ROM 的 VPBlock 並重算 CRC；驅動接著呼叫 `_SUpdateSRT`。
- 未知 3（已解決）：System 7.5.3 的 Monitors（v7.5.5）在「Options…」對話框列出解析度（"1024 × 768, 60Hz"…，經 `cscGetNextResolution`），按 OK 就即時切換（`cscSwitchMode`），桌面圖示重新排列。

## 6. 原生端與 kernel 的介面

```c
/* emu/mac/mac.h（設計時的摘要；實際的欄位見 mac_card_t） */
typedef struct { uint16_t width, height; } mac_video_res_t;
typedef struct {
    uint8_t *vram;            /* 32 MiB，host 提供 */
    uint8_t res, depth;       /* 目前的解析度索引與色深（0..5 = 1..32 位元） */
    uint32_t row_bytes;
    uint8_t clut[256][3];
    uint32_t dirty[...];      /* 1 KiB 為單位的髒區位元圖（bus 寫入時設定） */
    bool irq_enabled, irq_pending;
} mac_card_t;

int mac_card_render(mac_t *mac, uint32_t *out, uint32_t out_pitch, uint32_t *rows_out);  /* 只轉換有變動的列 */
```

- **髒區**：bus 寫入 VRAM 時把「位移 >> 10」那一格標記；render 只轉換碰到髒格的列，並回報哪些列變了（kernel 只把那些列放大輸出）。調色盤改變、模式改變時整張重畫。
- **kernel**（`kernel/mac/machine.c`）：畫面大小跟著卡的模式；`mac scale=1|1.5|2|auto`、`mac res=WxH`。整數倍用最近鄰；1.5 倍用最近鄰（每 2 個 Mac 像素變 3 個；比雙線性快，Mac 的 1 位元圖樣不會糊掉）。`auto`：在 {2, 1.5, 1} 中選「能放進實體螢幕且覆蓋面積最大」的組合，平手時取較大的比例。
- **預設解析度**：依實體螢幕自動選（同上規則）：2560x1440 → Mac 1280x720 以 2x 填滿；1920x1080 → 1280x720 以 1.5x 填滿；640x480 → 640x480 以 1x。
- **內建視訊**：預設關閉——RBV 回報的螢幕感測線改成 7（「沒有接螢幕」，*Guide to the Macintosh Family Hardware*：感測線 111 = 沒有顯示器）。結果（未知 4）：開機後只有一個 GDevice，是卡（`make test-rom` 檢查）。`mac_run -I`、kernel `mac builtin` 切回內建視訊（640x480），用來比較與除錯。


## 7. 實作結果（P9 N1–N4）

| 項目 | 結果 |
|---|---|
| Slot Manager | 找到卡；開機後只有 1 個 GDevice（卡），`ScrnBase = 0xC0000000` |
| 色深 | Monitors 可切 B&W、4、16、256、Thousands、Millions（截圖 `build/p9/`，`make clean` 會刪） |
| 解析度 | Monitors「Options…」列出 13 種，切換成功：800x600、1280x720、1920x1080、2560x1440、3840x2160（`make test-rom` 檢查其中三種） |
| 4K × 32 位元 | **不提供**：3840x2160 只到 16 位元（下面的「坑 4」） |
| 效能（QEMU/KVM，`make test-mac-perf`） | 實體 2560x1440、Mac 1280x720×32 位元 2x：不節流 178.8 frames/s；每幀模擬 5.3 ms；每次畫面更新 render 0.4 ms＋螢幕 2.2 ms（平均 72 列） |
| 開機時的色深 | `mac depth=…`（PRAMInitData＋修好的 CLKNOMEM，N8） |

**踩過的坑**：

1. **ROM 開機視訊碼要求 MinorLength**：`0x40800BF0` 用 `sReadLong` 讀 video sResource 的 ID `0x0B`，沒有就把這張卡當成不能用，改用「沒有螢幕」的假 GDevice。Basilisk 的 ROM 有 MinorBase／MinorLength，所以沒遇到。
2. **游標不動**：卡的 VBL 中斷要經 RBV 的 slot 中斷遮罩，而遮罩原本由被跳過的 InitHW 打開（見第 4 節）。症狀：選單能拉（選單追蹤直接讀滑鼠位置），但游標停在左上角。
3. **slot `$9` 沒有 PRAM**：IIci 只有 slot `$C`、`$D`、`$E` 三個 NuBus 插槽；Slot Manager 的 `sPutPRAMRec` 對 slot `$9` 回 -337（smSlotOOBErr）。所以卡從 `$9` 搬到 `$C`（計畫寫「例如 `$9`」）。
4. **3840x2160 × 32 位元會讓 System 當掉**：每列 15360 位元組。切到 Millions 後 heap 被像素值覆蓋（未實作存取的位址看起來是 `0x619A61AC` 這種顏色值），PC 跑到 `0x46001022`。2560x1440×32（每列 10240）正常、4K×16 正常。推測是 QuickDraw 或 Monitors 在堆疊上配一列大小的緩衝區。做法：每列超過 10240 位元組的解析度不提供 32 位元——ROM 的最後一個 mode list 項目在那個解析度變成 list 結尾（Monitors 不列 Millions），驅動也拒絕。
5. **工具的坑**：`mac_run` 的 `-J`／`-M`／`-K` 原本只收 32 筆，長腳本的最後幾下（OK 按鈕）被默默丟掉，看起來像 Mac 卡住。改成 256 筆。

**開機色深（P9 N8）**：ROM 的 `InitPRAMRecs`（`0x408061C0`）用 board sResource 的 PRAMInitData（ID 33，注意不是 37）填 slot PRAM，開機視訊碼（`0x40800DE0`）從 slot PRAM 的 vendorUse1 取開機模式。一開始沒有作用，追到的原因是**模型的時鐘晶片替身（CLKNOMEM）的 bug**：ROM 讀寫多個位元組的參數 RAM（`0x4080B1B8` 的迴圈）把下一個命令放在 D0，而 CLKNOMEM 每次都把 D0 清成 0，所以每次只讀寫到第一個位元組（slot PRAM、網路設定…全部受影響）。修正後（只回傳 D2，和 Basilisk II 一樣）：`mac depth=32`、`mac_run -C 1280x720x32` 直接以 Millions 開到 Finder。slot `$C` 的 PRAM 在 XPRAM `0x5E`–`0x65`（slot `$9` 從 `0x46` 起，每個 slot 8 位元組）。

連帶的變化（以前的行為其實靠這個 bug）：

- 以前「把網路設定（XPRAM `0xE0`）指向不存在的 Ethernet 卡讓 AppleTalk 不啟動」的做法，只因為多位元組讀取壞掉才有效；修好後 System 會跳「找不到 AppleTalk 驅動」的對話框。改成回報 SPConfig（XPRAM `0x13`）= 0（兩個序列埠都是 free，AppleTalk 關閉）。
- 以前 ROM 寫入參數 RAM 預設值時也只寫到第一個位元組，XPRAM `0x01` 留著 `0xFF`；開機碼向 `_InternalWait` 要的旗標（`0x40801464`）bit 7 因此是 1，就不等 SCSI 磁碟。修好後是 0，開機碼會輪詢 SCSI 直到逾時（Mac 時間約 10 秒，Finder 晚 500 幀出現）。現在讀 `0x01` 時固定設 bit 7–6（我們的 SCSI 匯流排永遠是空的）。
- 結果：開機時間與畫面和以前相同（1024x768 在第 1200 幀的 CRC 不變）；`make test-rom-qemu` 的黃金 CRC 仍由 host 與 kernel 一起決定。

### 效能（P9 N12，`make test-mac-perf MAC_PERF_RES=… MAC_PERF_DEPTH=…`，QEMU/KVM，實體 2560x1440，4000 幀不節流）

| Mac 的模式 | 倍率 | frames/s | 每幀模擬 | 平均每次更新（render＋螢幕） | 最慢的一次更新 |
|---|---|---|---|---|---|
| 1280x720 × 32 位元 | 2x | 175 | 5.6 ms | 0.2＋0.6 ms | 25 ms（整個畫面重畫） |
| 1920x1080 × 32 位元 | 1x | 172 | 5.7 ms | 0.2＋0.1 ms | — |
| 2560x1440 × 32 位元 | 1x | 171 | 5.7 ms | 0.2＋0.1 ms | 28 ms |
| 3840x2160 × 16 位元 | 0.5x（N13；之前只顯示左上角） | 152–170 | 5.7–6.2 ms | 0.3＋0.1 ms | 55 ms |
| 640x480 × 8 位元 | 3x | 186 | 5.3 ms | 0.3＋1.6 ms | — |

互動執行時 Mac 節流在 60 幀／秒（16.7 ms 一幀），所以平常的負擔約 6 ms／16.7 ms；整個畫面重畫（換模式、桌面全部重畫）時會掉一幀（4K 時兩三幀）。比實體螢幕大的 Mac 畫面（3840x2160 在 2560x1440 上）縮小成 0.5x（每兩個像素取一個；`mac scale=0.5`、Ctrl+Alt+PgDn 也可以到 0.5x），字很小但整個桌面看得到；連 0.5x 都放不下時才只顯示左上角。報告的最後一行 `mac: slowest screen update` 是這個最壞情況，實機請一起回報。

## 7.5 兩張卡（多螢幕，P9 N10）

`mac cards=2`（kernel）、`mac_run -2 WxH -q 第二個螢幕.ppm`（host）：第二張同樣的卡插在 slot `$D`（ROM 在 `0xFDFFxxxx`、VRAM 在 `0xD0000000`、RBV slot 中斷 bit 4）。程式上 `mac_t.card[]` 變成陣列；bus 依位址的 slot 找卡，驅動依 DCE 的 `dCtlSlot` 找卡，中斷處理程式的 sqParm 就是 slot。

- **要 fOpenAtStart**：一開始第二張卡沒有變成螢幕——ROM 只開啟開機螢幕的驅動；其他顯示卡的驅動要由 Slot Manager 在開機時開啟，條件是 sRsrcFlags 有 `fOpenAtStart`（bit 1）。現在 video sResource 的 flags = `fOpenAtStart | f32BitMode`。之後 System（沒有 `'scrn'` 資源時）替每張卡建一個 GDevice，桌面延伸到第二個螢幕，游標可以移過去（host 截圖）。
- **排列跟著 Mac**：System 把第二個螢幕放在主螢幕旁邊（同大小時在右邊；試過 1024x768＋800x600 時放在左邊）。`mac_card_position` 從 QuickDraw 的 DeviceList 找到該卡驅動的 GDevice，讀 gdRect 的左上角；kernel 依這個位置把兩個畫面擺在實體螢幕上，整體用同一個倍率、置中。在 Monitors 拖動螢幕，kernel 也會跟著重排（每幀檢查）。
- 第二張卡的解析度與第一張相同（預設解析度依「實體寬度的一半」選），可以在 Monitors 分別換。畫面 CRC 只算第一張。
- 驗證：`tests/host/test_mac_card.c` 的 `test_two_cards`；QEMU 2560x1440、兩張 640x480、2x 的截圖（`build/p9/n10_two_screens_half.png`）。

## 8. 檔案與介面

| 檔案 | 內容 |
|---|---|
| `emu/mac/nubus_video.c` | ROM 產生器（`build_rom`）、bus 存取、驅動的 Control／Status、VBL、`mac_render_changes` |
| `emu/mac/nubus_driver.s` → `nubus_driver_code.h` | 68k 驅動（`tools/gen_mac_stub.py` 產生，`make test` 檢查一致） |
| `emu/mac/mac.c` | slot 探測、bus 轉送、模擬 opcode `$712A`–`$712E`、RBV 感測線與 slot 中斷遮罩 |
| `kernel/mac/machine.c` | 畫面配置（`relayout`：大小、倍率、置中）、只畫變動的列、熱鍵 |
| `kernel/display/fb_scale.c` | `fb_blit_halves`（0.5 倍步進的最近鄰放大）、`fb_choose_size`（自動選解析度與倍率） |
| `tools/mac_run.c` | `-C WxH[xD]`（卡的起始解析度與色深）、`-I`（內建視訊） |
| `tools/mac_monitors.sh` | 用 Monitors 切解析度的滑鼠腳本 |

**kernel 的 `mac` 指令**：`mac [scale=0.5|1|1.5|2|2.5|3|4|auto] [res=WxH|auto] [depth=1|2|4|8|16|32] [cards=1|2] [builtin]`（`cards` 預設 1）。沒有 `res=`／`depth=` 時，從**上一次互動執行結束時**的解析度與色深開始（N16：在 Monitors 選的就會留著，直到 kernel 重開；Mac 自己記在磁碟覆蓋層的設定，執行結束就沒了），第一次則依螢幕自動選、1 位元；`res=auto` 忘掉上一次。預設 `res` 依實體螢幕選（2560x1440 → 1280x720 以 2x 填滿；1920x1080 → 1280x720 以 1.5x 填滿；640x480 → 640x480），預設倍率是能放進螢幕的最大值。執行中：Ctrl+Alt+PgUp／PgDn 放大／縮小半倍、Ctrl+Alt+Home 回到最大（這三個組合 kernel 自己吃掉，不送給 Mac；Control-Command-PgUp 在 Mac 沒有意義）。Mac 在 Monitors 換解析度時，kernel 自動重新計算置中與倍率（倍率維持使用者要的，放不下就降）。

**測試**：`tests/host/test_mac_card.c`（ROM 格式與 CRC、目錄、驅動位元組、bus、髒區與各色深的轉換、解析度清單、切換、4K 沒有 32 位元、VBL 中斷）、`tests/host/test_fb_scale.c`（1.5x、自動選擇）；本機：`make test-rom`（GDevice 數、Monitors 切三種解析度、兩張卡）、`make test-mac-remember`（`mac res=800x600`、停止、`mac` 仍是 800x600、`mac res=auto` 回到自動）、`make test-mac-hotkeys`（QEMU 送 Ctrl+Alt+PgDn／PgUp／Home，序列埠的倍率依序是 1.5x、2x、1.5x、1x、2x）、`make test-rom-qemu`（host 與 kernel 的畫面 CRC 相同，卡 640x480：`fa963b30`）、`make test-mac-perf`、`make mac-uefi-shot`。
