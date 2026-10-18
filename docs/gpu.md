# 顯示：Bochs VBE（BGA）、VBE（video BIOS）、Mac 畫面放大、Intel 原生顯示卡設計

`drivers/bga/`、`platform/pc/video/`（含 `vbe.c`）、`hal/framebuffer.h`、`kernel/display/fb_scale.c`

> P9（自動排程，**使用者尚未審核**）：第 3 節的 Mac 放大改為 0.5 倍步進並跟著 Mac 的虛擬 NuBus 顯示卡（`docs/nubus_video.md`）；新增第 6 節（VBE 執行期切換）、第 7 節（開機時的原生解析度）、第 8 節（為什麼沒有 60 Hz 以上）。

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

> P9 起：Mac 的畫面是虛擬 NuBus 顯示卡，640x480 到 3840x2160（`docs/nubus_video.md`）。放大以 0.5 倍為單位（`fb_blit_halves`：1x、1.5x、2x… 最近鄰，1.5x 時每兩個 Mac 像素有一個畫成兩倍寬／高），**不再切換螢幕模式**；`mac scale=1|1.5|2|…|auto`，執行中 Ctrl+Alt+PgUp/PgDn/Home。以下是 P6 的原始說明（640x480、整數倍、`mac 2` 會切模式），保留作為歷史。

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

## 4.5 實機（P7 R6）：用韌體給的 framebuffer，不寫 NVIDIA 驅動

P520 的顯示卡是 GeForce GTX 1070（NVIDIA GP104，PCI 10de:1b81）。**不寫它的驅動**（GPU 初始化與模式設定的份量很大，而且只能在實機驗證）。P9 更正：nouveau 的顯示引擎程式（例如 `nvkm/engine/disp/gp102.c`）其實是 **MIT** 授權，NVIDIA 的 open-gpu-doc 也有 MIT 的顯示類別標頭；可行性與分階段計畫見 `docs/nvidia_research.md`。我們只用**開機時韌體已經設定好的 framebuffer**：

```
UEFI：韌體的 GOP（Graphics Output Protocol）已經設好模式（通常是螢幕的原生解析度，例如 1920x1080）
      GRUB 的 efi_gop 驅動沿用它 → Multiboot2 的 framebuffer tag → kernel（platform/pc/boot/multiboot.c）
CSM（傳統 BIOS）：顯示卡的 VBIOS 提供 VBE 模式，GRUB 用 Multiboot 1 標頭要求 640x480x32（kernel-gfx.elf）
```

- 同一個 `kernel-gfx.elf` 有兩個標頭：Multiboot 1 要 640x480x32（BIOS 測試與舊行為不變），Multiboot2 的 framebuffer tag 寫 0x0x32＝**沒有偏好**，GRUB 就保留韌體選的模式。（試過 `gfxpayload`、`gfxmode`＋`gfxterm`：GRUB 對 Multiboot2 不理會它們；`all_video` 還會讓 Bochs/Cirrus 驅動搶走並設成 800x600x24，所以 UEFI 項目只載入 `efi_gop`。）
- `kernel-gfx1080.elf`（只給測試用）的 Multiboot2 標頭要 1920x1080x32。
- 像素格式：所有繪圖程式都假設 `0x00RRGGBB`（藍在低位元組，UEFI 的 BGRX，GOP 最常見的格式）。`multiboot_framebuffer_usable` 檢查 loader 回報的顏色位置（red 16/8、green 8/8、blue 0/8），不符就**不使用** framebuffer（只剩序列埠，實機看不到畫面）。開機訊息會印 `Loader framebuffer: …` 一行（位址、大小、pitch、bpp、顏色位置、是否可用），實機畫面全黑或沒有畫面時請拍下它。
- 文字大小：`fbcon_scale_for`：每 960 像素寬放大一倍（1920x1080：2 倍，字格 16x32，120x33 格；4K：4 倍），所以大螢幕上字不會太小。Mac 畫面本來就用 `fb_best_scale` 置中並用最大的整數倍放大（`mac` 指令的預設現在是「自動」，`mac 2` 仍是指定兩倍）；1920x1080 下是 2 倍（1280x960，置中）。
- BGA 驅動只在 PCI 1234:1111 且 VBE ID 暫存器合理時啟用，GTX 1070 不會碰到它。
- 測試：`make test-uefi`（OVMF：GRUB 保留韌體模式 1280x800 → 字 1 倍；暫時的 ISO 要求 1920x1080 → 字 2 倍、120x33 格，並檢查螢幕截圖的色條與文字像素）；Mac 在 UEFI 1080p 的畫面是本機手動檢查（需要 ROM）。
- **未在實機驗證**：GTX 1070 的 GOP 模式清單、像素格式（BGRX 是假設）、HiDPI 螢幕是否真的給 1920x1080 以上。

## 6. VBE 執行期切換解析度（P9 N6，`platform/pc/video/vbe.c`）

比喻：顯示卡自己帶了一本「說明書兼遙控器」——video BIOS（卡上的 ROM，PC 開機時被複製到 `0xC0000`）。裡面的 VBE（VESA BIOS Extensions）函式會幫你把卡切到任何它支援的模式，不必知道卡的暫存器。問題是這本說明書只能在 16 位元「真實模式」執行，而我們的 kernel 在 64 位元模式。

做法：**在 kernel 裡模擬一顆 8086/386 來執行它**（x86emu，X.org 用了十幾年的同一個模擬器，MIT/X11 授權，原封不動放在 `third_party/x86emu/`，來源與 SHA-256 見那裡的 README）。

