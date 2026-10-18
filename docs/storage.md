# 儲存：區塊裝置、ramdisk、ATA（kernel/block、drivers/ata）

> 自動開發時寫的，尚未審核。檔案系統本身見 `docs/hfs.md`。

## 1. 分層

比喻：磁碟是倉庫，檔案系統是倉管員。倉管員不需要知道貨是用卡車（ATA）還是用手推車（記憶體）
運來的，只要說「給我第 n 號貨架上 k 格的東西」。這個統一的說法就是**區塊裝置**。

```
shell: ls / cat
   │
kernel/fs/volume ── 開機時找第一個 HFS
   │
kernel/fs/hfs、apm ── 讀 HFS、分割區表
   │  block_read(裝置, 起始區塊, 數量, 緩衝)
kernel/block ── 區塊裝置註冊表（最多 8 個）
   │  ops.read
   ├─ kernel/block/ramdisk ── 把一段記憶體（GRUB module）當磁碟
   └─ drivers/ata ── IDE 硬碟（PIO）
```

## 2. 區塊裝置（kernel/block/block.h）

`block_device_t`：名稱、區塊大小、區塊數、`ops.read`、驅動自己的資料。驅動註冊，檔案系統讀取。
`block_read` 先檢查範圍（`lba + count` 不溢位、不超出裝置），所以驅動不用再檢查。
註冊時名稱不能重複，結構要一直有效。shell 指令 `disks` 列出所有裝置。

## 3. ramdisk（kernel/block/ramdisk）

GRUB 的 `module` 行（或 QEMU 的 `-initrd "檔案 名稱,檔案 名稱"`）把檔案載入記憶體；
`ramdisk_register_boot_modules()` 把每個 module 註冊成 `ram0`、`ram1`…，區塊大小 512，
不滿一個區塊的尾巴忽略。module 的記憶體由 PMM 保留並唯讀映射（見 `docs/hal.md` 的 `hal_boot_reserved_region`）。
這是**不需要任何磁碟驅動**就能把磁碟映像交給 kernel 的方法，所以測試與目前的開機流程都用它。

## 4. ATA PIO（drivers/ata）

**ATA** 是 IDE 硬碟的介面；**PIO**（Programmed I/O）是最簡單的傳輸方式：CPU 自己從資料埠一個字一個字讀。
慢，但不需要 DMA 或中斷，幾十行就能寫完。

| 項目 | 內容（OSDev Wiki "ATA PIO Mode"） |
|---|---|
| 埠 | primary channel：資料 0x1F0、sector count 0x1F2、LBA 0x1F3–0x1F5、drive 0x1F6、狀態/命令 0x1F7、控制 0x3F6 |
| 辨識 | 選 master，送 `IDENTIFY`（0xEC）；狀態 0 或 0xFF 表示沒有裝置；LBA mid/high 不為 0 表示是 ATAPI 或 SATA（略過）；從資料埠讀 256 字，字 60–61 是 LBA28 的扇區數 |
| 讀取 | drive 暫存器放 `0xE0 | LBA 的 24–27 位元`，sector count、LBA 低中高，送 `READ SECTORS`（0x20）；每個扇區先等 400 ns（讀 4 次 alternate status）、等 BSY 清除與 DRQ 設定，再讀 256 字 |
| 限制 | 只有 primary master、LBA28（最大 128 GiB）；可寫（WRITE SECTORS＋FLUSH CACHE，`block_write`；`mac` 用它保存改動，見 `docs/mac.md` 第 12 節）；一次命令最多 255 個扇區（驅動自己分批）；輪詢，`nIEN` 關閉 IRQ 14 |

註冊成 `ata0`。`hal_init` 結尾呼叫 `ata_init()`，沒有磁碟時什麼都不會發生。
QEMU 的 `-cdrom` 掛在 secondary channel，所以不會被誤認。

## 4.5 AHCI（SATA，`drivers/ahci`，Q10）

**AHCI** 是 SATA 磁碟的標準控制器介面（幾乎所有近 15 年的 PC；QEMU 的 `-device ich9-ahci`）。與 ATA PIO 的差別：命令不是「一個一個暫存器寫」，而是在記憶體裡準備好整份命令，控制器自己用 **DMA** 取命令、搬資料。

```
 記憶體（hal_dma_alloc）                         控制器（ABAR = PCI BAR 5，一個 port 0x80 位元組的暫存器）
 ┌ command list（32 個命令標頭）──┐ CLB
 │  [0] 旗標 | PRDTL | 命令表位址 ├───►  命令表：
 └────────────────────────────────┘          CFIS（H2D register FIS：0x27、命令、LBA48、扇區數）
 ┌ received FIS 區 ┐ FB                      PRDT（資料緩衝區的位址與長度）
 └─────────────────┘
 設定 CI 的 bit 0 ──► 控制器執行命令 ──► 完成後清除 CI 的 bit 0（錯誤時 TFD 的 ERR 位元）
```

