# HAL（硬體抽象層）

`hal/`（介面）、`arch/x86/`（CPU）、`platform/pc/`（PC 平台）、`drivers/`（裝置）、`tools/check_boundary.sh`（邊界檢查）

HAL 是 kernel 與硬體之間的介面。kernel 只呼叫 `hal_irq_unmask(1)`、`hal_timer_ticks()` 這類函式，不知道背後是 8259 PIC 還是 APIC、用 port I/O 還是 MMIO。換硬體時只換介面後面的實作，kernel 不用改。

對這個專案來說，HAL 是「讓 Classic Mac OS 的架構在現代硬體上延續」的前提：原本的 Mac OS 與 Toolbox 直接存取硬體位址，所以很難換硬體。之後的 68k 相容層只依賴 kernel 服務，kernel 只依賴 HAL。

## 1. 基本概念

比喻：電器（kernel）只認得插座（HAL 介面）；牆裡的電線怎麼走（硬體）、是 110V 還是 220V，由插座後面的配線（實作）處理。

```
┌─────────────────────────────┐
│ 68k 應用程式（未來）          │  A-line trap（0xAxxx）
├─────────────────────────────┤
│ 68k 相容層（未來）            │  翻譯「API 語意」：Toolbox → kernel 服務
├─────────────────────────────┤
│ kernel/                     │  與硬體無關：記憶體、console、terminal、shell…
╞═════════════════════════════╡  ← HAL 邊界：hal/*.h + kernel 的裝置介面
│ platform/pc/   drivers/     │  翻譯「硬體細節」
│ arch/x86/                   │
└─────────────────────────────┘
             x86 PC
```

| 名詞 | 意義 |
|---|---|
| **架構（arch）** | CPU 本身：GDT、IDT、例外、分頁表格式、控制暫存器 |
| **平台（platform）** | 「哪一種機器」：開機協定、中斷控制器、系統計時器，以及這台機器有哪些裝置 |
| **驅動程式（driver）** | 一種裝置：VGA、16550 UART、PS/2 鍵盤 |
| **HAL 介面** | `hal/*.h`：kernel 需要、由架構或平台提供的功能 |
| **裝置介面** | `kernel/` 定義、由驅動程式實作的 ops struct（`text_display_t`、`console_sink_t`） |

業界的兩種切法：Windows NT 的 `hal.dll` 抽象「主機板／晶片組」，開機時才選擇實作；Linux 的 `arch/` 抽象「CPU 架構」，編譯時選擇，驅動程式另外放在 `drivers/`。本專案把三者分開（決策 D1）。

## 2. 分層與依賴規則

```
          kernel/  ──────────────► hal/*.h（只有宣告）
             ▲  定義裝置介面            ▲ 實作
             │                         │
          drivers/ ◄── 註冊 ── platform/pc/ ──► arch/x86/
             │                                    ▲
             └──────── port I/O（arch/x86/io）─────┘
```

分界規則：**實作 `hal/` 介面的屬於 platform（或 arch），接到 kernel 子系統的裝置屬於 drivers**。例如 PIC 實作 `hal/irq.h`，所以在 `platform/pc/`；VGA 實作 `text_display_t`，所以在 `drivers/`。

| 目錄 | 可以 include | 不可以 |
|---|---|---|
| `kernel/` | `kernel/`、`hal/` | `arch/`、`platform/`、`drivers/`；`asm`、`inb`/`outb` |
| `hal/` | 只有 `hal/` 與標準 header；只能有 `.h` | 其他一切 |
| `arch/x86/` | `arch/`、`hal/`、`kernel/` 的 `lib`、`memory`、`panic`、`printf`、`test` | `platform/`、`drivers/` |
| `drivers/<裝置>/` | `hal/`、`kernel/` 的裝置介面、`arch/<arch>/io/`、自己的目錄 | `platform/`、其他 `arch/`、其他驅動 |
| `emu/` | `emu/`、`kernel/lib/`、`third_party/musashi/` | `hal/`、`arch/`、`platform/`、`drivers/`、其他 `kernel/` |
| `platform/pc/` | 全部（它負責組裝） | — |
| 全部 | 從專案根目錄寫的路徑；同目錄可用短名稱 | `#include "../` |

