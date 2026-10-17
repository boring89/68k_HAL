# CLAUDE.md

68k_HAL：在 x86（32-bit 保護模式）上執行的實驗性 OS 核心，長期目標是讓 Classic Mac OS 的架構在現代硬體上延續（參見 README.md）。目前為早期開發階段。

## 回應規則

- 使用繁體中文回應；程式碼、註解、commit message 使用英文。
- 回應保持精簡：說明做了什麼、為什麼，不重複貼出整段程式碼。

## 建置與執行

```bash
make            # 產生 build/kernel.elf
make iso        # 產生 build/68k_HAL.iso（需要 grub-mkrescue）
make run        # 在 QEMU 中開機，serial 輸出到 stdio
make test       # test-boundary + test-host + test-qemu + test-shell + test-video，修改後必須通過（第一次會下載 musl、Lua、TinyCC 的原始碼，需要網路）
make test-host  # host 單元測試（tests/host/，含 ASan/UBSan）
make test-qemu  # 以 -DKERNEL_SELFTEST 編譯 kernel，在 QEMU 中執行 kernel/test/selftest.c
make test-sst   # SingleStepTests 68000 測資（tests/data/sst/，需自行下載，不在 make test 中；見 docs/m68k.md）
make test-rom   # 在 host 上開機使用者的 Mac IIci ROM（變數 MAC_ROM、MAC_DISK；沒有檔案時跳過；不在 make test 中；見 docs/mac.md）
make test-rom-qemu  # 同一台 Mac 在 kernel（QEMU）裡跑，畫面 CRC 必須與 host 相同（本機專用）
make mac-screenshot # 圖形模式 QEMU 的畫面存成 build/mac/qemu_screen.ppm（本機專用）
make run-mac     # 在 QEMU 視窗中互動執行 Mac（輸入 mac + Enter；本機專用）
make sdk        # C SDK（musl 靜態 i386）→ build/sdk；`tools/68khal-cc` 是 gcc 的包裝；見 docs/userland.md
make initrd     # userland/ 的範例程式、Lua、tcc、SQLite、Kilo 與 box 工具箱打包成 build/initrd.tar（程式光碟，GRUB module 名稱 initrd）
make test-shell # 開機 ISO、用 QEMU monitor 打字執行 shell 指令並檢查 serial 輸出（tools/shell_test.py）
make test-video # 設定 1280x960 並檢查 QEMU screendump
make test-usb    # USB 鍵盤滑鼠經 xHCI（關掉 PS/2，含熱插拔）
make test-noapic # selftest 帶 "noapic"（8259 路徑）；make test-q35：q35 晶片組的 APIC 與網路
make test-uefi   # 同一個 ISO 用 UEFI 韌體（OVMF、q35）開機：Multiboot2、ACPI RSDP、fbcon（需要 ovmf 套件；不在 make test 中）
make test-fb      # GRUB 的 640x480 圖形模式：色條、framebuffer 文字終端、打指令看輸出（tools/fb_check.py；擋住「驅動啟動時把螢幕關掉」這類退步）
make test-mac-keys # Alt-N（= Command-N）在 Mac 建立資料夾（本機專用）
make test-net / test-virtio-net  # TCP（host 的 http.server 經 slirp 10.0.2.2）、socket、DNS；virtio-net 網卡
make test-mac-sound / test-mac-clock / test-mac-persist  # Mac 的聲音、時鐘、ATA 寫入（本機專用，需要 ROM 與磁碟）
make clean
```

- 需要的套件：見 README.md 的 Building 一節（含 `binutils-m68k-linux-gnu`，組譯 `tests/m68k/*.s`；`python3` 與 `hfsutils`，產生 `build/images/` 的測試磁碟映像；`curl`，下載 musl、Lua、TinyCC）。下載的原始碼放在 `third_party/downloads/`（gitignore，不進 repo）。
- `make` 同時產生 `build/kernel.elf`（文字模式）與 `build/kernel-gfx.elf`（Multiboot header 要求 640x480x32 framebuffer；ISO 兩者都放，GRUB 選單兩個項目）。
- 尚未建立：cross compiler（i686-elf-gcc）。
- CI：`.github/workflows/test.yml` 在 push 到 main 與 PR 時執行 `make test` 與 `make iso`。

