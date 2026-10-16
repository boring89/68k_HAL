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

## 2.5 virtio-net（`drivers/virtio_net`，Q11）

virtio 是「為虛擬機設計的裝置」：不模擬舊硬體，而是主機與客體共用幾個**環（virtqueue）**。比喻：兩條輸送帶，一條是我們放「空碗」讓主機把收到的封包裝進來（接收佇列），一條是我們放「裝好的碗」讓主機送出去（傳送佇列）。

```
 描述子表（每個 16 位元組：緩衝區位址、長度、旗標）
 available 環：驅動寫「這幾個描述子可以給裝置用了」，再把 index 加一，然後按鈴（寫 QUEUE_NOTIFY）
 used 環：裝置寫「這個描述子用完了，長度 n」
```

用 transitional 裝置的**傳統（legacy）PCI 介面**（PCI 1AF4:1000，I/O BAR 0）：重置 → ACKNOWLEDGE → DRIVER → 讀裝置功能位元（只用 `VIRTIO_NET_F_MAC`）→ 設定兩個佇列（把「環結構的頁框號」寫進 `QUEUE_ADDRESS`）→ `DRIVER_OK`。每個封包前有 10 位元組的 virtio-net 標頭（全 0：不用校驗和與分段卸載）。接收：先給裝置 32 個 2048 位元組的緩衝區；`virtio_poll` 讀 used 環、把封包交給堆疊、再把緩衝區還給裝置。資料來源：Virtual I/O Device (VIRTIO) Specification 1.1（2.6、4.1.4.8、5.1）。限制：只有 legacy 介面（沒有 1.0 的 modern 介面、沒有 offload、沒有多佇列）。

驗證：`make test-virtio-net`：QEMU `-device virtio-net-pci`，shell 的 `net`（顯示 `virtio-net`）、`dhcp`、`ping`、`dns` 都通過。堆疊一次只用一張網卡（先註冊的那張）。

## 3. 堆疊（`kernel/net/`）

- `packet.c`：所有封包的**組裝與解析**（純函式，`tests/host/test_net_packet.c` 涵蓋）：Internet checksum（RFC 1071）、Ethernet、ARP、IPv4、ICMP echo、UDP（含 pseudo header）、DHCP（RFC 2131）、DNS（RFC 1035，含壓縮指標）。
- `net.c`：狀態與流程：裝置註冊、ARP 快取（8 項）、收到封包的分派、回應 ping 與 ARP、UDP 監聽、`net_dhcp`（DISCOVER → OFFER → REQUEST → ACK，每 2 秒重試）、`net_ping`、`net_dns_resolve`。
- shell 指令：`net`（網卡與位址）、`dhcp`、`ping a.b.c.d`、`dns 名字`。

## 3.5 TCP 與 socket（Q9）

比喻：UDP 像寄明信片（可能掉、可能亂序）；TCP 像打電話：先撥號（三次握手），之後兩端都有「一條可靠的位元組流」，掉了的封包由雙方默默補寄。

```
 客戶端                          伺服器
  SYN (seq=x) ───────────────►
              ◄───────────── SYN+ACK (seq=y, ack=x+1)
  ACK (ack=y+1) ─────────────►          ← ESTABLISHED
  資料 (seq=x+1..) ──────────►          ← 收到後回 ACK（ack = 下一個要的位元組）
  FIN ───────────────────────►          ← 我這邊沒有資料了；對方也 FIN 才完全結束
```

`kernel/net/tcp.c`（RFC 793）：
- **狀態機**：CLOSED、LISTEN、SYN_SENT、SYN_RECEIVED、ESTABLISHED、FIN_WAIT_1/2、CLOSING、TIME_WAIT、CLOSE_WAIT、LAST_ACK。
- **可靠傳輸**：每個連線有 16 KiB 的傳送環與接收環；送出的資料留在傳送環裡直到被 ACK；逾時（1 s 起、每次加倍到 8 s、最多 6 次）就「退回最舊未確認的位元組」全部重送（go-back-N）。
- **流量控制**：每個封包帶「視窗」（接收環剩多少空間）；對方視窗是 0 時定期送 1 位元組探測。
- **沒有的**：亂序資料（收到缺口就丟掉、重複 ACK 要對方重送）、壅塞控制、選擇性確認、視窗縮放、緊急資料。TIME_WAIT 只有 2 秒。
- 純資料與函式：封包用函式指標送出、時間用函式指標取得，所以 `tests/host/test_tcp.c` 把兩個 TCP 堆疊接在一起（可以隨機掉封包、停電話線）測試：握手與關閉、30 萬位元組傳輸、14% 封包遺失仍正確、SYN 重送、被拒絕（RST）、重設、視窗填滿與恢復、逾時。

`net.c` 把 TCP 接到 IP（協定 6），並提供會等待的輔助函式 `net_tcp_connect/send_all/recv/close`。shell 指令 `http <host> [port [path]]` 取得網頁（host 可以是名字，會先用 DNS）。

**給使用者程式的 BSD socket**（`kernel/user/socket.c`）：`socket(AF_INET, SOCK_STREAM | SOCK_DGRAM)`、`bind`、`connect`、`listen`、`accept`、`send/recv/sendto/recvfrom/sendmsg/recvmsg`、`shutdown`、`getsockname/getpeername`、`setsockopt`（只認 `SO_RCVTIMEO`）、`getsockopt`，加上 `poll`、`select`。i386 Linux 把所有 socket 呼叫包成一個系統呼叫 `socketcall(編號, 參數陣列)`（musl 用它）。socket 是一個檔案描述子，所以 `read/write/close/dup/poll` 都能用。`/etc/resolv.conf`（`nameserver 10.0.2.3`）與 `/etc/hosts`（initrd 裡）讓 musl 的 `getaddrinfo` 可以運作。範例：`userland/httpget.c`（TCP）、`userland/dnsquery.c`（UDP 與 `getaddrinfo`）。

## 4. 測試

- host：`test_net_packet`。
- `make test-qemu`（QEMU 帶 `-netdev user -device e1000`）：selftest `test_net` 做 DHCP（必須拿到 10.0.2.15、閘道 10.0.2.2、DNS 10.0.2.3）、ping 閘道、DNS 查 `localhost`。
- `make test-shell`：在開機的 ISO 上輸入 `dhcp`、`ping 10.0.2.2`、`dns localhost` 並檢查輸出。
- `make test-net`：host 上開一個 `python3 -m http.server`（綁 127.0.0.1），QEMU 內經 slirp 的 10.0.2.2（= host）用 shell 的 `http` 與使用者程式 `httpget`（BSD socket）取得網頁並比對內容；`dnsquery` 測 UDP socket、`poll`、`getaddrinfo`。

## 5. 限制

- TCP 的限制見第 3.5 節；沒有 IP 分片重組、沒有 IPv6、沒有 DHCP 租約更新、ARP 快取不過期。
- 一次只有一張網卡；socket 只有 IPv4、沒有 `SOCK_RAW`、Unix domain socket 與 `socketpair`；被中斷的阻塞呼叫不會回 `EINTR`（沒有訊號）。
- 沒有 virtio-net 與其他網卡。