`emu/`（68k 模擬器）與機器無關：它只拿到一塊 guest RAM，不知道這塊記憶體怎麼來的；`kernel/` 可以使用它（M3 任務 7 起）。

`platform/` 是唯一知道「這台機器有哪些裝置」的地方：它初始化驅動，並把它們註冊到 kernel。kernel 從不 include 任何驅動。

## 3. 開機流程

```
GRUB ── EAX=0x2BADB002, EBX=mbi ──► _start（platform/pc/boot/boot.asm）
                                      設定堆疊、gdt_init（arch/x86/gdt）
                                      │
                                      ▼
                                   pc_entry(magic, info)（platform/pc/hal_init.c）
                                      只保存兩個值：此時連 console 都沒有
                                      │
                                      ▼
kernel_main()（kernel/main.c）
 ① hal_init_console()   平台註冊 VGA 文字顯示與 serial sink；不可印 log
 ② console_init()       terminal 接上已註冊的顯示裝置
 ③ hal_init_early()     檢查 magic、解析 Multiboot、印出 Multiboot 資訊
 ④ memory_init、pmm_init（用 hal/boot.h）
 ⑤ hal_init()           hal_cpu_init（CPUID、FPU/SSE）→ IDT → 分頁 → 對應開機資料 → framebuffer
                        → ACPI（MADT）→ PCI 列舉 → PIC → PIT（100 Hz）→ PS/2 鍵盤 → TSC 校正
                        → SMP（叫醒其他處理器）→ PS/2 滑鼠 → ATA → PCI 驅動（e1000、HDA、BGA）
    heap_vm_init()      heap 放在專用虛擬位址，需要分頁，之後 kmalloc 才能用
    user_init()         VFS（/ 是 initrd、/tmp、/dev、/hfs）與使用者模式（見 docs/userland.md）
 ⑥ input、terminal input、shell 初始化
 ⑦ hal_cpu_enable_interrupts()
 ⑧ 主迴圈：net_poll、處理 input event，hal_cpu_idle()
```

初始化分成三段，是因為依賴關係本身就有三段：terminal 需要顯示裝置（①在②之前）；PMM 需要開機記憶體表（③在④之前）；分頁需要 PMM（④在⑤之前），heap 需要分頁。順序中隱藏的依賴：

| 順序 | 原因 |
|---|---|
| IDT 在分頁之前 | 分頁設定錯誤時能印出 page fault，而不是 triple fault |
| 對應開機資料在分頁之後 | 用 `hal_mmu_map`，需要 recursive mapping |
| PIC 在 PIT、鍵盤之前 | `pic_init` 會遮蔽所有線，驅動之後才各自 unmask |
| ⑤的驅動在⑥之前沒有問題 | 中斷在⑦才開啟，事件不會在 input buffer 準備好之前進來 |
| ACPI 與 PCI 在 PIC 之前 | 只需要分頁與 `vmap`；PCI 驅動則在最後，因為它們用計時器（`hal_timer_ns`）與中斷控制器 |
| TSC 校正在 PIT 之後、SMP 之前 | 校正用 PIT 通道 2；叫醒其他處理器要靠 `hal_timer_ns` 計時 |

## 4. 介面清單

### HAL 介面（`hal/`）

編譯時綁定：每個架構或平台實作同名函式，實作檔命名為 `hal_<介面>.c`。

