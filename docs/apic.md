# APIC 與 MSI：中斷怎麼送到 CPU

`platform/pc/interrupt/`（`lapic.c`、`ioapic.c`、`hal_irq.c`、`irq.h`）、`platform/pc/pci/msi.c`、`arch/x86/interrupt/irq_stub.asm`、`hal/irq.h`、`hal/pci.h`

（P7 R3，自動排程一次做完，**使用者尚未審核**。）

## 1. 比喻

**8259 PIC** 像一棟小房子的**門鈴面板**：16 個按鈕（IRQ 0–15），每個按鈕對應固定的一種客人（計時器、鍵盤…），門鈴一響房子（CPU）就知道是哪個按鈕。

新電腦大得多：**很多 CPU**、很多 PCI 卡。所以有了兩層：
- **I/O APIC**（主機板上）：像**總機**，接 24 條（或更多）線，每條線可以設定「響的時候轉給哪一台電話（哪個 CPU）、說哪個代號（vector）」。
- **Local APIC**（每個 CPU 裡面）：像每個 CPU 的**櫃台**，收到代號就叫 CPU 去處理，處理完 CPU 要告訴櫃台「這件事辦完了」（EOI）。

**MSI**（Message Signalled Interrupts）更直接：PCI 卡不用拉線，而是**往一個特定的記憶體位址寫一個小數字**（0xFEExxxxx = 櫃台的位址；數字 = 代號）。就像客人不按門鈴，而是直接遞一張寫著代號的紙條給櫃台。好處：不用知道主機板怎麼把 PCI 的 INTA–INTD 接到哪條線（那份資訊要執行 ACPI 的 AML 位元組碼才知道，我們沒有 AML 直譯器）、不會共用線、每張卡自己一個代號。

名詞：**IRQ line**＝這個 kernel 裡的中斷編號（0–15 是傳統 ISA 線，16–63 專門給 MSI）；**GSI**（global system interrupt）＝I/O APIC 的輸入線編號；**vector**＝CPU 看到的代號（IDT 的索引，我們用 `0x20 + line`）；**EOI**＝end of interrupt。

## 2. 開機時發生什麼

```
acpi_init()      讀 MADT：local APIC 位址、I/O APIC 位址與 GSI 起點、interrupt source override
pic_init()       8259 重新對應到 vector 0x20–0x2F，全部遮蔽（總是先做）
irq_apic_enable()
   條件：CPU 有 APIC、MADT 有 I/O APIC、kernel 命令列沒有 "noapic"
   做的事：映射並啟用 local APIC（SVR 的 enable 位元 + spurious vector 0xFF）、
           映射 I/O APIC 並遮蔽所有輸入、hal_irq_controller() 變成 "IOAPIC"
之後 driver：hal_irq_register(line, handler); hal_irq_unmask(line)
   8259：  清 PIC 的遮蔽位元
   IOAPIC：查 override 表得到 GSI（例如計時器 IRQ0 → GSI 2）和極性／觸發方式，
           寫 redirection entry（vector = 0x20 + line，目的地 = 開機 CPU 的 APIC ID），解除遮蔽
```

**interrupt source override**（MADT 的 type 2）：ISA 的 IRQ 0 到了 I/O APIC 不一定在 0 號輸入；幾乎所有電腦把計時器接到輸入 2（QEMU 與 P520 都是）。PCI 專用的 IRQ（QEMU 的 5/9/10/11）常標為「level、active low」。沒有 override 的 ISA 線預設是 edge、active high。

**EOI**：APIC 模式下 `irq_handler` 的最後呼叫 `lapic_eoi()`（寫 local APIC 的 EOI 暫存器）。8259 模式照舊送 PIC 的 EOI，並處理 IRQ 7/15 的假中斷。APIC 的假中斷（vector 0xFF）有自己的 stub（`irq_spurious_stub`，直接 `iret`，不能 EOI）。

