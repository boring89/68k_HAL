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
make test       # test-host + test-qemu，修改後必須通過
make test-host  # host 單元測試（tests/host/，含 ASan/UBSan）
make test-qemu  # 以 -DKERNEL_SELFTEST 編譯 kernel，在 QEMU 中執行 kernel/test/selftest.c
make clean
```

- 需要的套件：見 README.md 的 Building 一節。
- 尚未建立：cross compiler（i686-elf-gcc）。

### 測試

- 不依賴硬體的模組（`kernel/lib`、`kernel/shell/parser.c`、`emu/m68k`）放在 `tests/host/test_<模組>.c`，使用 `tests/host/test.h` 的巨集。新增可在 host 編譯的模組時，要加入 Makefile 的 `HOST_LIB_SOURCES`。
- 需要硬體或中斷的測試放在 `kernel/test/selftest.c`，用 `SELFTEST_RUN` 註冊。

## 目錄結構

```
boot/          Multiboot 進入點（boot.asm）、GDT
arch/x86/      x86 硬體驅動：vga、serial、io（port I/O）、cpu（控制暫存器、invlpg）
kernel/        核心：main.c、interrupt、panic、memory、keyboard、input、terminal、shell、console、log、printf、lib
emu/m68k/      68k CPU 模擬器（cpu、decoder、effective_address、execute、memory）
tests/host/    host 單元測試
docs/          設計文件
tools/         開發工具
```

- 資料流：IRQ → keyboard → `input_event` ring buffer → 主迴圈 → terminal → shell
- Makefile 會自動搜尋 `boot hal kernel arch` 下的 `.c` 與 `.asm`，新增檔案不需修改 Makefile。`emu/` 不編進 kernel，只透過 `HOST_LIB_SOURCES` 在 host 測試中編譯。
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
- 不要加入任何 Apple ROM、System Software 或其他受版權保護的內容。

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
- `kernel/memory/paging`：分頁（已完成，見 `docs/paging.md`）

（之後新增的元件加入此清單）

## 已知問題（尚未修正）

- heap 大小固定為 1 MiB（開機時由 `pmm_alloc_frames` 提供），用完不會自動擴充（見 `docs/pmm.md` 第 8 節）。
- `emu/m68k` 的 MOVE 和 ADDI 是寫死的（固定立即值存入 D0），沒有 CCR 旗標。
