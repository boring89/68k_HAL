# USB：控制器列舉與設計

`drivers/usb_host/`（列舉控制器）、`drivers/xhci/`（xHCI 控制器與 HID 鍵盤滑鼠）、`kernel/usb/`（描述子、HID 報告解析、裝置清單）。

**P7 R4 完成（自動排程一次做完，使用者尚未審核）**：xHCI 啟動、port 重置、裝置列舉、HID boot 鍵盤與滑鼠、熱插拔、MSI。集線器（hub，R10）也有；隨身碟（Mass Storage，R11）也有（唯讀／讀寫的區塊裝置 `usb0`）。

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

## 2.5 xHCI driver（`drivers/xhci/xhci.c`）

比喻：xHCI 控制器像一個**郵局**。我們（軟體）把「要寄的信」（TRB，每個 16 位元組的小紙條）放進**環狀的信箱**（ring）然後按**門鈴**（doorbell）；郵局把做完的事寫在另一個環狀信箱（事件環）並響鈴（中斷）。每張紙條有一個**cycle bit**表示「這張現在歸誰」：生產者每繞環一圈就把「有效」的意義翻轉一次，所以不需要頭尾指標暫存器。

```
 軟體                                    xHCI
 命令環 ──[Enable Slot][Address Device]…──▶            （按 doorbell 0）
 傳輸環 ──[Setup][Data][Status]… ──────────▶            （按 doorbell slot，目標 endpoint）
 事件環 ◀──[Command Completion][Transfer Event][Port Status Change]── 中斷（MSI）
```

啟動順序（`xhci_probe` → `start_controller`）：
1. PCI 驅動順序：`xhci_pci_driver` 排在 `usb_host` 之前；它先登記控制器（和 usb_host 同樣的記錄，讓 `usb` 能列出；驅動之間不能互相 include，所以自己做一份），再自己接手。
2. 向 BIOS 取回控制權（USB Legacy Support 擴充 capability：設 OS-owned 位元、等 BIOS-owned 清除、關掉 SMI）。
3. 停止、重置（`HCRST`）、設 `CONFIG`、配置 DCBAA（device context base address array）、scratchpad 緩衝（控制器要求的工作記憶）、命令環、事件環（一個 segment 的 ERST）、啟動 `R/S`。
4. 中斷：MSI/MSI-X（`hal_pci_enable_msi`）；沒有就用 PCI 的傳統線；**輪詢也保留**：`usb_poll()`（kernel 主迴圈與排程器的閒置迴圈每圈呼叫）會自己跑事件環，所以中斷丟了或不存在也不會掉鍵。處理事件環的兩個地方（中斷處理程式與輪詢）互斥靠「輪詢時關中斷」（`hal_cpu_irq_save`）。
5. 等 200 ms（裝置接上要時間），掃描每個 port。

每個接了東西的 port（`scan_port`）：重置 port（USB2 需要；USB3 硬體自己訓練，已在 U0 就不用）→ `Enable Slot`（拿到 slot 編號）→ 配 device context、input context、控制 endpoint 的傳輸環 → `Address Device`（xHC 送出 SET_ADDRESS）→ 讀 device descriptor 前 8 位元組得知控制 endpoint 的真正封包大小（全速裝置可能是 8/16/32/64，不同就 `Evaluate Context` 更正）→ 讀完整 device 與 configuration descriptor（`kernel/usb/descriptor.c` 解析）→ 如果有 HID boot 介面：`SET_CONFIGURATION`、`SET_PROTOCOL(boot)`、`SET_IDLE(0)`、`Configure Endpoint`（加上 interrupt IN endpoint 與它的傳輸環）→ 放一個 Normal TRB 等報告。

**熱插拔**：事件環的 Port Status Change 事件只設旗標；`usb_poll()` 呼叫的 `service()`（不在中斷裡，可以等）重新掃那個 port：拔掉就 `Disable Slot` 並釋放；插上就列舉。

HID 報告 → `kernel/usb/hid.c`：鍵盤 8 位元組（修飾鍵位元 + 最多 6 個按下的鍵）與上一份比較，產生 release → 修飾鍵變化 → press 的事件（所以 Ctrl+C 是 Ctrl 先、C 後）；滑鼠 3–4 位元組（按鈕、dx、dy）直接成為滑鼠事件。使用 `input_event_push`，和 PS/2 驅動一樣，所以 shell、使用者程式、Mac 都不需要知道輸入來自 USB。