| Header | 函式 | 實作 | 說明 |
|---|---|---|---|
| `hal/cpu.h` | `hal_cpu_enable_interrupts`、`hal_cpu_disable_interrupts` | `arch/x86/cpu/hal_cpu.c` | `sti`/`cli`，同時是編譯器屏障 |
| | `hal_cpu_idle` | | `hlt` 一次：睡到下一個中斷 |
| | `hal_cpu_halt` | | `cli` 後永遠 `hlt`（NMI 也會再停） |
| `hal/irq.h` | `hal_irq_register(line, handler)` | `platform/pc/interrupt/hal_irq.c` | 線號超出範圍、NULL、重複註冊回傳 `-1` |
| | `hal_irq_unmask(line)` | | 所有線一開始都遮蔽；8–15 會一併開啟串接線 2 |
| `hal/timer.h` | `hal_timer_ticks`、`hal_timer_frequency` | `platform/pc/timer/hal_timer.c` | `uint32_t` tick；用 `now - start` 計算間隔 |
| `hal/mmu.h` | `hal_mmu_map`、`hal_mmu_unmap`、`hal_mmu_get_phys` | `arch/x86/mmu/hal_mmu.c` | flags 為 `HAL_MMU_WRITABLE`、`HAL_MMU_USER`；未知位元回傳 `-1` |
| `hal/boot.h` | `hal_boot_memory_region(i, &r)` | `platform/pc/boot/hal_boot.c` | 實體記憶體表，型別為 ACPI address range types |
| | `hal_boot_reserved_region(i, &r)` | | 開機資料本身佔用的記憶體（PMM 必須保留）；順序：Multiboot info、記憶體表、module 表，之後每個 module 依序是內容與名稱字串。`hal_init` 會把這些區域唯讀 identity map，所以 module 在分頁開啟後仍可讀 |
| | `hal_boot_module_count()`、`hal_boot_module(i, &m)` | | boot loader 載入的檔案（GRUB `module`、QEMU `-initrd "檔案 名稱,檔案 名稱"`）：位址、長度、名稱字串。shell 指令 `modules` 列出；QEMU 給的名稱是「檔案 名稱」整串 |
| `hal/init.h` | `hal_init_console`、`hal_init_early`、`hal_init` | `platform/pc/hal_init.c` | 見第 3 節 |
| `hal/framebuffer.h` | `hal_framebuffer_get(&fb)` | `platform/pc/video/hal_framebuffer.c` | 圖形模式的線性 framebuffer（位址、寬高、pitch、32 bpp 0x00RRGGBB）。文字模式回傳 -1。圖形模式要靠 Multiboot header 要求（flags bit 2，640x480x32）。GRUB 對 Multiboot 1 只看 header、不理會 `gfxpayload`（實測），所以同一份 kernel 連結兩次：`kernel.elf`（文字模式）與 `kernel-gfx.elf`（`nasm -d GRAPHICS`，header 要求圖形模式），GRUB 選單有兩個項目。圖形模式時 console 畫在 framebuffer 上（`kernel/display/fbcon.c`：8x16 的格子、IBM BIOS 的 8x8 公有領域字型每列畫兩次、VGA 16 色；上方 16 像素是色條，見 `kernel/display/fb_pattern.c`；640x480 = 80x29 格）。QEMU `-kernel kernel-gfx.elf` 不提供 framebuffer（會印 `multiboot knows VBE. we don't`，無害），請用 ISO。檢查：`tools/fb_check.py`（開 ISO 的 graphics 項目：檢查色條，再打 `cpuinfo` 檢查輸出有畫在螢幕上） |
| `hal/framebuffer.h` | `hal_framebuffer_set_mode(w, h)`、`hal_framebuffer_modes(list, max)`、`hal_framebuffer_native(&w, &h)` | `platform/pc/video/hal_framebuffer.c`、`vbe.c` | 執行期換模式：先試 Bochs/QEMU 的 BGA（命令列 `nobga` 跳過），再試 video BIOS 的 VBE（x86emu 執行 int 0x10，只在 BIOS／CSM 開機；UEFI 回傳 -1）。`modes` 列 VBE 的 32 位元模式，`native` 讀 EDID 的偏好解析度（P9，`docs/gpu.md` 第 6 節） |
| `hal/clock.h` | `hal_clock_now(&t)` | `platform/pc/rtc/hal_clock.c` | 目前的 UTC 日期時間。讀 CMOS RTC（index 0x70 / data 0x71，OSDev Wiki "CMOS"）：等 update-in-progress 清除、連讀兩次一致才採用；BCD / 二進位、12 / 24 小時、世紀暫存器（0x32，沒有時 70–99 視為 19xx）的轉換在 `rtc_decode.c`（host 可測）。假設 RTC 存的是 UTC（QEMU 預設）。shell 指令 `date` |