### 測試

- 不依賴硬體的模組（例如 `kernel/lib`、`kernel/terminal/terminal.c`、`kernel/console/sink.c`、`platform/pc/boot/multiboot.c`、`emu/m68k`；完整清單見 Makefile 的 `HOST_LIB_SOURCES`）放在 `tests/host/test_<模組>.c`，使用 `tests/host/test.h` 的巨集。新增可在 host 編譯的模組時，要加入 `HOST_LIB_SOURCES`。
- 需要硬體或中斷的測試依所屬的層放在 `kernel/test/selftest.c`、`arch/x86/test/selftest_x86.c` 或 `platform/pc/test/selftest_pc.c`，用 `SELFTEST_ASSERT` 檢查、`SELFTEST_RUN` 執行（`kernel/test/selftest.h`）。
- `make test` 第一步是 `tools/check_boundary.sh`（層之間的 include 規則，見 `docs/hal.md` 第 7 節）。新增規則時要加上 `tests/boundary/<規則>/` fixture。
- 磁碟映像（`build/images/*.img`）由 `tools/make_apm_image.py`、`tools/make_hfs_image.sh`、`tools/make_disk_image.py` 產生，內容全部自己合成；用法與 hfsutils 的 `~/.hcwd` 注意事項見 `docs/hfs.md`。
- 圖形模式用 `tools/fb_check.py`（需 grub-mkrescue，不在 `make test` 中）檢查 framebuffer（色條與文字終端）。
- 需要 QEMU 額外裝置的驗證（`-smp 4`、e1000、HDA、BGA）直接加在 `QEMU_TEST_FLAGS` 或 `test-shell`／`test-video`。shell 的輸出會鏡射到 serial（`terminal_set_mirror`），所以 `tools/shell_test.py` 能檢查任何指令。
- 使用者程式的測試：`userland/*.c` 與 `userland/expected/*.txt`（預期輸出：在 host 上用 `unshare -Ur chroot` 執行同一個檔案得到）；`kernel/test/selftest.c` 的 `user_tests` 在 OS 內執行並比對。
- 重構時用 `tools/boot_log.py` 比較前後的開機 log 與 VGA 畫面（需要 `python3`，不在 `make test` 中）。

## 目錄結構

```
hal/           HAL 介面（只有 .h）：cpu、irq、timer、mmu（含位址空間）、task（context 切換）、usermode、boot（含 module）、framebuffer、clock、init、acpi、pci、dma、usermode
arch/x86/      CPU：cpu（hal_cpu、CPUID、控制暫存器）、gdt（含 TSS）、interrupt（IDT、例外、IRQ stub）、mmu（分頁、hal_mmu、space = 每個行程一份分頁目錄）、task（context 切換、FPU）、io（port I/O）、user（ring 3、int 0x80）、test
platform/pc/   PC 平台：boot（Multiboot 進入點、hal_boot）、video（framebuffer、BGA 模式設定）、rtc（CMOS 時鐘）、interrupt（8259 PIC、local APIC、I/O APIC、hal_irq；見 docs/apic.md）、timer（PIT、TSC、hal_timer）、acpi、smp（INIT-SIPI-SIPI）、pci（列舉、驅動清單）、dma、vmap（0xE0000000 視窗）、hal_init.c、test
drivers/       裝置驅動：vga、serial、ps2_keyboard、ps2_mouse、ata（讀寫，見 docs/storage.md）、ahci（SATA）、e1000 與 virtio_net（網卡）、hda（Intel HD Audio）、bga（Bochs VBE 顯示）、usb_host（列舉 USB 控制器）、xhci（xHCI 控制器、HID 鍵盤滑鼠）
kernel/        與硬體無關的核心：main.c、panic、memory（pmm、heap）、block（區塊裝置、ramdisk）、fs（apm、hfs 唯讀、volume；見 docs/hfs.md）、display（含 fb_scale、fbcon 圖形模式文字終端）、console、terminal（含 mirror、ANSI 解析器 ansi）、keyboard（keycode、state、keychar、keyseq 按鍵序列）、input、shell、log、printf、lib、m68k（guest RAM 與示範）、mac（在 kernel 裡執行 Mac；見 docs/mac.md）、user（ELF 載入器、系統呼叫、console；見 docs/userland.md）、vfs（ramfs、tarfs、hfsfs、devfs 的 console；見 docs/userland.md）、net（packet 純函式、tcp、net 堆疊；見 docs/net.md）、usb（控制器註冊表；見 docs/usb.md）、sound（見 docs/sound.md）、pci（名稱表；見 docs/pci.md）、test
emu/m68k/      68k 虛擬機：Musashi 的封裝層（vm：含可選的 bus 與逐指令 hook）、A-line trap 分派（trap）、Musashi 設定（m68kconf.h）；freestanding/ 是 kernel 建置用的 libc 替身
emu/mac/       Mac IIci 機器模型（M-P3，見 docs/mac.md）：記憶體配置與模擬 opcode（mac.c）、ROM 修補（rom_patch.c）、VIA（via.c）、RBV/DAC/視訊（video.c）、IWM、SCSI、.Disk 驅動（disk.c、stub.s）；與硬體無關，檔案由使用者提供，不在 repo
third_party/   第三方原始碼，不修改：musashi（68k CPU 核心，MIT）；downloads/ 是下載的 musl、Lua、TinyCC、Haiku（gitignore）
sdk/           C SDK 的執行期輔助（rt/divdi3.c：32 位元的 64 位元除法，因為 host 沒有 32 位元 libgcc）
userland/      範例使用者程式（C，用 tools/68khal-cc 編譯）、data/（程式讀的檔案）、expected/（預期輸出）
tests/host/    host 單元測試
docs/          設計文件
tools/         開發工具
```

