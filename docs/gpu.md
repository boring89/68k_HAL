# 顯示：Bochs VBE（BGA）、Mac 畫面放大、Intel 原生顯示卡設計

`drivers/bga/`、`platform/pc/video/`、`hal/framebuffer.h`、`kernel/display/fb_scale.c`

## 1. 目前的顯示路徑

比喻：螢幕像一面「由記憶體決定顏色的燈牆」。framebuffer 就是這塊記憶體：每個像素 4 個位元組（0x00RRGGBB），寫進去畫面就變。顯示卡負責「照著這塊記憶體掃描輸出」；**模式設定（mode setting）** 就是告訴顯示卡「燈牆是 1280×960、每像素 32 位元、記憶體在這裡」。

兩種來源：

| 來源 | 怎麼來 | 解析度 |
|---|---|---|
| GRUB（`kernel-gfx.elf`） | Multiboot header 要求 640×480×32，GRUB 用 BIOS 的 VBE 設好，把 framebuffer 位址放在 Multiboot info | 固定 640×480 |
| 自己設定（**新**） | `hal_framebuffer_set_mode(w, h)` → `drivers/bga` | 任意（QEMU 標準 VGA 上限約 2560×1600） |

兩種方式都可以：文字模式的 `kernel.elf`（沒有 framebuffer）也能用 `video 1280 960` 或 `mac 2` 從 VGA 文字模式直接切到圖形模式（文字終端之後只輸出到 serial；圖形模式開機的 `kernel-gfx.elf` 有 framebuffer 文字終端，見 1.5）。

## 1.5 圖形模式的文字終端（`kernel/display/fbcon.c`）

沒有 VGA 文字緩衝區的機器（UEFI 開機、或 GRUB 的 graphics 項目）要看到 shell，就得自己把字畫在 framebuffer 上。`fbcon` 實作 `text_display_t`（和 VGA 文字模式同一個介面），所以 terminal、ANSI、使用者程式（kilo）都不用改。

```
 一個格子 8x16 像素                      640x480 螢幕
 字型 8x8：每列 1 個位元組（bit0 = 最左）   ┌────────────────────────────┐ 色條 16 列
 每一列畫兩次 → 8x16                        ├────────────────────────────┤
 前景/背景 = VGA 16 色調色盤                 │ HAL> cpuinfo               │ 80 欄 x 29 列
 游標 = 格子底部兩列的底線                    │ ...                        │
                                            └────────────────────────────┘
```

- 每個格子在記憶體裡也存一份（字元、前景、背景）：捲動時只要「讀影子、重畫」，不用讀 framebuffer（真實硬體上 framebuffer 讀取很慢）。`copy_cell` 就是這樣做的。
- 字型：IBM PC BIOS 的 8x8 字型（公有領域，由 Daniel Hepper 整理的 font8x8_basic.h，原始出處 Marcel Sondaar／IBM），只有 ASCII 0–127；其他字元顯示為空白。檔案是 `kernel/display/fbcon_font.c`（附出處說明，直接放在 repo 裡，建置不需要下載）。
- `mac` 結束後（可能換過模式）重新 `fbcon_init` 再 `terminal_init`，畫面接回終端。
- 限制：每個格子逐像素寫入，捲動一次約 29x80 個格子 x 128 像素（QEMU 上沒問題；真實硬體的 write-combining 之後再說）；沒有 UTF-8、沒有 CP437 的線條字元；`video` 指令（文字模式核心切到圖形）仍然畫整面測試圖樣、之後 shell 看不到（可用 `mac` 或重開機）。
- 測試：`tests/host/test_fbcon.c`（在記憶體中的假 framebuffer 檢查像素）；`make test-fb`（QEMU：色條、提示字元、打 `cpuinfo` 看輸出）。

## 2. BGA 驅動（`drivers/bga/bga.c`）

QEMU 的標準 VGA（`-vga std`，PCI 1234:1111）是 Bochs 的 VBE 擴充卡。它不需要 BIOS 呼叫，用兩個 I/O port 設定「DISPI 暫存器」（參考：Bochs VBE Extensions、OSDev Wiki "Bochs VBE Extensions"）：

```
 outw(0x1CE, 暫存器編號);  outw(0x1CF, 值);        （16 位元）

 0 ID        0xB0C0–0xB0CF：確認是 BGA
 1 XRES      寬        2 YRES  高        3 BPP  每像素位元數（32）
 4 ENABLE    bit 0 啟用；bit 1 GETCAPS（讀 XRES/YRES/BPP 變成「最大值」）；bit 6 線性 framebuffer
 6 VIRT_WIDTH 虛擬寬度（= XRES，表示每列 XRES×4 位元組）
```

設定模式：`ENABLE = 0` → 寫 XRES、YRES、BPP、VIRT_WIDTH → `ENABLE = 0x01 | 0x40`（啟用＋線性 framebuffer）→ 讀回 XRES/YRES 確認。framebuffer 的實體位址是 PCI BAR 0（16 MiB）；`hal_framebuffer_set_mode` 用 `vmap` 映射它（一般可快取記憶體；真實硬體上若要追求速度與一致性，之後可改用 PAT 的 write-combining）。

限制：每次切換模式都會再映射一次（`vmap` 不歸還），不要在迴圈中切換；只有 32 bpp；沒有硬體加速（所有繪圖都是 CPU 寫像素）。

## 3. Mac 畫面放大（HiDPI）