以下是 P6 新增的介面（細節見各自的文件）：

| Header | 函式 | 實作 | 說明 |
|---|---|---|---|
| `hal/cpu.h`（新增） | `hal_cpu_init`、`hal_cpu_info`、`hal_cpu_online_count` | `arch/x86/cpu/hal_cpu.c`、`platform/pc/smp/smp.c` | CPUID 資訊、開啟 FPU/SSE、啟動中的處理器數；見 `docs/cpu.md` |
| `hal/timer.h`（新增） | `hal_timer_ns`、`hal_timer_ns_resolution` | `platform/pc/timer/hal_timer.c` | 奈秒時鐘（TSC，用 PIT 通道 2 校正）；沒有 TSC 時退回 tick |
| `hal/acpi.h` | `hal_acpi_info` | `platform/pc/acpi/hal_acpi.c` | MADT：處理器、local APIC、I/O APIC |
| `hal/mmu.h`（新增旗標） | `HAL_MMU_UNCACHED` | `arch/x86/mmu/paging_entry.c` | PCD+PWT：裝置暫存器用 |
| `hal/mmu.h`（新增函式） | `hal_mmu_protect`、`hal_mmu_get_flags` | `arch/x86/mmu/hal_mmu.c` | 改變已映射頁面的旗標（唯讀、保護頁）、讀回旗標 |
| `hal/usermode.h` | `hal_user_init`、`hal_user_run`、`hal_user_exit`、`hal_user_set_tls` | `arch/x86/user/hal_user.c` | ring 3、系統呼叫、使用者錯誤；見 `docs/userland.md` |
| `hal/pci.h` | `hal_pci_device_count`、`hal_pci_device`、`hal_pci_find*`、`hal_pci_config_read/write`、`hal_pci_enable`、`hal_pci_map_bar`、`hal_pci_probe_drivers`、型別 `hal_pci_driver_t` | `platform/pc/pci/` | PCI 列舉、BAR、驅動比對；見 `docs/pci.md` |
| `hal/dma.h` | `hal_dma_alloc`、`hal_dma_free` | `platform/pc/dma/hal_dma.c` | 實體連續、歸零、有實體位址的記憶體，給裝置 DMA |

`platform/pc/vmap/` 是平台內部的「虛擬位址視窗」（`0xE0000000`–`0xF0000000`），用來映射 ACPI 表、APIC、PCI BAR、DMA 記憶體；不是 HAL 介面。

### kernel 的裝置介面（由驅動程式實作）

執行時綁定（ops struct），因為同一類裝置可能有好幾個，或是根本沒有。

| Header | 型別 | 註冊 | 實作 |
|---|---|---|---|
| `kernel/block/block.h` | `block_device_t`：名稱、區塊大小、區塊數、`ops.read` | `block_register`（最多 8 個） | `drivers/ata`、`kernel/block/ramdisk`；見 `docs/storage.md` |
| `kernel/display/text_display.h` | `text_display_t`：寬、高、`put_cell`、`copy_cell`、`set_cursor`、`clear` | `text_display_register`（只有一個） | `drivers/vga`：`vga_text_display` |
| `kernel/console/sink.h` | `console_sink_t`：`write`、`write_char` | `console_register_sink`（最多 4 個） | `drivers/serial`：`serial_console_sink` |
| `kernel/input/event_buffer.h` | `input_event_push` | —（驅動直接推入） | `drivers/ps2_keyboard`、`drivers/ps2_mouse`（IRQ 12，3 位元組封包 → `INPUT_EVENT_MOUSE`，dx 向右、dy 向下為正，溢位時飽和到 ±255）|

### 跨層的約定函式

名稱固定、由另一層呼叫的函式。asm stub 與 selftest 的三個函式宣告在呼叫端，由下層實作：