- 分層與依賴規則、開機流程、介面清單、新增驅動程式的方式：見 `docs/hal.md`。
- 資料流：IRQ → `drivers/ps2_keyboard` → `input_event` ring buffer → 主迴圈 → terminal → shell
- Makefile 會自動搜尋 `arch drivers emu hal kernel platform` 下的 `.c` 與 `.asm`，新增檔案不需修改 Makefile。`emu/m68k/*.c`（不含子目錄）同時進 host 測試（`HOST_LIB_SOURCES`）；`emu/m68k/freestanding/` 只編進 kernel。`third_party/musashi` 由 Makefile 的 Musashi 區塊另外編譯（`m68kmake` 產生 `m68kops.c`，host 與 kernel 各編一份）。
- 程式碼在 freestanding 環境執行：沒有 libc，只能使用 `<stdint.h>`、`<stdbool.h>`、`<stdarg.h>`、`<stddef.h>`。字串與記憶體函式使用 `kernel/lib/`（`k_strcmp`、`k_memcpy` 等）。

## 程式風格

- 每個新檔案開頭加上授權標頭：
  ```c
  /*
   * Copyright (C) 2026 YuanChi Hsieh
   * SPDX-License-Identifier: GPL-3.0-or-later
   */
  ```
  ASM 檔使用 `;` 註解的相同內容。
- 縮排 4 個空格，大括號換行（Allman 風格）。
- 函式與型別以模組名稱為前綴：`heap_init`、`m68k_cpu_step`、`input_event_t`。
- Header guard 依路徑命名，例如 `KERNEL_MEMORY_HEAP_H`、`EMU_M68K_CPU_H`。
- include 路徑：跨目錄一律從專案根目錄寫起（`#include "kernel/lib/k_string.h"`，Makefile 有 `-I.`）；同目錄用短名稱（`#include "pmm.h"`）；禁止 `../`。
- 分層：`kernel/` 只能 include `kernel/` 與 `hal/`，不可有 inline asm 或 port I/O；硬體相關的程式碼放在 `arch/`、`platform/` 或 `drivers/`（規則見 `docs/hal.md` 第 2 節，由 `make test` 檢查）。
- 實作 `hal/<X>.h` 的檔案命名為 `hal_<X>.c`。
- 回傳值慣例：成功 `0`，失敗 `-1`。
- 模組內部函式宣告為 `static`。
- 與既有程式碼保持一致；不要只為了風格而修改無關的程式碼。

## Commit 格式

`<模組路徑>: <小寫開頭的簡短描述>`，例如：

```
kernel/shell: add command argument support
emu/m68k: add CCR flag handling
```

## 工作規則