**安全模式**：GRUB 的 kernel 命令列（`multiboot /boot/kernel.elf noapic`）有 `noapic` 就留在 8259（`hal_boot_cmdline()` 讀得到命令列，Multiboot 1 與 2 都支援）。`make test-noapic` 用 `-append noapic` 把整個 selftest 再跑一遍。

## 3. PIT 的模式 3 與 QEMU

計時器（PIT）原本設成「模式 3（方波）」。在 8259 下一個週期一次上升緣＝一次中斷；但**經 QEMU 的 I/O APIC 時每個週期送了兩次**（用 `hal_timer_ns` 量到：11 個 tick 只花 51 ms）。改成「模式 2（rate generator）」後 8259 與 I/O APIC 都是每週期一次。（真實硬體上兩者應該相同；Linux 的 PIT clockevent 也用模式 2。）

## 4. MSI

```c
int line = hal_pci_enable_msi(&device, handler);   // >= 16，或 -1（沒有 MSI/MSI-X，或在 8259 模式）
```

`hal_irq_msi_alloc` 在 16–63 找一條空的線、登記 handler，回傳**要告訴裝置的位址與資料**（位址 `0xFEE00000 | (目的 APIC ID << 12)`，資料 = vector）。`hal_pci_enable_msi`（`platform/pc/pci/msi.c`）走 PCI 的 capability 串列（設定空間 0x34 起）找到 MSI（id 5）或 MSI-X（id 0x11），把位址與資料寫進去並打開 enable 位元：

```
MSI   : +2 控制  +4 位址(低)  [+8 位址(高)]  +8/+12 資料(16 位元)  [遮蔽位元]
MSI-X : 訊息表在某個 BAR 裡（每項 16 位元組：位址低、位址高、資料、控制），只用第 0 項
```

裝置自己的「中斷要不要送」位元（例如網卡的 interrupt mask 暫存器）仍是 driver 的事。

**目前的使用情形**：e1000（82540EM）、AHCI、HDA 在 QEMU 與我們的 driver 裡本來就靠輪詢（driver 不開裝置的中斷，或有中斷也只是計數），QEMU 的 82540EM 也沒有 MSI，所以沒有改用 MSI（改了也沒有好處）。MSI 由 R4 的 xHCI driver 實際使用；自測以 self-IPI 驗證 MSI 線路（見下）。

## 5. 限制

- 只有開機 CPU 接收外部中斷；其他 CPU 啟動後 `hlt`（沒有 per-CPU 資料，SMP 排程不在範圍內）。
- 沒有 x2APIC、沒有 local APIC timer（計時仍用 PIT）、沒有 IOMMU/中斷重映射。
- PCI 的 INTx（沒有 MSI 的卡）在 APIC 模式下沒有路由資訊（要 AML），所以會變成純輪詢；e1000/virtio-net 因此在 APIC 模式下不會收到中斷（輪詢本來就夠用，`sched_wait` 在計時器中斷時醒來）。
- 單一 vector：MSI 只用 1 個、MSI-X 只用第 0 項。

## 6. 測試

- `tests/host/test_acpi_parse.c`：MADT 的 override 與 GSI 起點。
- `platform/pc/test/selftest_pc.c`：`test_apic_in_use`（QEMU 的 MADT 有 I/O APIC，計時器 override = 輸入 2）、`test_msi_line_delivery`（配一條 MSI 線，用 local APIC 的 self-IPI 送 vector，檢查 handler 被呼叫；這條路徑和裝置寫 MSI 訊息完全一樣：vector → IDT stub → `irq_handler`）、`test_pci_capabilities`（capability 串列）；8259 專用的測試在 APIC 模式下檢查「8259 全遮蔽」。
- `make test-noapic`（`-append noapic`，8259 路徑）、`make test-q35`（q35 晶片組、e1000 + dhcp/ping）、`make test-uefi`（OVMF 下的 APIC）。
