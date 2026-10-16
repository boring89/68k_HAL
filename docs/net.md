# 網路：e1000 網卡與最小網路堆疊

`drivers/e1000/`、`kernel/net/`、`hal/pci.h`、`hal/dma.h`

## 1. 概念

比喻：網路封包像一層一層的信封。最外面的信封寫「實體地址」（MAC，網卡的出廠號碼），裡面一個信封寫「網際網路地址」（IP），再裡面是「哪個程式」（port）和內容：

```
 Ethernet   [目的 MAC | 來源 MAC | 類型]         ← 網卡之間（同一條線上）
   ARP      「10.0.2.2 的 MAC 是誰？」             ← 用 IP 查 MAC
   IPv4     [版本/長度 | 協定 | 來源 IP | 目的 IP | 檢查碼 | 內容]
     ICMP   ping（echo 請求／回覆）
     UDP    [來源 port | 目的 port | 長度 | 檢查碼 | 資料]
       DHCP 向路由器要一個 IP；DNS 把名字換成 IP
```

QEMU 的 `-netdev user`（slirp）幫你模擬一個小型家用網路：

| 角色 | 位址 |
|---|---|
| 我們（DHCP 配給） | 10.0.2.15 |
| 閘道（也回應 ping、DHCP） | 10.0.2.2 |
| DNS（轉送到主機的解析器） | 10.0.2.3 |

## 2. 驅動：Intel e1000（`drivers/e1000/e1000.c`）

QEMU 預設網卡是 Intel 82540EM（PCI 8086:100E）。資料來源：Intel 8254x Software Developer's Manual（暫存器與描述子格式）。

- **暫存器**在 BAR0（128 KiB MMIO），用 `hal_pci_map_bar` 映射成不可快取。`CTRL.RST` 重置；`RAL0/RAH0` 已含 EEPROM 載入的 MAC。
- **描述子環**（ring）：一塊記憶體，裡面是一排 16 位元組的描述子，每個指向一個 2048 位元組的緩衝區。CPU 與網卡輪流擁有描述子，用 head（網卡處理到哪）和 tail（CPU 給到哪）兩個暫存器協調：

```
 接收環（32 個）                          傳送環（8 個）
  RDH ─► 網卡下一個要填的                   TDH ─► 網卡下一個要送的
  ...  [DD=1: 收到了，等 CPU 取]            ...  [DD=1: 送完了，可以重用]
  RDT ─► CPU 還給網卡的最後一個             TDT ─► CPU 下一個要放的
```

  接收：描述子 `status` 的 DD（descriptor done）位元由網卡設定；驅動讀出封包交給堆疊，清掉 status，把 `RDT` 指向這個描述子還給網卡。傳送：把封包複製到緩衝區，設定長度與命令（EOP 包結尾、IFCS 由網卡加 CRC、RS 完成後回報），然後把 `TDT` 加一。
- **DMA 記憶體**用 `hal_dma_alloc`（實體連續）；描述子裡放的是實體位址。
- **中斷**：註冊 PCI 給的 IRQ 線，處理函式只讀 `ICR`（讀即確認）；封包由 `net_poll()` 取走（主迴圈與所有等待的函式都會呼叫它）。這樣協定程式不在中斷內執行。

## 3. 堆疊（`kernel/net/`）

- `packet.c`：所有封包的**組裝與解析**（純函式，`tests/host/test_net_packet.c` 涵蓋）：Internet checksum（RFC 1071）、Ethernet、ARP、IPv4、ICMP echo、UDP（含 pseudo header）、DHCP（RFC 2131）、DNS（RFC 1035，含壓縮指標）。
- `net.c`：狀態與流程：裝置註冊、ARP 快取（8 項）、收到封包的分派、回應 ping 與 ARP、UDP 監聽、`net_dhcp`（DISCOVER → OFFER → REQUEST → ACK，每 2 秒重試）、`net_ping`、`net_dns_resolve`。
- shell 指令：`net`（網卡與位址）、`dhcp`、`ping a.b.c.d`、`dns 名字`。

## 4. 測試

- host：`test_net_packet`。
- `make test-qemu`（QEMU 帶 `-netdev user -device e1000`）：selftest `test_net` 做 DHCP（必須拿到 10.0.2.15、閘道 10.0.2.2、DNS 10.0.2.3）、ping 閘道、DNS 查 `localhost`。
- `make test-shell`：在開機的 ISO 上輸入 `dhcp`、`ping 10.0.2.2`、`dns localhost` 並檢查輸出。

## 5. 限制

- 沒有 TCP、沒有 IP 分片重組、沒有 IPv6、沒有 DHCP 租約更新、ARP 快取不過期。
- 一次只有一張網卡；使用者程式還不能用 socket（沒有 `socket` 系統呼叫）。
- 沒有 virtio-net 與其他網卡。