- 直接在 `main` 上開發，**不建立 branch 或 PR**（除非使用者要求）。一個任務對應一個小 commit。
- **不要自行 commit 或 push**：完成並通過測試後提出 commit 訊息，使用者審核 diff 確認後才執行。
- **不要猜使用者的想法，有問題就問**：需求不明確、有多種合理解讀，或需要使用者做決定時，先提問再動手。不要自行補上使用者沒說的需求。
- 一次只做被要求的任務；發現其他問題時列出來，不要順便修改。
- 涉及多個檔案或架構的變更，先提出計畫，等使用者確認後再實作。
- 修改後必須執行 `make test` 並通過；新功能要附上測試。
- 硬體相關的數值（I/O port、descriptor 位元、68k 指令編碼）要註明出處（Intel SDM、OSDev Wiki、M68000 Programmer's Reference Manual），不確定時明說。
- 中斷處理程式與主迴圈共用的變數必須宣告 `volatile`。
- 無法恢復的錯誤使用 `kernel/panic/panic.h` 的 `panic()` / `KASSERT()`。
- 下載的原始碼（`third_party/downloads/`）總量上限 5 GB，只從官方來源下載並驗證 SHA-256；移植進 repo 的外部程式碼保留原授權聲明；不引入 GPL-2.0-only 的程式碼。
- 不要加入任何 Apple ROM、System Software 或其他受版權保護的內容。使用者的 ROM 與磁碟映像（`~/mac/`）只能讀取，不可複製進 repo、不可 commit、不可進 `make test` 或 CI；需要它們的驗證只放在 `make test-rom` 等本機目標。

## 開發模式

依任務性質分成兩種模式。不確定屬於哪一種時，先問使用者。

### 修正模式：既有程式碼的 bug 修正與整理

Claude 依照上方工作規則直接實作。

### 協作模式：新元件（清單見下方）

使用者負責架構決策，Claude 負責說明原理、列出決策點並實作。使用者不熟悉這些元件的底層知識，所以**理解優先於進度**。

1. **概念**：先說明這個元件要解決什麼問題、需要哪些背景知識。
2. **決策點**：每一步列出需要決定的事項（選項、取捨、Claude 的建議與理由、潛在問題），由使用者決定；不要替使用者決定架構。
3. **一次一個函式**：使用者決定後由 Claude 實作，並說明：
   - 用途：它在整個元件中負責什麼、誰會呼叫它
   - 原理：背後的硬體或演算法原理，附上出處（Intel SDM 章節、OSDev Wiki 頁面）
   - 關鍵的程式碼行在做什麼
4. **確認**：使用者理解並確認後，才進行下一個函式。不要一次寫完整個元件。
5. **測試**：每個函式都要有對應的測試，並且通過 `make test`。
6. **紀錄**：元件完成後，把原理與設計決策整理到 `docs/<元件名稱>.md`，作為之後查閱的知識庫。

解釋方式：先用比喻再說明實際運作；專有名詞第一次出現時要解釋；多用 ASCII 圖和本專案的實際數字；一次一個概念，保持簡短。

協作模式的元件：
- `kernel/memory/pmm`：實體記憶體管理（已完成，見 `docs/pmm.md`）
- `arch/x86/mmu/paging`：分頁（已完成，見 `docs/paging.md`）
- HAL：硬體抽象層的邊界（已完成，見 `docs/hal.md`）
- `emu/m68k`：以 Musashi 為基礎的 68k 虛擬機（M3，已完成，見 `docs/m68k.md`）。注意：這個元件是由使用者睡覺時的自動排程一次做完的，沒有走協作模式的逐步確認，使用者尚未審核；之後修改時請先帶使用者過一遍 `docs/m68k.md`

- `emu/mac`：Mac IIci 機器模型與 `kernel/mac`（P3，見 `docs/mac.md`）。同樣是由自動排程一次做完的，沒有走協作模式的逐步確認，使用者尚未審核；之後修改時請先帶使用者過一遍 `docs/mac.md`