| 宣告 | 實作 | 呼叫者 |
|---|---|---|
| `irq_handler(irq)`（`arch/x86/interrupt/irq_stub.h`） | `platform/pc/interrupt/hal_irq.c` | `irq_stub.asm` |
| `kernel_main()`（`kernel/main.h`） | `kernel/main.c` | `pc_entry` |
| `selftest_arch_run`、`selftest_platform_run`、`selftest_platform_exit`（`kernel/test/selftest.h`） | `arch/x86/test/`、`platform/pc/test/` | `selftest_run` |

### P7 新增的介面

| 檔案 | 內容 |
|---|---|
| `hal/mmu.h`（位址空間） | `hal_space_create/switch/destroy/map/copy_page/current`：每個行程一份分頁目錄，kernel 範圍共用（見 `docs/process.md` 第 3 節） |
| `hal/task.h` | `hal_context_init/switch`（kernel 堆疊上的執行緒切換）、`hal_task_set_kernel_stack`、`hal_task_set_tls`、`hal_fpu_save/restore/reset_area` |
| `hal/usermode.h` | 另有 `hal_user_enter`、`hal_user_fork`、`hal_user_regs_get/set` |
| `hal/irq.h` | `hal_irq_count()`：到目前為止的中斷總數（排程器用它判斷有沒有事發生）；`hal_irq_msi_alloc`、`hal_irq_controller`（見 `docs/apic.md`）；線 0–15 是 ISA 線，16–63 給 MSI |
| `hal/pci.h` | 另有 `hal_pci_find_capability`、`hal_pci_enable_msi`（MSI 與 MSI-X） |
| `hal/power.h` | `hal_power_off()`（ACPI soft off：FADT 的 PM1a_CNT 埠＋DSDT 裡 `\\_S5` 的睡眠型態；失敗時 QEMU／Bochs／VirtualBox 的埠）、`hal_power_reboot()`（FADT 的 reset register → 8042 的 0xFE → 埠 0xCF9 → triple fault）。shell 的 `poweroff`、`reboot`。`make test-power`（QEMU：i440fx、Q35、UEFI 各測一次，QEMU 必須結束） |
| `hal/boot.h` | 另有 `hal_boot_cmdline()`（GRUB 的 kernel 命令列，例如 `noapic`） |

### 開機協定：Multiboot 1 與 2

`boot.asm` 同時有兩種 header。BIOS 的 GRUB（`multiboot`）、QEMU `-kernel` 走 Multiboot 1；**UEFI 的 GRUB 用 `multiboot2`**（`grub.cfg` 依 `$grub_platform` 選項目），因為只有 Multiboot2 會給 ACPI RSDP（UEFI 下它不在 0xE0000–0xFFFFF）。`multiboot2_init` 把 tag 清單（記憶體表、module、framebuffer、RSDP）翻成同一個 `multiboot_info_t`，其他程式不需要知道用的是哪一版。UEFI 沒有 VGA 文字模式，所以 UEFI 項目用 `kernel-gfx.elf`（要求 framebuffer），console 只有 fbcon 與 serial；需要 `insmod all_video` 才拿得到 GOP framebuffer。`make test-uefi`（q35 + OVMF）檢查這條路。

## 5. 一個 IRQ 的路徑

```
鍵盤 ── IRQ1 ──► 8259 PIC（IMR bit 1 = 0 才轉送）── 向量 0x21 ──► CPU
                                                                   │
IDT[0x20 + n] = irq_stub_table[n]（arch/x86/interrupt/irq_stub.asm）  ▼
   push n → pusha → irq_handler(n)（platform/pc/interrupt/hal_irq.c）
                       ├─ pic_handle_spurious(n)：假的 IRQ 7/15 → 直接返回
                       ├─ handlers[n]()  → ps2_keyboard_handler → input_event_push
                       └─ pic_send_eoi(n)
```

- **Spurious IRQ**：8259 的請求在 CPU 確認前消失時，仍會送出最低優先的線（master 的 IRQ 7、slave 的 IRQ 15），但該線的 ISR（In-Service Register）位元是 0。`pic_handle_spurious` 用 OCW3 讀 ISR 判斷：假的 IRQ 7 不送 EOI；假的 IRQ 15 只送 EOI 給 master（master 的串接線確實收到了請求）。出處：OSDev Wiki「8259 PIC」Spurious IRQs、8259A datasheet。

