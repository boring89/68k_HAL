# 原生 NVIDIA 模式設定的可行性研究（GTX 1070，只整理資料，不實作）

P9 N11 初稿（自動排程），2026-10-18 依使用者要求**查證並補強**（第 3、6、7 節是查證結果）。目的：使用者想要 2560x1440 **144 Hz**，以及 UEFI 開機後也能換實體解析度。這兩件事都只能由「顯示卡自己的模式設定程式」做（`docs/gpu.md` 第 8 節）。**沒有任何程式碼**，也沒有在實機上對顯示卡寫入任何東西。

## 1. 比喻：顯示卡裡的「放映機」

framebuffer 是一卷底片；GTX 1070 裡有一台**放映機**（display engine，NVIDIA 叫 EVO），它照著時刻表（像素時脈、水平與垂直的 total／blank／sync）一格一格把底片送進一條**纜線**（SOR：Serial Output Resource，可以輸出 DisplayPort、HDMI 或 DVI）。韌體（GOP 或 VBIOS）開機時已經把放映機設成 60 Hz 的時刻表；要換成 144 Hz，就得自己操作這台放映機，並且讓纜線兩端用新的速度重新協調。

```
 VRAM 裡的 framebuffer
     │  surface（位址、pitch、格式）
     ▼
   head 0..3（raster：寬高、blank、sync；像素時脈 → VPLL）
     │
     ▼
   SOR 0..3（DP / HDMI / DVI 編碼）── DDC（I2C：EDID、HDMI 的 SCDC）
                                   └─ DP AUX（DPCD、link training）── 螢幕
```

操作放映機的方法：在記憶體裡寫一串「命令」（method，例如「head 0 的 raster 寬度 = 2720」），放進**推送緩衝區**（push buffer），再告訴顯示引擎「讀到這裡」。引擎在套用設定的過程中會發三次 **supervisor 中斷**，要求驅動在中間設定時脈、SOR 等硬體（nouveau 的 `gf119_disp_super`）。

## 2. 使用者的實際硬體（2026-10-18 在 host 上唯讀查到）

| 項目 | 結果 | 來源 |
|---|---|---|
| 顯示卡 | GP104 [GeForce GTX 1070]，PCI `10de:1b81`，VBIOS `86.04.50.40.AD` | `lspci -nn`、`nvidia-smi` |
| 螢幕 | ASUS **XG27AQV**（27 吋，2560x1440） | GNOME Mutter `GetCurrentState` |
| 連接方式 | **HDMI**（`card1-HDMI-A-1` connected；三個 DP 與 DVI-D 沒接） | `/sys/class/drm` |
| Linux（NVIDIA 官方驅動）目前的模式 | **2560x1440 @ 143.678 Hz**；清單另有 120、60 Hz 與 3840x2160 @ 60 Hz | Mutter |

注意：Wayland 下 `xrandr --verbose` 顯示的時序（例如 806.75 MHz）是 XWayland 自己用 CVT 算出來的假模式，不是送給螢幕的實際時序；實際的像素時脈要等第 1 階段在實機讀 `0x640450`。

**這改變了初稿的假設**：初稿只考慮 DisplayPort。螢幕接 HDMI 時，要算出 1440p 在 HDMI 下多少 Hz 以上需要 HDMI 2.0 的額外功能：

```
HDMI 1.4 的 TMDS 上限 340 MHz
2560x1440 用 reduced blanking：htotal ≈ 2720、vtotal ≈ 1481 → 每格 ≈ 4.03 M 像素時脈
340 MHz / 4.03 M ≈ 84 Hz
```

所以 **1440p 超過約 84 Hz（120 與 144 Hz 都是）就要 HDMI 2.0 的 scrambling**：像素時脈約 580 MHz，TMDS 時脈比例要改成 1/40，並且經 DDC（I2C 位址 `0x54`）寫螢幕的 **SCDC** 暫存器告訴螢幕「已開啟 scrambling」。nouveau 有實作（`gm200_sor_hdmi_scdc`）。改用 DP 線則是 HBR2 × 4 lanes（17.28 Gbps 有效頻寬，大於 144 Hz 所需的約 14 Gbps），沒有 scrambling 的問題，但要做 link training。XG27AQV 是否有 DP 輸入，**要使用者看螢幕背面確認**。

## 3. 授權（逐檔查證）

查證的版本：kernel.org `torvalds/linux` commit `32c52ec87d62`（2026-10-17），只取 `drivers/gpu/drm/nouveau`（放在 `third_party/downloads/linux-nouveau/`，不進 git）。

| 檔案數（`.c`／`.h`，共 1212） | 授權 |
|---|---|
| 762 | MIT 全文（"Permission is hereby granted…"，沒有 SPDX 行） |
| 395 | `SPDX-License-Identifier: MIT` |
| 1 | `GPL-2.0 OR MIT`（可選 MIT） |
| 54 | **沒有任何授權聲明** |

