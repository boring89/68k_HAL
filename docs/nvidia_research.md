# 原生 NVIDIA 模式設定的可行性研究（GTX 1070，只整理資料，不實作）

P9 N11（自動排程，**使用者尚未審核**）。目的：使用者想要 2560x1440 **144 Hz**，以及 UEFI 開機後也能換實體解析度。這兩件事都只能由「顯示卡自己的模式設定程式」做（`docs/gpu.md` 第 8 節）。這份文件整理：需要什麼、資料在哪裡、授權能不能用、風險與分階段的做法。**沒有任何程式碼**，也沒有在實機上試過任何東西。

## 1. 比喻：顯示卡裡的「放映機」

framebuffer 是一卷底片；GTX 1070 裡有一台**放映機**（display engine，NVIDIA 叫 EVO／NVDisplay），它照著時刻表（像素時脈、水平與垂直的 total／blank／sync）一格一格把底片送進一條**纜線**（SOR：Serial Output Resource，接 DisplayPort 或 HDMI）。韌體（GOP 或 VBIOS）開機時已經把放映機設成 60 Hz 的時刻表；我們要換成 144 Hz，就得自己去操作這台放映機，而且 DisplayPort 的纜線兩端要重新「握手」（link training）協調速度。

```
 VRAM 裡的 framebuffer
     │  surface（位址、pitch、格式）
     ▼
   head 0..3（raster：寬高、blank、sync；像素時脈 → VPLL）
     │
     ▼
   SOR 0..3（DP / HDMI / DVI 編碼）── DP AUX 通道（讀 EDID、DPCD、link training）── 螢幕
```

## 2. 需要的東西

| 項目 | 內容 | 為什麼難 |
|---|---|---|
| 找到卡、映射暫存器 | PCI 10de:1b81；BAR0 = 16 MiB 的暫存器（MMIO）、BAR1 = VRAM 視窗 | 容易（我們已有 PCI 與 `hal_pci_map_bar`，P8 也支援 4 GiB 以上的 BAR） |
| 晶片辨識 | `PMC_BOOT_0`（BAR0 + 0）讀出架構與實作（GP104） | 容易 |
| 顯示引擎的「核心通道」 | Pascal（GP102 以後）的 core channel class **`0x987D`**：用 DMA 推送方法（method）設定 head 的 raster、surface、輸出資源，再送 `UPDATE` | 要先建立推送緩衝區（push buffer）與 DMA 物件（context DMA）、處理 "supervisor" 中斷的三階段交握（nouveau 的 `nv50_disp_super_*`） |
| 像素時脈 | head 的 VPLL；Pascal 的時脈設定透過顯示引擎與 VBIOS 表 | 數值錯誤時螢幕無訊號（不會壞硬體，但要能回復） |
| DisplayPort | AUX 通道讀 DPCD、link training（HBR2 5.4 Gbps × 4 lanes 足夠 2560x1440@144 Hz 的約 14 Gbps） | 協調失敗就沒畫面；要依 DPCD 調整電壓與預強調 |
| VBIOS 表 | DCB（哪個 SOR 接哪個接頭）、init scripts（devinit）、時脈表 | 格式要逐一解析；nouveau 的 `nvkm/subdev/bios` 有完整解析器 |
| 韌體 | 顯示引擎在 Pascal **不需要** NVIDIA 簽章的韌體（nouveau 在 GP10x 能做模式設定；需要簽章韌體的是加速引擎與 PMU 管理） | 只要不碰 3D／電源管理就沒有這個問題（**需要實機確認**） |

## 3. 資料來源與授權

| 來源 | 內容 | 授權 | 能不能用 |
|---|---|---|---|
| NVIDIA open-gpu-doc（github.com/NVIDIA/open-gpu-doc；download.nvidia.com/open-gpu-doc/Display-Class-Methods/2/） | 顯示類別的方法定義，例如 `cl987d.h`（`NV987D_CORE_CHANNEL_DMA`，已下載到 `third_party/downloads/nvidia-doc/`，不進 git） | MIT | 可以（保留聲明） |
| Linux nouveau（`drivers/gpu/drm/nouveau/nvkm/engine/disp/`，例如 `gp102.c`） | 實際的顯示引擎程式：通道、supervisor、SOR、DP link training；`nvkm/subdev/bios` 的 VBIOS 解析 | **檔案開頭是 MIT（"Copyright 2016 Red Hat Inc. Permission is hereby granted…"）**。`docs/gpu.md` 先前寫「Nouveau 是 GPL-2.0-only」是錯的（只有部分 Linux DRM 膠水檔案是 GPL）；**移植前要逐檔確認** | 逐檔確認為 MIT 的可以移植並保留聲明；GPL-2.0-only 的檔案不可 |
| envytools／rnndb | 暫存器資料庫（逆向工程） | MIT | 可以參考 |
| open-gpu-kernel-modules（NVIDIA 官方開源 kernel 模組） | 只支援 Turing 以後（需要 GSP 韌體），**不支援 Pascal** | MIT／GPL 雙授權 | 對 GTX 1070 沒用 |
| Haiku | 只有 Intel／Radeon 的原生驅動，沒有 NVIDIA 的模式設定 | MIT | 對 GTX 1070 沒用 |

## 4. 份量估計

nouveau 的相關程式（只算顯示：disp 引擎、DP、VBIOS 解析、I2C／AUX、時脈）大約 2 到 3 萬行 C，而且依賴 nvkm 的物件模型（subdev、engine、object、event）。在我們的 kernel 裡重寫「只支援 GP104、只支援 DP、只有一個 head」的最小版本，估計 3000–6000 行，需要多次實機測試（QEMU 沒有 NVIDIA 卡，**無法在這裡驗證任何一步**）。

## 5. 建議的分階段（每階段都要實機驗證後才做下一階段）

1. **只讀**：映射 BAR0，讀 `PMC_BOOT_0` 確認 GP104；讀出 GOP 設好的 head 狀態（raster 大小、surface 位址與 pitch）並印出。完全不寫入，最安全。成功的話，`lspci -v` 可以顯示「NVIDIA GP104，目前 2560x1440」。
2. **AUX／I2C 讀 EDID 與 DPCD**：只讀，得到螢幕支援的所有時序（包含 144 Hz 的 detailed timing）與 DP 的 link 能力。
3. **換 surface（不換時序）**：讓顯示引擎讀我們自己配置的 framebuffer（例如 VRAM 裡的另一塊），驗證推送緩衝區與 core channel 能用。失敗時重開機即可回復。
4. **換時序（同一解析度，60 → 144 Hz）**：VPLL、raster、DP link 重新訓練。這是風險最高的一步：螢幕沒訊號時要有自動回復（例如 10 秒內沒按鍵就回到 GOP 的設定）。
5. **換解析度**：再加上 surface 大小與 pitch，接到 `hal_framebuffer_set_mode`。

## 6. 結論與待決定

- **技術上可行，授權上可行**（open-gpu-doc 與 nouveau 的 MIT 檔案），但份量大、而且每一步都只能在使用者的實機上驗證；建議先做第 1、2 階段（只讀、無風險），由實機結果決定要不要繼續。
- 待使用者決定：是否開始第 1 階段；能接受的實機測試次數；是否接受「移植 nouveau 的 MIT 檔案」這條路（而不是從 open-gpu-doc 自己寫）。
