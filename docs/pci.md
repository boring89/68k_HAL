# PCI 與驅動框架

`hal/pci.h`、`hal/dma.h`、`platform/pc/pci/`、`platform/pc/dma/`、`platform/pc/vmap/`、`kernel/pci/pci_names.c`

## 1. PCI 是什麼

比喻：PCI 是主機板上的「插座街」。每個插座（device）住著一個裝置（網卡、顯示卡…）；每個裝置有一張「名片」（設定空間，256 位元組）寫著它是誰、需要什麼資源。開機時韌體已經幫大家分配好地址，作業系統只要讀名片、照著用。

```
 bus 0 ── [00:00.0 host bridge] [00:01.0 ISA bridge] [00:01.1 IDE] [00:02.0 VGA] [00:03.0 e1000]…
   │        每個格子 = bus:device.function（device 0–31，function 0–7）
   └─ PCI-to-PCI bridge ── bus 1 ── …
```

名片（設定空間，PCI Local Bus Specification 3.0 第 6 章）重要欄位：

| 偏移 | 內容 |
|---|---|
| 0x00 | vendor ID、device ID（誰做的、什麼型號）；vendor = 0xFFFF 表示這格沒東西 |
| 0x04 | command：bit 0 回應 I/O、bit 1 回應記憶體、bit 2 可當 bus master（自己做 DMA） |
| 0x08 | revision、prog IF、subclass、class（例如 class 2 = 網路，3 = 顯示，4 = 多媒體） |
| 0x0E | header type（bit 7 = 多功能裝置；0 = 一般，1 = PCI 橋） |
| 0x10–0x27 | 六個 BAR（base address register）：裝置的暫存器在哪裡 |
| 0x3C | interrupt line（韌體選的 8259 IRQ 線）、interrupt pin（INTA–INTD） |

**存取方式**（mechanism #1，OSDev Wiki "PCI"）：把 `0x80000000 | bus<<16 | device<<11 | function<<8 | 暫存器` 寫進 port 0xCF8，再從 port 0xCFC 讀／寫那個 32 位元暫存器。

**BAR**：低位元說明種類（bit 0 = 1 → I/O port；bit 0 = 0 → 記憶體，bits 2:1 = 0 為 32 位元、2 為 64 位元，bit 3 = 可預取）。大小的量法：先關閉裝置的解碼，寫入全 1，讀回來：維持為 0 的位元就是裝置有解碼的位址位元，`大小 = ~(讀回值的位址位元) + 1`，再把原值寫回（`pci_bar.c`，純函式，host 測試涵蓋 I/O、32/64 位元、預取）。

## 2. 我們怎麼做

- `platform/pc/pci/hal_pci.c`：`pci_init()` 從 bus 0 開始掃，遇到 PCI 橋就跟進它的 secondary bus；每個存在的 function 記進一個靜態陣列（最多 64 個）。header type 0 的裝置會量好六個 BAR。
- `lspci`（shell）：`00:01.1 IDE interface: Intel [8086:7010]`；`lspci -v` 另外顯示 IRQ 與每個 BAR（`kernel/pci/pci_names.c` 提供名稱）。
- `hal_pci_map_bar(dev, n)`：把記憶體 BAR 映射成不可快取（`HAL_MMU_UNCACHED`，PCD+PWT；裝置暫存器絕對不能被 CPU 快取）。位址來自 `vmap`，在 `0xE0000000`–`0xF0000000`。
- `hal_pci_enable(dev, bits)`：開 I/O／記憶體解碼、bus master。
- `hal/dma.h`：`hal_dma_alloc(size, &phys)` 回傳虛擬位址並給出實體位址。裝置的描述子環、封包緩衝區都放這裡；x86 的 PCI DMA 與 CPU 快取一致，所以用一般（可快取）記憶體即可。

## 3. 驅動程式怎麼掛上去

`hal_pci_driver_t`（`hal/pci.h`）：名稱、vendor/device（或 vendor = 0 時用 class/subclass）、`probe(device)`。

1. 在 `drivers/<名稱>/` 寫驅動，匯出一個 `const hal_pci_driver_t <名稱>_pci_driver`；`probe` 做 `hal_pci_enable`、`hal_pci_map_bar`、`hal_dma_alloc`、初始化硬體，成功回 0。
2. 把它列進 `platform/pc/pci/pci_drivers.c` 的 `drivers[]`（平台負責組裝，所以只有它會 include 各個驅動）。
3. 開機時 `hal_pci_probe_drivers()` 對每個裝置找第一個符合且 `probe` 成功的驅動。

驅動可以 include `hal/`、`kernel/` 的裝置介面、`arch/x86/io/`（port I/O）；不可以 include 別的驅動或 `platform/`。

## 4. 中斷

PCI 裝置的中斷經由 8259 的 IRQ 線（interrupt line 欄位）。PCI 中斷是**電位觸發**（level）且可共用，而 8259 預設邊緣觸發：PIIX 的 ELCR（port 0x4D0/0x4D1）要把那條線設成電位觸發。驅動用 `hal_irq_register` 註冊處理函式，並在處理函式內讀裝置的中斷狀態暫存器來清除原因（否則中斷會一直來）。

## 5. 測試

- `tests/host/test_pci_bar.c`、`test_pci_names.c`。
- selftest `test_pci_devices`：QEMU 預設機器要有 host bridge（8086:1237）、ISA bridge、IDE（8086:7010）、標準 VGA（1234:1111）；VGA 的 BAR0 = 16 MiB 預取記憶體、BAR2 = 4 KiB；設定空間讀寫、`hal_pci_enable`、`hal_pci_map_bar` 並讀 Bochs ID 暫存器。
- selftest `test_dma_alloc`；`make test-shell` 跑 `lspci`。

## 6. 限制

- 只有 mechanism #1（沒有 PCIe MMCONFIG）、最多 64 個 function、BAR 超過 4 GiB 的無法映射。
- 沒有 MSI/MSI-X，沒有 PCI 中斷路由表（用韌體寫的 interrupt line）。
- `vmap` 只增不減（`hal_dma_free` 釋放頁框但虛擬位址不重用）。
