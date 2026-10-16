# USB：控制器列舉與設計

`drivers/usb_host/`、`kernel/usb/`（第一步已完成）；其餘是設計。

## 1. 概念

比喻：USB 像一棵樹。樹根是**主控制器（host controller）**，插在主機板上（PCI 裝置）；樹枝是**集線器（hub）**；樹葉是裝置（鍵盤、滑鼠、隨身碟…）。只有主控制器能開口：裝置永遠是被問的一方（輪詢）。

```
 CPU ── PCI ── [xHCI 主控制器] ── root hub（根集線器，4 個 port）
                                    ├─ port 1: 鍵盤（HID 類別）
                                    ├─ port 2: hub ─ 隨身碟（Mass Storage 類別）
                                    └─ port 3: 空
```

四種主控制器（年代由舊到新，各有不同的暫存器與資料結構）：

| 類型 | 速度 | 暫存器 | PCI prog IF | 說明 |
|---|---|---|---|---|
| UHCI | USB 1.1（12 Mb/s） | I/O port（BAR 4） | 0x00 | Intel/VIA；CPU 幫忙很多，軟體排程 frame |
| OHCI | USB 1.1 | 記憶體（BAR 0） | 0x10 | 其他廠商；硬體幫忙較多 |
| EHCI | USB 2.0（480 Mb/s） | 記憶體 | 0x20 | 高速；只管高速，低/全速要靠同晶片的 UHCI/OHCI「伴隨控制器」 |
| xHCI | USB 3.x（5/10/20 Gb/s）也相容舊速度 | 記憶體 | 0x30 | 一顆處理所有速度；現在所有新電腦（QEMU `qemu-xhci`） |

## 2. 已完成：列舉控制器（`usb` 指令）

`drivers/usb_host` 是一個 PCI 驅動（class 0x0C／subclass 0x03）：認出控制器的種類（prog IF）、讀版本與 port 數，登記到 `kernel/usb`。**不重置、不啟動**任何控制器（BIOS 可能還在用它提供開機時的 USB 鍵盤）。

- xHCI／EHCI：映射 BAR 0，讀 capability 暫存器的第一個 32 位元字（bits 7:0 = CAPLENGTH、bits 31:16 = HCIVERSION），再讀 HCSPARAMS1：xHCI 的 `MaxSlots`（bits 7:0）與 `MaxPorts`（bits 31:24）；EHCI 的 `N_PORTS`（bits 3:0）。**要用整個 32 位元字讀**：QEMU 的控制器對 capability 區的半字讀取回 0。
- OHCI：`HcRhDescriptorA`（偏移 0x48）的低 8 位元是 port 數。UHCI 固定 2 個 root port。
- 版本不在暫存器時用 PCI 設定空間 0x60 的 SBRN（0x20 = USB 2.0）。
- 範例輸出（QEMU）：`00:04.0 xHCI (USB 3.x) version 1.0, 8 ports, 64 device slots`、`EHCI … version 1.0, 6 ports`、`UHCI … 2 ports`。
- 測試：selftest `test_usb_hosts`（QEMU 同時掛 `qemu-xhci`、`usb-ehci`、`piix3-usb-uhci`）與 `make test-shell` 的 `usb` 指令。

## 3. 設計：之後要做的事（依序，每一步都能在 QEMU 驗證）

參考實作：Haiku 的 USB 匯流排驅動（MIT 授權，原始碼已淺層下載在 `third_party/downloads/haiku/haiku-src/src/add-ons/kernel/busses/usb/`：`xhci.cpp` 3300 行、`ehci.cpp` 3100 行、`uhci.cpp` 2500 行、`ohci.cpp`）。建議**只做 xHCI**（QEMU 與所有近年電腦都有；它也是唯一同時處理 1.x/2.0/3.x 裝置的）。

1. **啟動 xHCI**（約 600 行）：
   - 向 BIOS 取回控制權（xHCI Extended Capabilities 的 USB Legacy Support，把 BIOS-owned 位元清掉、設 OS-owned）；
   - 重置（USBCMD.HCRST，等 USBSTS.CNR 清除）、`CONFIG.MaxSlotsEn`；
   - 分配 DCBAA（Device Context Base Address Array）、**命令環**（command ring）、**事件環**（event ring，含 ERST）、scratchpad；`hal_dma_alloc` 提供實體連續記憶體；
   - 啟動 `USBCMD.R/S`；用 NOOP 命令測試命令環與事件環（`TRB` 的 cycle bit 是生產者／消費者的同步機制）。
2. **偵測 port 與列舉裝置**：讀 `PORTSC`（Current Connect Status）；重置 port（PR）；`Enable Slot` 命令 → 配置 device context 與 control endpoint 的 transfer ring → `Address Device` 命令；用 control transfer（SETUP／DATA／STATUS 三個 TRB）讀 device descriptor（18 位元組）與 configuration descriptor（含 interface、endpoint 描述子）。`usb` 指令列出裝置：vendor:product 與類別。
3. **hub**：hub 類別驅動（port 狀態變化的 interrupt endpoint；對 hub 的 port 做重置與電源管理）。
4. **HID**（鍵盤、滑鼠）：boot protocol（`SET_PROTOCOL 0`，固定 8 位元組的鍵盤報告、3–4 位元組的滑鼠報告）→ interrupt IN endpoint 輪詢 → 轉成 `input_event`（跟 PS/2 驅動一樣的 `input_event_push`）。這一步完成後，沒有 PS/2 的新電腦也能用鍵盤與滑鼠。
5. **Mass Storage（隨身碟）**：Bulk-Only Transport（CBW → 資料 → CSW）＋ SCSI 命令 `READ(10)`/`WRITE(10)`/`READ CAPACITY` → 註冊成 block 裝置（`usb0`），跟 `sata0` 一樣。
6. 之後才是 EHCI／OHCI／UHCI（舊電腦與某些虛擬機）、中斷（MSI）取代輪詢、isochronous（音訊）。

這些都需要的基礎建設已經有了：PCI 列舉與 BAR 映射（`hal/pci.h`）、DMA 記憶體（`hal/dma.h`）、`hal_timer_ns` 計時、block 裝置與 `input_event` 介面。最大的新工作是 TRB 環的管理（約 1000 行）與 USB 描述子的解析（純函式，適合 host 測試）。

## 4. 限制

目前只有「找到並列出控制器」：沒有任何 USB 裝置能用。