- 向量起點 `IRQ_STUB_VECTOR_BASE`（0x20）同時用於 IDT 與 PIC 的 ICW2，兩邊不會不一致。
- 16 條線都有 stub，所以任何一條被 unmask 都不會落到空的 IDT gate。

## 6. 設計決策

### 架構決策（D1–D8）

| # | 項目 | 決定 | 理由 |
|---|---|---|---|
| D1 | 目錄 | `hal/` + `arch/x86/` + `platform/pc/` + `drivers/` | 驅動數量會一直增加，需要固定位置；CPU、機器、裝置是三種不同的變化 |
| D1-b | `hal/` 內容 | 只有 `.h` | 讓 `hal/` 等於「合約」 |
| D2 | 粒度 | 依子系統：cpu、irq、timer、mmu、boot、init | 每個 header 一個關注點；描述 kernel 需要什麼，而不是硬體能做什麼 |
| D3 | 綁定 | HAL 編譯時；裝置介面用 ops struct | 一次只有一種 CPU 與中斷控制器；同類裝置可能有多個或沒有 |
| D4 | 記憶體 | PMM、heap 留在 kernel；分頁搬到 arch，對外是 `hal/mmu.h`；Multiboot 搬到 platform，對外是 `hal/boot.h` | 分頁表格式是 x86 的；開機協定是 PC/GRUB 的 |
| D5 | 中斷 | 只有驅動使用 IRQ 線號；kernel 不碰 IRQ；EOI 在 HAL 內部 | 驅動本來就知道自己的硬體；USB/PCI 的線號是動態的，固定的邏輯名稱撐不久 |
| D6 | include | `-I.`，從根目錄寫起；同目錄用短名稱；禁止 `../` | 路徑唯一、可 grep，邊界檢查得以成立 |
| D7 | 邊界檢查 | `tools/check_boundary.sh`，掛在 `make test` | 規則寫成程式才不會被遺忘 |
| D8 | 邊界上的檔案 | 拆開：terminal / text_display / VGA；keyboard 的 PS/2 部分 / keycode、state、keychar；console / sink / serial；selftest 依層拆分 | 每一份只屬於一層 |

### 實作時的決策

| 項目 | 決定 | 理由 |
|---|---|---|
| `hal_cpu_idle` 與 `hal_cpu_halt` 分開 | `hlt` 有「等一下」與「永遠停住」兩種意圖 | 永遠停住必須先 `cli`，寫在函式裡就不會忘 |
| `"memory"` clobber | `sti`/`cli`/`hlt` 都加 | 成為編譯器屏障；`hlt` 醒來後會重新讀取 handler 改過的變數 |
| 註冊與 unmask 分開 | `hal_irq_register` 後由驅動自己 `hal_irq_unmask` | 驅動可以「註冊 → 初始化裝置 → unmask」 |
| 線號型別 | `unsigned int` | `uint8_t` 會把 256 默默截斷成 0 |
| init 不放進各 HAL 介面 | 只有 `hal/init.h` 的三個階段 | 初始化順序是平台的決定，不應散落在 kernel |
| tick 型別 | `uint32_t` | 32 位元 CPU 讀 64 位元值要兩條指令，中間被中斷會讀到一半新一半舊 |
| 記憶體區域型別 | ACPI address range types（1–5），未知值原樣傳遞 | E820、UEFI、Multiboot 都使用；不是 Multiboot 專屬 |
| 開機資料 | `hal_boot_reserved_region` 與記憶體表分開 | 混入記憶體表會改變 `mmap` 的輸出 |
| kernel 進入點 | `pc_entry` 保存 magic 與 info，`kernel_main(void)` 不帶參數 | kernel 不需要知道 PC 怎麼開機 |
| 顏色 | `color_t` 只是名稱；VGA 驅動用表轉成調色盤數值 | 目前數值剛好相同，但 kernel 不再依賴這個巧合 |
| 沒有顯示裝置 | terminal 什麼都不做 | 支援只有 serial 的 headless 機器 |
| 搬檔 | `git mv` 與修改內容分成兩個 commit | 內容改太多時 git 不會把它認成改名 |