Mac IIci 的畫面是 640×480。在現代螢幕上很小，所以 `mac 2` 先把顯示模式切成 1280×960，再把每個 Mac 像素畫成 2×2 的方塊（`kernel/display/fb_scale.c`；整數倍放大、不模糊，點陣字型與圖示保持銳利）：

```
 Mac 輸出 640x480   ─────►  fb_blit_scaled(scale=2)  ─────►  framebuffer 1280x960
  (0x00RRGGBB)                 每個像素 → 2x2 方塊
```

- `mac [N]`：N = 1（預設，不改模式）、2、3、4；0 = 在目前模式下盡量放大（`fb_best_scale`）。
- 若切不了模式（沒有 BGA）就用目前模式能容納的最大倍率；螢幕比圖小時只顯示左上角。
- 驗證：`make mac-screenshot MAC_SHOT_SCALE=2`（本機，需要 ROM）→ `build/mac/qemu_screen.ppm` 是 1280×960，桌面清楚放大。
- 自動測試（`make test-video`，在 `make test` 中）：開機 ISO、輸入 `video 1280 960`、QEMU `screendump` 必須是 1280×960 並顯示 8 條測試色條。
- host 測試：`tests/host/test_fb_scale.c`。

## 4. Intel 原生顯示卡（只有設計，沒有實作）

**為什麼不做**：QEMU 沒有模擬 Intel 的內顯（i915），所以無法在這裡驗證；沒有驗證的顯示卡驅動風險很高（設錯時序可能讓面板沒畫面，嚴重時損壞硬體的可能性雖低但真實存在）。這份是之後有實機時的路線圖。

**參考來源**：Haiku 的 `intel_extreme` 驅動（MIT 授權；本專案是 GPL-3.0-or-later，可以移植並保留聲明，但不可引入 GPL-2.0-only 的程式碼，例如 Linux 的 i915）。原始碼已淺層下載到 `third_party/downloads/haiku/`（約 5 MB，不進 git）：

| 檔案 | 內容 |
|---|---|
| `headers/private/graphics/intel_extreme/intel_extreme.h` | 所有暫存器位址與位元定義（約 2000 行），按世代分（915/945/965/G4x/ILK/SNB/IVB/HSW/…/TGL） |
| `src/add-ons/kernel/drivers/graphics/intel_extreme/` | 核心端：找裝置（PCI vendor 0x8086、class 3）、映射暫存器與 GTT、讀 VBT（BIOS 的面板資訊）、電源管理 |
| `src/add-ons/accelerants/intel_extreme/mode.cpp` | 模式設定的主流程（`intel_set_display_mode`） |
| `…/pll.cpp`、`TigerLakePLL.cpp` | 時脈（PLL）計算：像素時脈 = 解析度 × 更新率 × 時序 |
| `…/Pipes.cpp`、`Ports.cpp`、`FlexibleDisplayInterface.cpp`、`PanelFitter.cpp` | pipe（時序產生）、port（類比 VGA／LVDS／HDMI／DP／eDP）、FDI（舊平台 CPU 與 PCH 之間的連結）、面板縮放 |

**硬體概念**：

```
 framebuffer（GTT 映射的記憶體）
     │  plane（讀取記憶體，格式、stride、位址）
     ▼
   pipe A/B/C（產生水平／垂直時序：total、blank、sync）  ← PLL 提供像素時脈
     │  （舊平台：FDI → PCH 的 transcoder）
     ▼
   port / encoder（類比 VGA、HDMI、DP …）→ 螢幕
```

**分階段的實作計畫**（每階段在實機上驗證，前一階段成功才做下一階段）：

1. **辨識與映射**：PCI 掃描 class 3 的 8086:xxxx，依 device ID 查世代；BAR 0 = 暫存器（MMIO，用 `hal_pci_map_bar`，不可快取）；BAR 2 = 孔徑（aperture）；GTT 在 MMIO 內或 BAR；讀 BIOS 已設好的模式（`INTEL_DISPLAY_A_CONTROL`、`…_BASE`、`…_BYTES_PER_ROW`、pipe size 暫存器）。到這一步就能**沿用 BIOS 設定好的模式畫畫**（等於 GRUB 的 framebuffer，但自己知道位址、stride）。
2. **讀 EDID**：用 GMBUS（Intel 的 I²C 控制器）或 bit-bang 的 DDC 讀螢幕的 EDID（128 位元組），得到支援的時序與偏好解析度。
3. **模式設定**：關閉 plane、pipe、port；計算並寫入 PLL（`pll.cpp`）；寫 pipe 的 H/V total、blank、sync 與 pipe size；開 pipe；設定 plane（像素格式、stride、surface 位址）並啟用；開 port。順序與等待（等 vblank、等 PLL 鎖定）不可省略。
4. **記憶體**：GTT 項目把實體頁面映射到 GPU 位址；stolen memory 與配置 framebuffer 的規則依世代而異。
5. **其他**：背光（`intel_set_brightness`）、DPMS 電源、游標平面、多螢幕。

世代差異極大（Haiku 單 `pll.cpp` 就 1000 行、`Ports.cpp` 2700 行），建議先只支援一個世代（例如手邊那台機器的世代），並先做階段 1。

## 5. 不做的事

- **NVIDIA**：需要專有韌體與未公開的文件，不做。
- 3D 加速、GPU 命令佇列（`engine.cpp`）：不做；畫面全由 CPU 寫像素。
