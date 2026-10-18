# 虛擬 NuBus 顯示卡（emu/mac）

這份文件說明 Mac IIci 模型裡的**虛擬 NuBus 顯示卡**：Mac 以為 slot `$9` 插了一張顯示卡，可以選 640x480 到 3840x2160 的解析度與 1／2／4／8／16／32 位元色深；kernel 把卡的畫面依 HiDPI 比例放大到實體螢幕。這部分由自動排程（P9）完成，**使用者尚未審核**。第 1–6 節是設計（P9 N0），第 7 節以後是實作結果。

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

我們的卡（slot `$9`）：

```
 0x90000000 ┌──────────────────────────┐
            │ VRAM 32 MiB              │  3840x2160x32 位元 = 33,177,600 位元組 ≤ 32 MiB
 0x92000000 ├──────────────────────────┤
            │ （空）                    │
 0xF9000000 ├──────────────────────────┤
            │ （空）                    │
 0xF9FFxxxx │ declaration ROM（約 2 KiB）│  四條 byte lane 都用（ByteLanes = 0x0F），
 0xF9FFFFFF └──────────────────────────┘  所以 ROM 的位元組在位址上是連續的
```

- 只有 slot `$9` 回應。P8 修好的「空 slot 不能看起來像有卡」（`docs/mac.md` 第 4 節）仍成立：`SLOT_PROBE` 只對 slot `$9` 的位址回報「有卡」（真的讀一個位元組），其他 slot 照舊跳到 bus error 的復原位址。
- VRAM 用 sResource 的 **MajorBaseOS**（`0x0C`，super slot 空間裡的偏移 0）。**未知 1**：IIci ROM 的 Slot Manager／InitDevices 在 32 位元模式下是否用 MajorBaseOS 計算 `dCtlDevBase`。備案：驅動的 Open 自己把 `dCtlDevBase` 設成 `0x90000000`（Basilisk 在切換模式時也直接寫 `dCtlDevBase`）。

## 3. declaration ROM 的結構

依 *Designing Cards and Drivers*（第 8 章 "Firmware"）。ROM 從**尾端**讀起：最後 20 位元組是 format block，它指向 sResource directory。

```
 sResource directory          ID → 偏移（每筆 4 位元組：ID 1 byte＋24 位元有號偏移）
   ├─ 0x01 board sResource
   │     sRsrcType   (1) = 類別 1 Board, cType 0, DrvrSW 0, DrvrHW 0
   │     sRsrcName   (2) = "68k_HAL Video Card"
   │     BoardId     (32)
   │     VendorInfo  (36) → VendorId、RevLevel、PartNum、Date
   └─ 0x80 video sResource
         sRsrcType   (1) = 類別 3 Display, cType 1 Video, DrvrSW 1 (Apple), DrvrHW 'hH'
         sRsrcName   (2) = "Display_Video_Apple_68kHAL"
         sRsrcDrvrDir(4) → sMacOS68020 (2) → DRVR（第 4 節）
         sRsrcHWDevId(8) = 1
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

- 模型每幀（60 Hz）讓卡的中斷成立（`cscSetInterrupt` 打開時），RBV 的 SIFR bit 0 變 0。
- 驅動的 Open 用 `_SIntInstall` 掛一個 slot 中斷處理程式（`SQElem`），它用模擬 opcode 清掉卡的中斷，再呼叫 `JVBLTask`（低記憶體 `0xD28`，D0 = slot 9）執行這個 slot 的 VBL task，回傳 D0 = 1。
- **未知 2**：IIci ROM 的 slot 中斷分派如何對應 SIFR 的位元（推測 bit 0–5 = slot `$9`–`$E`，Linux 的 `via.c` 用同樣的對應）。

## 5. 多解析度與 Display Manager

- 解析度清單（顯示模式 ID `0x80` 起）：640x480、800x600、1024x768、1152x870、1280x720、1280x960、1280x1024、1440x900、1600x1200、1920x1080、1920x1200、2560x1440、3840x2160。
- `cscGetNextResolution`：上一個 ID 是 `0xFFFFFFFE` 時回第一個；`0` 回目前的；最後一個之後回 `0xFFFFFFFD`。`cscGetVideoParameters`（ID＋色深 → VPBlock）讓 Display Manager 不必讀 ROM。`cscGetConnection` 回報 "modeless connection"（`kModelessConnect` = 8，所有模式都 valid/safe）。
- `cscSwitchMode`：原生端換模式、改 ROM 的 VPBlock 並重算 CRC；驅動接著呼叫 `_SUpdateSRT`。
- **未知 3**：System 7.5.3 的 Monitors 控制台是否會列出多個解析度。System 7.5 起有 Display Manager，但 7.5.3 的 Monitors 可能只有色深；如果 Monitors 不列，計畫的備案是 kernel 端的參數（`mac res=1280x720`）與 host 的 `mac_run` 腳本呼叫 `cscSwitchMode`（或 Display Manager 的 `DMSetDisplayMode`）。

## 6. 原生端與 kernel 的介面

```c
/* emu/mac/mac.h（摘要） */
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
- **內建視訊**：預設關閉——RBV 回報的螢幕感測線改成 7（「沒有接螢幕」，*Guide to the Macintosh Family Hardware*：感測線 111 = 沒有顯示器），IIci ROM 的內建視訊 primary init 看到沒有螢幕就不建它的 sResource（**未知 4**，P9 N1 驗證）。`mac_run -I`、kernel `mac builtin` 切回內建視訊（640x480），用來比較與除錯。