## 7. 邊界檢查

`tools/check_boundary.sh [root]` 掃描 `.c`/`.h`，違規時印出 `boundary: <規則>: 檔案:行:內容` 並回傳 `1`。`make test` 的第一步是 `test-boundary`。

| 規則 | 檢查 |
|---|---|
| `relative_include` | 任何地方的 `#include "../` |
| `kernel_includes_lower` | `kernel/` include `arch/`、`platform/`、`drivers/` |
| `kernel_hardware_access` | `kernel/` 中的 `asm(`、`inb(`、`outb(` 等 |
| `hal_includes` | `hal/` include 其他目錄 |
| `hal_headers_only` | `hal/` 中有 `.h` 以外的檔案 |
| `arch_includes` | `arch/` include `platform/`、`drivers/` |
| `arch_includes_kernel` | `arch/` include `kernel/` 的 lib、memory、panic、printf、test 以外的部分 |
| `driver_includes` | `drivers/` include `platform/`，或 `arch/` 中 `io/` 以外的部分 |
| `driver_includes_driver` | 一個驅動 include 另一個驅動 |

每條規則在 `tests/boundary/<規則>/` 都有一個故意違規的 fixture（不會被編譯）。`make test` 要求每個 fixture 都被**同名的規則**回報，所以 regex 寫錯、永遠通過的規則會被發現。

**新增規則時**：在 `check_boundary.sh` 加一行 `check <規則> '<regex>' '<允許的 regex>' <目錄...>`，並新增 `tests/boundary/<規則>/` fixture。允許的 regex 比對的是 grep 的輸出（`檔案:行:內容`），所以 include 要用 `ALLOWED_INCLUDE` 開頭，不能用 `^`。

## 8. 新增驅動程式

以一個假想的 RTC（即時時鐘）驅動為例：

1. **確定它接到哪個 kernel 介面**。已有的介面能用就用（顯示 → `text_display_t`、文字輸出 → `console_sink_t`、輸入 → `input_event_push`）。沒有的話，在 `kernel/<子系統>/` 定義新的 ops struct 與註冊函式；註冊表與 kernel 端的邏輯不碰硬體，加進 `HOST_LIB_SOURCES` 用 host 測試。
2. **建立 `drivers/rtc/`**：`rtc.c`、`rtc.h`。對外只公開 `rtc_init()` 與它的 ops 物件；handler 等內部函式宣告為 `static`。硬體數值（port、暫存器位元）要註明出處。
3. **需要中斷時**：在 `rtc_init` 中依序「`hal_irq_register(線號, handler)` → 設定裝置 → `hal_irq_unmask(線號)`」。PC 上 16 條線都已經有 IDT stub。
4. **由平台初始化與註冊**：在 `platform/pc/hal_init.c` 適當的階段呼叫 `rtc_init()` 並註冊（console 類在 `hal_init_console`，其他通常在 `hal_init` 的最後）。kernel 不 include 驅動。
5. **測試**：不碰硬體的部分（例如 scancode 轉換）加進 `HOST_LIB_SOURCES` 寫 host 測試；需要硬體的部分寫在 `platform/pc/test/selftest_pc.c`。
6. `make test` 必須通過（邊界檢查會擋下 kernel include 驅動、驅動 include 平台或其他驅動）。

**PCI 裝置的驅動**（網卡、聲音、顯示）不由 `hal_init` 直接呼叫，而是匯出一個 `hal_pci_driver_t`（vendor/device 或 class 比對、`probe`），列進 `platform/pc/pci/pci_drivers.c` 的 `drivers[]`；`probe` 用 `hal_pci_enable`、`hal_pci_map_bar`、`hal_dma_alloc`、`hal_irq_register`。見 `docs/pci.md`，例子是 `drivers/e1000`、`drivers/hda`、`drivers/bga`。

## 9. 新增 HAL 介面或新平台