```
 shell: video 1920 1080 ─► hal_framebuffer_set_mode ─► BGA？（QEMU 的 Bochs 介面；有 nobga 就跳過）
                                                     └► vbe_switch ─► x86emu 執行 int 0x10
                                                                       ├ 記憶體：前 1 MiB 的私人複本
                                                                       │  （IVT、BIOS 資料區、0xC0000 video BIOS、0xF0000 系統 BIOS）
                                                                       └ I/O port：真的硬體（video BIOS 就是靠它設定顯示卡）
```

| 呼叫 | 用途 |
|---|---|
| `4F00h` | 控制器資訊：簽名 "VESA"、版本、模式清單的位址（`vbe_parse_info`） |
| `4F01h` | 每個模式的資訊：寬、高、bpp、記憶體模型、線性 framebuffer 的實體位址、每列位元組（VBE 3.0 有線性模式專用的 pitch 與顏色遮罩；`vbe_parse_mode_info`） |
| `4F02h` | 設定模式，BX bit 14 = 線性 framebuffer |
| `4F15h` | VBE/DDC：讀螢幕的 EDID（128 位元組；第一個 detailed timing = 原生解析度，`edid_preferred`） |

- 只用「32 位元、直接色（memory model 6）、0x00RRGGBB 的遮罩、有線性 framebuffer」的模式（`vbe_mode_usable`）。
- 呼叫方式：在私人記憶體 `0000:0700` 放一個 `HLT`，堆疊上推入 FLAGS、CS=0、IP=0x700（就像 CPU 執行了 `INT 10h`），從中斷向量表的 `int 0x10` 開始執行，video BIOS 的 `IRET` 回到 HLT 就結束。超過 5000 萬次指令讀取視為卡住。
- **UEFI 開機沒有 video BIOS**（`0xC0000` 沒有 `55 AA`，或 `int 0x10` 不指向 ROM）：全部回報「不支援」，`video list` 印 `no video BIOS …`。UEFI 下的解析度由開機時決定（第 7 節）。
- 32 位元 kernel 的實體頁 0 沒有映射（null 保護），所以前 1 MiB 透過 `vmap` 讀（只在第一次呼叫時複製一次）。
- shell：`video list`（VBE 模式）、`video edid`（原生解析度）、`video native`（切到原生）、`video W H`。
- 測試：`tests/host/test_vbe_parse.c`（解析）；`make test-vbe`（在 `make test` 中）：QEMU（SeaBIOS 與它的 VGA BIOS）、kernel 命令列 `nobga`，`video list`、`video edid`、切到 800x600 與 1920x1080，從 screendump 檢查大小與色條；`make test-uefi` 檢查 UEFI 下回報不支援。32／64 位元都通過。
- **未在實機驗證**：GTX 1070 的 VBIOS 在 x86emu 中是否正常（X.org 以前用同一個模擬器跑 NVIDIA 的 VBIOS，但沒有保證）；VBE 模式清單通常不含 2560x1440（VESA 標準模式只到 1600x1200，廠商可加），這要實機的 `video list` 才知道。

## 7. 開機時就用原生解析度（P9 N5）

- **UEFI**：GRUB 對 Multiboot2 只看 kernel header 的 framebuffer tag（`gfxpayload` 無效，已在 OVMF 試過）。所以有三個 kernel：`kernel-gfx1440.elf`（要求 2560x1440，USB 映像的預設）、`kernel-gfx1080.elf`（選單項目「UEFI: 1920x1080」）、`kernel-gfx.elf`（沒有要求＝保留韌體的模式）。GOP 沒有那個模式時 GRUB 保留韌體的模式。
- **BIOS／CSM**：Multiboot 1 header 現在也要求同樣的大小（`GFX_WIDTH`；沒有定義時仍是 640x480），GRUB 透過 VBE 設定；USB 映像的「Macintosh」項目在 BIOS 下也用 `kernel-gfx1440.elf`。之後還可以在 shell 用 `video native`（EDID）或 `video W H` 換。
- 測試：`make test-usb-image` 檢查 BIOS 與 UEFI 的「mac」項目都是 2560x1440、UEFI 預設 2560x1440、「UEFI: 1920x1080」、「保留韌體解析度」1280x800。

## 8. 為什麼沒有 60 Hz 以上（以及 UEFI 下開機後不能換解析度）

更新率（每秒畫幾次）是**顯示卡送給螢幕的時序**決定的：像素時脈、水平與垂直的 total／blank／sync。這些只有顯示卡的模式設定程式碼能改（第 4 節的 pipe 與 PLL）。我們用的 framebuffer 是韌體設定好的：

- UEFI 的 GOP 只提供「解析度清單」，沒有更新率可選，而且**只能在開機服務（boot services）期間**呼叫；GRUB 把控制交給 kernel 後就不能再用。GOP 選的時序幾乎都是 60 Hz（EDID 的第一個 detailed timing 或標準模式）。
- VBE 3.0 的 `4F02h` 可以帶 CRTC 資訊區塊指定時序（refresh rate control），但很多 VBIOS 不支援，而且只到 BIOS／CSM 開機才有。
- GTX 1070 要 144 Hz、或 UEFI 開機後換解析度，就需要原生 NVIDIA 模式設定（PLL、display engine、HDMI/DP 連結訓練），沒有公開文件、Nouveau 是 GPL-2.0-only，不在範圍內（使用者已同意）。
- Mac 自己的時間軸是 60.15 Hz（VIA 的 Ticks），所以 Mac 的畫面更新本來就是 60 次／秒；kernel 每個 Mac frame 畫一次有變動的列。

## 5. 不做的事

- **NVIDIA**：目前不做；可行性研究（資料來源、授權、分階段）見 `docs/nvidia_research.md`。
- 3D 加速、GPU 命令佇列（`engine.cpp`）：不做；畫面全由 CPU 寫像素。