- **沒有任何一個檔案是 GPL-2.0-only**。`docs/gpu.md` 原本寫「Nouveau 是 GPL-2.0-only」是錯的（已更正）。`MODULE_LICENSE("GPL and additional rights")` 只是 Linux 模組的標籤，表示「GPL 相容的寬鬆授權」。
- 沒有聲明的 54 個檔案依 Linux 的 `COPYING`（沒有標示的檔案預設 GPL-2.0），**一律當成 GPL-2.0-only，不可移植**。它們幾乎都是標頭檔：`include/nvif/*.h`、`dispnv50/*.h`、`nvkm/subdev/mmu/*.h`、`gsp.h`、`falcon.h` 等。需要的定義要依 MIT 的 `.c` 檔或 NVIDIA 文件自己寫。
- 需要的目錄全部是 MIT：`nvkm/engine/disp`、`nvkm/subdev/bios`、`i2c`、`instmem`、`devinit`、`clk`、`gpio`。

其他來源：

| 來源 | 內容 | 授權 | 能不能用 |
|---|---|---|---|
| NVIDIA open-gpu-doc（github.com/NVIDIA/open-gpu-doc） | 顯示類別 `cl917d.h`、`cl987d.h`、`cl907d.h`；**DCB 4.x 規格**、**BIT（BIOS Information Table）**、**Devinit 腳本 opcode**（`devinit.xml`）。已下載到 `third_party/downloads/nvidia-doc/` | MIT（`LICENSE.md`，NVIDIA 2019） | 可以 |
| open-gpu-doc 的暫存器手冊（`manuals/`） | 只有 Turing、Ampere 的 `dev_display`；**沒有 Pascal** | MIT | Pascal 的暫存器位址只能從 nouveau（MIT）與 envytools 取得 |
| envytools／rnndb | 逆向工程的暫存器資料庫 | MIT | 可以參考 |
| open-gpu-kernel-modules | 只支援 Turing 以後（GSP 韌體），不支援 Pascal | MIT／GPL | 對 GTX 1070 沒用 |
| Haiku | 沒有 NVIDIA 的模式設定 | MIT | 沒用 |

## 4. 韌體：顯示不需要 NVIDIA 簽章的韌體（查證）

- nouveau 給 GP104 的設定（`nvkm/engine/device/base.c` 的 `nv134_chipset`）：顯示引擎是 `gp102_disp`。`engine/disp/` 裡**沒有任何載入韌體的程式**。需要簽章韌體的是 `acr`、`sec2`、`pmu`、`gr`（3D、電源管理），模式設定都不碰。
- 像素時脈（VPLL）直接寫暫存器：`gf100_devinit_pll_set` 從 VBIOS 的 PLL 表讀範圍，`gt215_pll_calc` 算 N／M／P，寫 `info.reg + 0x04`、`+0x10`。
- devinit（開機初始化記憶體與時脈）在 GM200 以後要在 PMU 上執行，但 **GOP／VBIOS 開機時已經做過**（GPU 已經 "posted"），我們不需要再做。
- 這和實際經驗一致：nouveau 在 GTX 10 系列可以設定模式，只是不能調高 GPU 時脈（那需要簽章韌體，我們也用不到）。

## 5. 需要的東西（補上初稿漏掉的兩項）

| 項目 | 內容 | 難度 |
|---|---|---|
| 找到卡、映射暫存器 | BAR0 = 16 MiB 的暫存器、BAR1 = VRAM 視窗 | 容易（已有 PCI 與 `hal_pci_map_bar`） |
| 晶片辨識 | `PMC_BOOT_0`（BAR0 + 0） | 容易 |
| **VBIOS 取得與解析** | 從 PRAMIN（GOP 開機時 VBIOS 映像在 VRAM 裡）或 PCI ROM 讀出；解析 BIT、DCB（哪個 SOR 接哪個接頭）、PLL 表、DP／TMDS 表 | 中；有 NVIDIA 的 DCB 與 BIT 規格 |
| **VBIOS 腳本直譯器**（初稿漏掉） | supervisor 第 2、3 階段要執行 VBIOS 的 IED 腳本（`nv50.c` 的 OnInt2／OnInt3，依像素時脈選），DP link training 也要執行 DP 表的腳本。nouveau 的 `subdev/bios/init.c` 約 2350 行；opcode 定義有 NVIDIA 的 `devinit.xml` | 中到難；腳本會寫任意暫存器，錯了就沒畫面 |
| **顯示引擎的記憶體區**（初稿漏掉） | 64 KiB 的物件區與雜湊表（RAMHT）要放在 **VRAM**，位址寫進 `0x610010`。可以經 PRAMIN 視窗（BAR0 + `0x700000`，視窗基底寫 `0x001700`）寫入，不需要 GPU 的分頁表 | 中 |
| 推送緩衝區 | 可以放在系統記憶體（target 3 = HOST），用我們的 `hal_dma_alloc` | 容易 |
| 核心通道 | class `0x987D`。對照 NVIDIA 的標頭：`cl987d.h` 和 `cl917d.h` 的 method **只差能力通知區（capabilities notifier）**；nouveau 對 GP102 用的就是 `core917d.c` | 中 |
| supervisor 三階段 | `gf119_disp_super`：中斷 → 讀 armed／assembly 狀態 → 關舊輸出、設 VPLL、開新輸出 | 難（時序與順序要對） |
| 輸出：HDMI（使用者目前的接法） | SOR 的 TMDS 設定；> 340 MHz 要 scrambling 與 SCDC（第 2 節） | 中 |
| 輸出：DP（若改接 DP） | AUX 讀 DPCD、link training（電壓與預強調調整） | 難 |