- P7（自動排程一次做完，**使用者尚未審核**；之後修改前請先帶使用者過一遍對應的文件）：Multiboot2 與 UEFI 開機（`docs/hal.md`）、行程與排程與訊號與管線（`docs/process.md`）、APIC 與 MSI（`docs/apic.md`）、xHCI 與 USB 鍵盤滑鼠（`docs/usb.md`）。P7 的規劃與紀錄在 `P7_PLAN.md`（不進 git）。
- P6（自動排程一次做完，**使用者尚未審核**；之後修改前請先帶使用者過一遍對應的文件）：CPU（`docs/cpu.md`）、使用者模式與 C SDK 與 VFS（`docs/userland.md`）、PCI 與驅動框架（`docs/pci.md`）、網路（`docs/net.md`）、聲音（`docs/sound.md`）、顯示（`docs/gpu.md`）、Mac 的聲音／時鐘／磁碟寫入（`docs/mac.md` 第 11、12 節）。P6 的規劃與紀錄在 `P6_PLAN.md`（不進 git）。

（之後新增的元件加入此清單）

## 已知問題（尚未修正）

- 鍵盤：US 配置；已有 Ctrl（Ctrl-字母 = 控制字元，Ctrl-D = EOF）、Alt、Caps Lock、方向鍵、F1–F12、導覽鍵（`0xE0` 擴充碼），但沒有數字鍵台（keypad）、其他配置、鍵盤 LED、按住重複。
- `emu/mac`：鍵盤與滑鼠以封包注入（Ctrl = Control、Alt = Command、Win/AltGr = Option）、聲音只有 ASC 的 FIFO 模式、沒有序列埠與網路；只支援 ROM `368CADFE`（Mac IIci）；其餘見 `docs/mac.md` 第 10 節。
- 滑鼠事件（`INPUT_EVENT_MOUSE`）進入 input buffer 但主迴圈忽略。
- HFS 名稱比較只對 ASCII 不分大小寫，查詢是線性掃描（見 `docs/hfs.md`）。
- ATA 驅動只支援 primary master、LBA28、輪詢（可讀寫，寫入後 FLUSH CACHE）。
- 使用者程式：協作式多工（fork／execve／wait／pipe／訊號，最多 64 個行程；CPU 密集的程式不會讓出 CPU；fork 複製整個位址空間；沒有執行緒、alarm、作業控制，見 `docs/process.md` 第 9 節）；終端機沒有 UTF-8；沒有 NX（可讀即可執行）；堆疊固定 2 MiB。見 `docs/userland.md` 第 7 節。
- 網路：TCP 沒有亂序重組、壅塞控制、選擇性確認；沒有 IP 分片、IPv6；一次只用一張網卡；HDA 只有輸出；BGA 只有 32 bpp；AHCI 只用輪詢與一個命令槽；USB：xHCI 的 HID 鍵盤滑鼠可用（沒有 hub 與隨身碟），其餘控制器只列出（見 `docs/usb.md`）；中斷由 I/O APIC + local APIC 處理（沒有 APIC 或命令列有 `noapic` 時用 8259，見 `docs/apic.md`），其他處理器只回報後 `hlt`（沒有 per-CPU 資料）。
- `vmap`（`0xE0000000`–`0xF0000000`）與 `hal_dma_free` 的虛擬位址不重用。
- QEMU 的 HDA 透過 CORB/RIRB 只回應第一個命令，驅動改用立即命令暫存器（原因未查明，見 `docs/sound.md`）。
- 開機時的 identity map 只有前 4 MiB（kernel 結尾約 0x225000）：kernel 超過 4 MiB 會在 `paging_init` panic。heap 已搬到專用虛擬位址，不佔這 4 MiB。
- heap 不會歸還記憶體：用完會自動擴充（`heap_vm_init`，每次至少 64 KiB，最多到 `HEAP_VIRT_LIMIT`），但擴充後 frame 不會還給 PMM。
- `emu/m68k`（Musashi）的限制：guest 視為沒有 FPU 的 68030（F-line 指令在 `m68k_vm_instr_hook` 轉成 vector 11 例外，不進入 Musashi 的 FPU）；SingleStepTests 中 address error 的測試全部不同（Musashi 沒有 prefetch queue，決定不補，見 `docs/m68k.md` 第 8 節）；其餘差異見 `docs/m68k.md`。guest RAM 預設 8 MiB（`m68k_guest_set_size` 可改），映射在虛擬位址 `0x10000000`。