- **新 HAL 介面**：`hal/<名稱>.h` 只有宣告與常數，說明合約（誰呼叫、何時可用、錯誤時回傳什麼）；實作放在 `arch/<arch>/.../hal_<名稱>.c` 或 `platform/<平台>/.../hal_<名稱>.c`。不要加 init 函式，初始化放進 `hal_init*`。
- **新平台**：新增 `platform/<平台>/`，實作 `hal/irq.h`、`hal/timer.h`、`hal/boot.h`、`hal/init.h`、自己的進入點與 `selftest_platform_*`。目前 Makefile 會編譯 `platform/` 下所有檔案，所以真的有第二個平台時，要先讓 Makefile 只選一個平台（以及一個架構）。

## 10. 已接受的限制

| 限制 | 說明 |
|---|---|
| 只有一個平台與一個架構 | Makefile 編譯 `arch/`、`platform/` 下的全部檔案；有第二個時再加選擇機制 |
| 驅動直接使用 port I/O | `drivers/` 可以 include `arch/<arch>/io/`；換到 MMIO 的機器時，需要 HAL 提供 I/O 存取函式 |
| 只能在 QEMU 中間接測試 spurious IRQ | 無法讓 QEMU 產生真的 spurious IRQ；selftest 在中斷處理之外呼叫 `pic_handle_spurious`（此時 ISR 全為 0），驗證判斷與計數 |
| 沒有 `hal_irq_mask`、handler 沒有 context 指標 | 目前沒有需要的呼叫者 |
| `hal_cpu_halt` 沒有自動測試 | 不會返回；只能透過 panic 路徑間接涵蓋 |
| 驅動的中斷 handler 在中斷關閉時執行 | handler 要短；耗時的工作交給主迴圈（目前鍵盤只推入 input event） |

## 11. 測試

- **host**（`tests/host/`）：`test_console_sink`（註冊與廣播）、`test_terminal`（用假的顯示裝置測換行、捲動、退格、顏色、headless）、`test_ps2_keyboard`（scancode 與 keymap）、`test_ps2_mouse`（封包解碼）、`test_multiboot`（含 `hal_boot_*`）、`test_paging_entry`（含 `paging_flags_from_hal`）。
- **QEMU selftest**：
  - `kernel/test/selftest.c`：框架、heap、`hal_timer_ticks`、`hal_cpu_idle`
  - `arch/x86/test/selftest_x86.c`：EFLAGS.IF、`cpu_irq_save`/`cpu_irq_restore`、CR0、分頁、`hal_mmu`、例外、IDT gate、IRQ stub 線號
  - `platform/pc/test/selftest_pc.c`：開機後 IMR 為 `0xFC`/`0xFF`、`hal_irq_*`、spurious IRQ 判斷、IMR 修改保留中斷旗標、100 Hz、開機資料已對應、QEMU 結束 port
- **行為不變的驗證**：`tools/boot_log.py` 在 QEMU 中開機、輸入一組 shell 指令，印出 serial log 與每個指令後的 VGA 畫面（隨 kernel 大小變動的數值會被遮蔽）。重構前後各存一份再 `diff`。它不在 `make test` 中，需要 `python3`。

## 12. 之後的工作

- 第二個平台或架構時：Makefile 的平台／架構選擇、驅動的 I/O 存取抽象化。
- console sink 目前只能註冊不能移除。
- `kernel/terminal/terminal.c` 的 `terminal_scroll` 沒有宣告在 header，但也不是 `static`。
- `drivers/vga` 的 `vga_init`、`vga_get_cell` 沒有使用者。

## 11. x86-64（P8）

kernel 預設是 64 位元（`make`），32 位元用 `make ARCH=i386`。`hal/` 的介面沒有改（位址用 `uintptr_t`）；新增 `hal_phys_to_virt()`（32 位元 identity、64 位元 HHDM）與 `HAL_MMU_WRITE_COMBINING`。64 位元專屬的程式碼在 `arch/x86_64/`（GDT、IDT、分頁、context switch、使用者模式）與 `platform/pc/boot/boot64.asm`；`arch/x86/` 保留 32 位元專用與兩者共用的部分（`cpu`、`io`），Makefile 依 `ARCH` 排除另一邊。位址空間、開機流程與限制見 `docs/x86_64.md`。