## 6. 第 1 階段（只讀）要讀的暫存器

來源：nouveau `engine/disp/gf119.c`（MIT），**沒有在實機驗證**。armed（目前正在輸出的）狀態在 `0x640000`，assembly（尚未套用的）在 `+0x20000`。

| 位址 | 內容 |
|---|---|
| `0x000000` | `PMC_BOOT_0`：晶片 ID（GP104） |
| `0x022448` | head 的數量 |
| `0x612004` | bit 0–3：存在的 head；bit 8–15：存在的 SOR |
| `0x640414 + head × 0x300` | 高 16 位元 vtotal、低 16 位元 htotal |
| `0x640418 + head × 0x300` | vsync end、hsync end |
| `0x64041c + head × 0x300` | vblank end、hblank end |
| `0x640420 + head × 0x300` | vblank start、hblank start |
| `0x640450 + head × 0x300` | **像素時脈（Hz）** → 更新率 = 時脈 /（htotal × vtotal） |
| `0x640404 + head × 0x300` | bit 6–9：色深（5 = 24 位元） |
| `0x640200 + sor × 0x20` | bit 8–11：輸出協定（1／2／5 = TMDS，8／9 = DP） |

成功的話，開機後可以印出「GP104，head 0：2560x1440，像素時脈 X MHz = 60.0 Hz，SOR n：TMDS」，同時證明 MMIO 映射與 nouveau 的位址在這張卡上正確。

## 7. 份量估計（修正）

只算需要的部分：顯示引擎（`gp102.c` 200、`gf119.c` 1261、`nv50.c` 1795、`gk104.c`、`gm200.c`、`gp100.c`、`head.c`、`ior.c`、`outp.c`、`chan.c`、`base.c` 約 4400 行）、`dp.c` 675、VBIOS（`init.c` 2347、`dcb.c`、`conn.c`、`dp.c`、`pll.c`、`disp.c`、`base.c`、shadow 約 1900 行）、AUX／I2C、PLL 計算、PRAMIN、RAMHT。nouveau 原始約 1.1 萬行，依賴 nvkm 的物件模型。在我們的 kernel 寫「只支援 GP104、一個 head、HDMI（或 DP）」的最小版本，估計 **5000–8000 行**（初稿的 3000–6000 沒算 VBIOS 腳本直譯器）。QEMU 沒有 NVIDIA 卡，**每一步都只能在 P520 實機驗證**。

## 8. 建議的分階段（每階段實機驗證後才做下一階段）

1. **只讀**：映射 BAR0，讀第 6 節的暫存器並印出。完全不寫入。
2. **讀 EDID 與（DP 時）DPCD**：經 SOR 的 I2C／AUX，只讀。HDMI 接法可以順便讀 SCDC 的版本暫存器。
3. **VBIOS**：讀出 VBIOS 映像並解析 BIT、DCB、PLL 表（只讀；可以先在 host 用 `tests/host` 對 VBIOS 檔案測試——檔案由使用者自己在實機上存，**不進 repo**）。
4. **接管顯示引擎、只換 surface**：建立物件區、RAMHT、核心通道，讓引擎讀我們配置的 framebuffer，時序不變。失敗時重開機即可回復。
5. **換時序（2560x1440 60 → 144 Hz）**：VPLL、VBIOS 腳本、HDMI scrambling + SCDC（或 DP link training）。風險最高：螢幕沒訊號時要自動回復（例如 10 秒內沒按鍵就回到原本的時序）。
6. **換解析度**：接到 `hal_framebuffer_set_mode`，UEFI 開機後也能換。

安全性：寫錯暫存器不會損壞硬體或螢幕（最壞是沒畫面或 GPU 卡住，重開機就恢復），而且我們不碰電壓、風扇、GPU 時脈。

## 9. 結論與待決定

- **技術上可行，授權上可行**：nouveau 需要的部分全是 MIT，顯示不需要簽章韌體，NVIDIA 公開了 DCB、BIT、devinit 腳本與顯示類別的文件。難處是份量（5000–8000 行）與只能在實機上驗證。
- 你的螢幕在 Linux 下已經用 HDMI 跑 143.678 Hz，所以硬體確定能達到 144 Hz；我們要多做 HDMI 2.0 的 scrambling。
- 待使用者決定：
  1. 是否開始第 1 階段（只讀，無風險）。
  2. 144 Hz 走 **HDMI**（維持現在的接法，多做 scrambling／SCDC）還是改接 **DP**（多做 link training；先確認 XG27AQV 有沒有 DP 輸入）。
  3. 「移植 nouveau 的 MIT 檔案並保留聲明」還是「依 NVIDIA 文件與 nouveau 參考自己寫」。前者快、程式碼風格不同；後者符合本專案風格但慢。
  4. 能接受的實機測試次數（每階段至少一次重開機）。