**Hub（P7 R10）**：class 9 的裝置：`SET_CONFIGURATION`，讀 hub descriptor（USB2：型別 0x29；USB3：0x2A）得知 port 數，用 `Configure Endpoint`（只改 slot context：Hub 位元、port 數、TT think time）告訴控制器它是 hub（SuperSpeed hub 另送 `SET_HUB_DEPTH`），對每個 port `SET_FEATURE(PORT_POWER)`，等 300 ms，再逐 port 用 `GET_STATUS`（class request）看有沒有裝置：有就 `SET_FEATURE(PORT_RESET)`、等 reset 完成、從 port status 取得速度（USB2 hub：bit 9 低速、bit 10 高速、否則全速；USB3 hub 一律 SuperSpeed），然後和 root port 上的裝置同一套流程（`enumerate_device`），差別是 slot context 要填 **route string**（從 root 往下每層 hub 一個 4 位元的 port 編號）與 **root hub port number**，低／全速裝置接在高速 hub 後面時還要填 **TT**（transaction translator）所在的 hub slot 與 port。熱插拔：`poll_hubs` 每 250 ms 輪詢所有 hub 的所有 port（不用 hub 的狀態變化 interrupt endpoint，比較簡單；代價是最多 250 ms 的延遲）。拔掉 hub 會遞迴移除後面所有裝置。深度上限 5 層。測試：`make test-usb` 的第三輪（QEMU `usb-hub` 後面的 usb-kbd、熱插拔的 usb-mouse）。**SuperSpeed hub 與多 TT hub 沒有測試**（QEMU 沒有）。

限制：只用第一個 xHCI 控制器；interrupt endpoint 出錯（stall）時停止輪詢該 endpoint 而不做 Reset Endpoint 復原；沒有 USB 3 的 stream、isochronous、電源管理（U1/U2）；沒有 Mass Storage。

測試：`tests/host/test_usb_hid.c`、`test_usb_descriptor.c`（純函式）；`make test-usb`：q35 + `-machine q35,i8042=off`（沒有 PS/2）+ `qemu-xhci` + `usb-kbd` + `usb-mouse`，用 monitor 的 `sendkey` 打字、`mouse_move` 移動滑鼠（`/bin/evtest` 看到事件）；另一輪從只有控制器開始，`device_add usb-kbd`／`usb-mouse` 熱插拔。

**Mass Storage（P7 R11）**：class 8、subclass 6（SCSI）、protocol 0x50（Bulk-Only Transport）的介面（`usb_find_storage`）：`SET_CONFIGURATION` → `Configure Endpoint` 一次加兩個 bulk endpoint（IN＝type 6、OUT＝type 2，各自一個傳輸環）→ 每個 SCSI 命令是三步：**CBW**（command block wrapper，31 位元組，簽章 `USBC`、tag、資料長度、方向、LUN、CDB）從 bulk OUT 送出，資料階段（透過 64 KiB 的 bounce buffer；每個 TRB 不能跨 64 KiB 邊界，所以每頁一個 TRB 用 chain 串起來），**CSW**（13 位元組，簽章 `USBS`、同一個 tag、狀態 0 = good）從 bulk IN 讀回。啟動時用 `TEST UNIT READY` 輪詢到就緒（失敗就 `REQUEST SENSE` 清掉狀態，最多 20 次）、`READ CAPACITY(10)` 取得容量與區塊大小，登記成區塊裝置 `usb0`（`disks` 會列出）。讀寫用 `READ(10)`／`WRITE(10)`（寫完 `SYNCHRONIZE CACHE`）。stall 時做 Reset Endpoint ＋ Set TR Dequeue Pointer ＋ `CLEAR_FEATURE(ENDPOINT_HALT)`。限制：LBA 32 位元（2 TiB）、單一 LUN、同步（等完成才回）、拔掉後區塊裝置仍在清單但讀寫回 -1。測試：`test_usb_storage_disk`（QEMU `usb-storage` 在 xHCI 後面：全部內容比對、寫入讀回還原）。**未在實機驗證**。

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

見 2.5。UHCI／OHCI／EHCI 只列出，不驅動。
