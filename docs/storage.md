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
| 限制 | 只有 primary master、LBA28（最大 128 GiB）、唯讀；一次命令最多 255 個扇區（驅動自己分批）；輪詢，`nIEN` 關閉 IRQ 14 |

註冊成 `ata0`。`hal_init` 結尾呼叫 `ata_init()`，沒有磁碟時什麼都不會發生。
QEMU 的 `-cdrom` 掛在 secondary channel，所以不會被誤認。

## 5. 測試

- `tests/host/test_block.c`：註冊表、範圍檢查（含 `lba + count` 溢位）、ramdisk。
- QEMU selftest（`make test-qemu`）：`-initrd` 載入測試檔案與 `disk.img`；`-drive ...,if=ide` 掛 300 個扇區的
  IDE 磁碟（超過 255，所以會用兩次命令），逐位元組比對，並檢查最後一個扇區與越界。