步驟（AHCI 1.3.1、OSDev Wiki "AHCI"）：PCI 比對 class 1／subclass 6／prog IF 1；開啟記憶體與 bus master；`GHC.AE` 打開 AHCI 模式；對 `PI` 中每個實作的 port：`SSTS` 說明有裝置（DET = 3、IPM = 1）、`SIG` 是 SATA 磁碟（0x101）；停掉 port（清 `ST` 與 `FRE`，等 `CR`、`FR` 清除）；配置 command list、received-FIS 區、命令表與 64 KiB 的暫存緩衝區（`hal_dma_alloc`）；開 `FRE`、`ST`；`IDENTIFY DEVICE` 取得扇區數（字 100–103，48 位元）；註冊 block 裝置 `sata0`、`sata1`…。

讀寫：`READ DMA EXT`（0x25）、`WRITE DMA EXT`（0x35）、寫完 `FLUSH CACHE EXT`（0xEA）。每個命令最多 128 個扇區（暫存緩衝區大小；驅動自己分批），用輪詢等 `CI` 清除。限制：沒有 ATAPI、沒有 port multiplier、沒有熱插拔、沒有中斷、只用 command slot 0。

## 4.6 NVMe（`drivers/nvme`，P7 R5，使用者尚未審核）

比喻：NVMe 像一間**有很多櫃台的銀行**，而 AHCI／ATA 是只有一個窗口。每個櫃台是一對**佇列**：提交佇列（SQ，我們放「工單」，每張 64 位元組）與完成佇列（CQ，硬碟放「回條」，每張 16 位元組）。放好工單後敲一下**門鈴**（暫存器，寫入新的尾端位置）；硬碟做完把回條放進 CQ，並翻轉回條上的 **phase 位元**（每繞佇列一圈翻一次，所以我們能分辨新舊回條）；我們讀完回條也敲 CQ 的門鈴。佇列 0 是管理佇列（建立佇列、identify），佇列 1 是我們建立的 I/O 佇列。

```
 啟動：CC.EN=0 → 等 RDY=0 → 設 AQA/ASQ/ACQ（管理佇列的大小與位址）→ CC.EN=1（設好 64/16 位元組項目大小）→ 等 RDY=1
       Identify Controller（型號、MDTS 單次傳輸上限）→ Identify Namespace 1（區塊數 NSZE、區塊大小 2^LBADS）
       Create I/O Completion Queue（QID 1）→ Create I/O Submission Queue（QID 1，掛在 CQ 1）
 讀寫：Read(0x02)／Write(0x01)，起始 LBA、區塊數-1、資料位址 PRP1/PRP2；寫完送 Flush(0x00)
```

**PRP**（physical region page）：描述資料在哪些實體頁：1 頁只用 PRP1；2 頁用 PRP1 和 PRP2；更多則 PRP2 指向一頁「頁位址清單」（每項 64 位元，本驅動最多 128 KiB 一次，31 項）。資料經過 128 KiB 的 bounce buffer（`hal_dma_alloc`），比較大的 `block_read/write` 自動拆成多個命令。

限制：只用 namespace 1、一個 I/O 佇列、一次一個命令、輪詢（建立 CQ 時 IEN=0，沒有中斷）、不支援 MSI-X／命名空間管理／電源狀態／scatter-gather 以外的 SGL；controller 最小頁面大小必須是 4 KiB。**QEMU 驗證，未在實體 NVMe（P520 的 Kingston KC3000）上驗證**。

測試：`kernel/test/selftest.c` 的 `test_nvme_disk`（QEMU `-device nvme`，256 KiB 映像：逐位元組比對全部內容、讀超出範圍失敗、不同大小的寫入—讀回—還原：1 頁、2 頁（PRP2 = 第二頁）、17 個區塊（PRP list）、全部 512 個區塊（拆成兩個命令））。`disks` 會列出 `nvme0`。

## 4.7 P10 加在區塊層上的東西（`docs/mac_disk.md`）

- `kernel/fs/gpt.c`：GPT 解析；`gpt_view_register` 把一個分割區變成一個區塊裝置（讀寫轉給整顆磁碟，加上起點）。
- `kernel/block/overlay.c`：寫入時複製的疊加層（系統碟的原始映像不改，寫入存在另一個分割區；斷電安全）。
- `kernel/block/cache.c`：write-through 的讀取快取，只用在 68k_HAL 磁碟；`block_read`／`block_write` 遇到有快取的裝置就走它。
- 寫入的約定（`block.h`：回傳時已在媒體上）三個驅動都做到了：ATA 送 FLUSH CACHE、AHCI 送 FLUSH CACHE EXT、NVMe 送 Flush。

## 5. 測試

- `tests/host/test_block.c`：註冊表、範圍檢查（含 `lba + count` 溢位）、ramdisk。
- AHCI：QEMU `-device ich9-ahci -drive ...,if=none -device ide-hd,bus=ahci.0` 掛 128 個扇區的 SATA 磁碟（每次測試重新產生）：逐位元組比對、越界、寫入後讀回並還原（含超過一個命令的 128 扇區）；IDE 磁碟也做同樣的寫入測試。
- QEMU selftest（`make test-qemu`）：`-initrd` 載入測試檔案與 `disk.img`；`-drive ...,if=ide` 掛 300 個扇區的
  IDE 磁碟（超過 255，所以會用兩次命令），逐位元組比對，並檢查最後一個扇區與越界。
