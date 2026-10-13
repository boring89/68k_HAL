# 分頁（Paging）

`kernel/memory/paging.c`、`kernel/memory/paging.h`、`kernel/memory/paging_entry.c`、`kernel/memory/paging_entry.h`、`arch/x86/cpu/cpu.h`

分頁讓 CPU 在每一次記憶體存取時，把程式使用的**虛擬位址**翻譯成 RAM 上的**實體位址**。翻譯規則寫在 kernel 維護的分頁表裡，由 CPU 的 MMU 自動查詢。目前的用途是抓 bug（null pointer、存取未對應的位址會立刻 page fault），之後是 heap 擴充、記憶體保護與多個位址空間的基礎。

## 1. 基本概念

### 虛擬位址與實體位址

比喻：住戶信上寫「101 號房」（虛擬位址），管理員查對照簿（分頁表）才知道實際在 3 樓東側（實體位址）。住戶不需要知道房間實際在哪，管理員可以隨時調整。

| 名詞 | 意義 |
|---|---|
| **虛擬位址** | 程式看到、使用的位址 |
| **實體位址** | RAM 晶片上的位址（PMM 管理的就是它） |
| **MMU** | CPU 內負責翻譯的硬體 |
| **分頁表** | kernel 寫好、MMU 查閱的對照表 |
| **TLB** | CPU 內的翻譯快取。修改分頁表後必須用 `invlpg` 通知 CPU 丟掉舊結果 |

好處：隔離（每個程式一套表）、保護（唯讀、只限 kernel）、連續的假象（散落的 frame 對應成連續的虛擬位址）、自由擺放（kernel 的虛擬位址可以和實體位址無關）。

（出處：Intel SDM Vol. 3A, Chapter 4 "Paging"；OSDev Wiki "Paging"）

### 兩層分頁表

32-bit 虛擬位址切成三段（Intel SDM Vol. 3A §4.3, Figure 4-2）：

```
 31          22 21          12 11              0
┌──────────────┬──────────────┬─────────────────┐
│  dir index   │ table index  │     offset      │
│   10 bits    │   10 bits    │     12 bits     │
└──────────────┴──────────────┴─────────────────┘
```

| 名稱 | 大小 | 內容 | 一格管多大 |
|---|---|---|---|
| page directory | 4 KiB = 1024 格 | 每格（PDE）指向一張 page table | 4 MiB |
| page table | 4 KiB = 1024 格 | 每格（PTE）指向一個 4 KiB frame | 4 KiB |

起點是 **CR3**：存放 page directory 的實體位址。表格裡存的都是**實體位址**。

例：VGA 緩衝區 `0xB8000`

```
0xB8000 = 0000000000 0010111000 000000000000
          └ dir=0 ┘  └tbl=184┘  └offset=0 ┘

CR3 → directory[0] → page table 0 → [184] → frame 0xB8000 + 0 = 0xB8000
```

兩層的好處：沒用到的 4 MiB 區域，PDE 標「不存在」即可，不必配置 page table。

### 表項格式

PDE 和 PTE 都是 32 bits（Intel SDM Vol. 3A §4.3, Table 4-5、Table 4-6）：

```
 31                              12 11  9 8 7 6 5 4 3 2 1 0
┌──────────────────────────────────┬─────┬─┬─┬─┬─┬─┬─┬─┬─┬─┐
│  frame 位址的高 20 bits           │ AVL │G│*│D│A│C│W│U│R│P│
└──────────────────────────────────┴─────┴─┴─┴─┴─┴─┴─┴─┴─┴─┘
                                    * PTE: PAT / PDE: PS
```

frame 一定 4 KiB 對齊，所以位址的低 12 bits 永遠是 0，空出來放旗標。

| bit | 名稱 | 意義 | 本專案 |
|---|---|---|---|
| 0 | P | 0 = 不存在，存取就 page fault | `PAGING_PRESENT` |
| 1 | R/W | 0 = 唯讀 | `PAGING_WRITABLE` |
| 2 | U/S | 0 = 只限 ring 0 | `PAGING_USER`（目前不用） |
| 5 | A | CPU 存取過時自動設 1 | 只讀 |
| 6 | D | CPU 寫入過時自動設 1（只在 PTE） | 只讀 |
| 3、4、7、8 | PWT、PCD、PS/PAT、G | 快取、大頁、全域頁 | 設 0 |
| 9–11 | AVL | 留給 OS | 未使用 |

兩個細節：

1. PDE 和 PTE 的權限**取較嚴格的**，所以 PDE 設寬鬆（P | RW），限制放在 PTE。
2. CR0.WP（bit 16）= 0 時，ring 0 可以寫入唯讀頁（Intel SDM Vol. 3A §2.5）。目前 WP = 0，所以 R/W 對 kernel 沒有效果。

## 2. 開啟分頁

```
mov cr3, <directory 的實體位址>
mov cr0, <CR0 | 0x80000000>       ; CR0.PG = 1，下一條指令開始翻譯
```

設定 PG 之後的**下一條指令**就要經過翻譯。如果正在執行的程式碼沒有對應：

```
取指令 → page fault → 跳 handler（也沒對應）→ double fault → 再失敗 → triple fault → CPU 重置
```

沒有任何錯誤訊息。所以開啟前必須對應好：kernel 程式碼與資料（含 GDT、IDT、16 KiB 堆疊、128 KiB PMM bitmap，都在 `.bss`/`.data` 內）、heap、VGA。port I/O（serial、PIC、鍵盤）不經過記憶體位址，不受影響。

（出處：Intel SDM Vol. 3A §2.5、§4.1.2、§6.15；OSDev Wiki "Triple Fault"）

`paging_init` 的流程：

```
① KASSERT：kernel_end ≤ 4 MiB、heap 結尾 ≤ 4 MiB      開啟前先檢查，失敗時還能印訊息
② pmm_alloc_frame × 2                                directory、page table 0
③ k_memset 清 0                                      PG = 0，可直接用實體位址
④ table[1..1023] = identity；table[0] 保持 0          第 0 頁不對應
⑤ directory[0] → table；directory[1023] → directory   recursive mapping
⑥ cpu_write_cr3(directory)
⑦ cpu_write_cr0(CR0 | PG)
⑧ paging_map_multiboot()                             需要 recursive mapping，所以在最後
```

PMM 給的 frame 不清 0（見 `docs/pmm.md`），內容是垃圾；垃圾中 P = 1 的項目會變成隨機的對應，靜默地寫壞記憶體。清 0 後全部是「不存在」，是安全的起點。

早期 Intel 手冊建議設定 PG 後接一個 `jmp`。identity map 下前後的指令位址相同，實務上不需要（OSDev Wiki 的範例也沒有），QEMU 上驗證正常。

## 3. 開啟後的位址空間（QEMU，128 MiB）

QEMU monitor `info mem` 的輸出（`qemu-system-i386 -kernel`）：

```
0000000000001000-0000000000400000 00000000003ff000 -rw
00000000ffc00000-00000000ffc01000 0000000000001000 -rw
00000000fffff000-0000000100000000 0000000000001000 -rw
```

```
虛擬位址                                         實體位址
0x00000000 ┌──────────────────────────┐
           │ 第 0 頁：不對應           │          （null pointer → page fault）
0x00001000 ├──────────────────────────┤ ┐
           │ Multiboot 資訊（0x9500）  │ │
           │ VGA（0xB8000）            │ │ identity
           │ kernel（0x100000～）       │ │ 虛擬 = 實體
           │ heap（0x22A000～0x32A000）│ │
           │ directory（0x32A000）     │ │
           │ page table 0（0x32B000）  │ │
0x00400000 ├──────────────────────────┤ ┘
           │ 未對應，paging_map 使用    │
0xFFC00000 ├──────────────────────────┤ ┐
           │ page table 0 的 recursive  │ │ 保留區
           │ 視窗（其餘依需要出現）       │ │ paging_map/unmap 拒絕
0xFFFFF000 │ directory 本身            │ │
0xFFFFFFFF └──────────────────────────┘ ┘
```

`xp` 讀出的實體內容：

```
0x0032A000（directory[0]）    = 0x0032B023   → page table 0，P | RW | A
0x0032AFFC（directory[1023]） = 0x0032A023   → 指向自己，P | RW | A
```

`0x20`（A）是 CPU 存取過後自己設的，`paging_entry_get_phys` 會忽略它。

## 4. Recursive mapping

**問題**：開啟分頁後，kernel 要修改分頁表時也只能用虛擬位址，而新建的 page table 可能在 4 MiB 以外，沒有對應。

**做法**：`directory[1023]` 指向 directory 自己。

```
存取 0xFFC00000 + N × 4096（dir 1023, table N）

CR3 → directory[1023] → directory      ← MMU 以為是 page table
    → directory[N]    → page table N   ← MMU 以為是資料頁
```

- 第 N 張 page table 出現在 `0xFFC00000 + N × 4096`（`paging_table_virt(N)`）
- 第 1023 張繞兩圈，就是 directory 本身：`0xFFFFF000`（`PAGING_DIRECTORY_VIRT`）
- `0xB8000` 的 PTE 在 `0xFFC00000 + 184 × 4 = 0xFFC002E0`

成立的前提：PDE 和 PTE 格式相同（只用 4 KiB 頁、PS = 0），且 directory[1023] 的 U/S = 0，未來的 user 程式看不到分頁表。

限制：

| 項目 | 說明 |
|---|---|
| 虛擬空間 | 最高 4 MiB 保留，`paging_map` / `paging_unmap` 一律拒絕 |
| 只看得到目前的位址空間 | 透過 CR3 繞圈。要修改其他位址空間需另外的機制 |
| PDE 不存在時 | `paging_table_virt(N)` 本身未對應，讀取會 page fault，所以一定先檢查 PDE |
| 只在分頁開啟後有效 | `paging_init` 在開啟前直接用實體位址填表 |

（出處：OSDev Wiki "Paging"，Recursive mapping 段落）

### 建立新的 page table（`paging_get_or_create_table`）

```
① dir_index == 1023 → 失敗
② PDE 存在 → 回傳 paging_table_virt(N)
③ pmm_alloc_frame()，失敗回傳 0
④ directory[N] = phys | P | RW            先掛上 PDE
⑤ invlpg(paging_table_virt(N))
⑥ 透過 paging_table_virt(N) 清 0           才能存取新 frame
```

④～⑥ 之間新表內是垃圾，CPU 可能預先快取其中的翻譯。目前沒有程式存取該區域，且 `paging_map` 每寫一格 PTE 都對該頁 `invlpg`，所以會清掉可能殘留的錯誤翻譯。⑤ 的 `invlpg` 也會清除 paging-structure cache（Intel SDM Vol. 3A §4.10.3、§4.10.4.1）。

## 5. Page fault（例外 14）

MMU 翻譯失敗（P = 0 或違反權限）時觸發。它是 **fault**：CPU 存下的 EIP 指向出錯的指令本身，handler 修好後返回會重新執行。

| 資訊 | 來源 | 意義 |
|---|---|---|
| CR2 | `cpu_read_cr2()` | 出錯的虛擬位址 |
| EIP | `frame->eip` | 出錯的指令 |
| error code | `frame->err_code` | 出錯時在做什麼 |

error code（Intel SDM Vol. 3A §4.7, Figure 4-12）：

| bit | 0 | 1 |
|---|---|---|
| 0 P | 頁不存在 | 違反權限 |
| 1 W/R | 讀取 | 寫入 |
| 2 U/S | ring 0 | ring 3 |
| 3 RSVD | — | 表項的保留位元被設定 |
| 4 I/D | — | 取指令（需 NX/SMEP，本專案不用） |

例：`*(uint32_t *)0 = 1` → CR2 = `0`、error code = `0x2`（不存在、寫入、kernel）。分頁開啟前同一行會靜默寫壞 BIOS 中斷向量表。

`kernel/interrupt/exception.c` 印出暫存器、CR2 與解讀後的 error code，然後 `panic`。解讀由 `kernel/interrupt/page_fault.c` 的 `page_fault_describe` 負責（host 測試：`tests/host/test_page_fault.c`），位元常數 `EXCEPTION_PF_*` 定義在 `exception.h`：

```
CPU exception 14: Page Fault (#PF)
err_code=2 eip=10110F cs=8 eflags=10006
...
cr2=0 (not present, write, kernel)
```

## 6. 設計決策

### 第 1 步的決策（D1–D8）

| # | 項目 | 決定 | 理由 |
|---|---|---|---|
| D1 | 位址配置 | identity map（虛擬 = 實體） | 現有程式不用改；higher half 要同時處理連結器、開機組合語言、位址換算，失敗時沒有輸出。之後可再遷移 |
| D2 | 開機時對應範圍 | 前 4 MiB，第 0 頁除外 | 涵蓋目前所有東西，只需 1 張 page table（8 KiB）；第 0 頁抓 null pointer |
| D3 | 頁大小 | 只用 4 KiB | 一種格式；可以單獨排除第 0 頁；recursive mapping 需要 |
| D4 | 存取分頁表 | recursive mapping（directory[1023]） | 只用一格 PDE，不需額外記憶體 |
| D5 | API | 見第 8 節 | 未對齊回 `-1`；重複 map 回 `-1`；unmap 不歸還 frame；空表不釋放；`get_phys` 用輸出參數（實體位址 0 可能合法）；PMM 用完回 `-1`；map 自動加 PRESENT |
| D6 | 測試 | host 測純計算，QEMU 測硬體行為 | map/unmap 的邏輯很短，會出錯的是和硬體的互動 |
| D7 | 開機位置 | `idt_init` 之後；額外對應 Multiboot 資料 | 設定錯誤時 page fault 有訊息，而不是 triple fault；shell `mmap` 在開機後仍讀記憶體表 |
| D8 | 保護 | 暫不開 CR0.WP；U/S = 0；不設 PCD | WP 需要把 `.text` 對齊到頁，是另一個任務；沒有 user 程式；VGA 區域已由 MTRR 設為不快取 |

### 實作時的決策

| 項目 | 決定 | 理由 |
|---|---|---|
| 檔案切分 | `paging_entry.c`（純計算，加入 `HOST_LIB_SOURCES`）＋ `paging.c`（硬體） | 純計算可以在 host 上用 ASan/UBSan 測 |
| CPU 指令 | `arch/x86/cpu/cpu.h` 的 `static inline` | 和 `arch/x86/io/io.h` 一致；`-Werror` 下未使用的 `static` 函式無法單獨 commit |
| `paging_entry_make` 的輸入 | phys 遮罩到 bits 31:12、flags 遮罩到 bits 11:0 | 未對齊的 phys 不會變成旗標；是否視為錯誤由 `paging_map` 檢查 |
| index 超過 1023 | `paging_dir_index`、`paging_table_virt` 都遮罩 | 結果一定在範圍內；host 的 `uintptr_t` 是 64-bit |
| `paging_get_phys` | 回傳值包含頁內 offset | 和 MMU 行為一致 |
| `paging_unmap` 未對應的頁 | 回傳 `-1` | 和 map 拒絕重複對應一致 |
| `paging_get_or_create_table` | 和 `paging_map` 同一個 commit | 單獨 commit 會有未使用的 `static` 函式 |
| Multiboot 資料在第 0 頁 | `panic` | 對應它會破壞 null pointer 防護；實際上那裡是 BIOS 中斷向量表 |
| `paging_identity_map_range` | 維持 `static` | 只有一個使用者；等 ACPI、framebuffer 等第二個使用者出現再公開 |

## 7. 已接受的限制

1. **不阻止 unmap kernel 自己的頁**：unmap `0x00100000` 會立刻當機，由呼叫端負責。
2. **空的 page table 不釋放**：每張 4 KiB。
3. **WP = 0**：唯讀頁對 kernel 無效（`test_paging_fault_read_only` 記錄這個行為）。
4. **只有一個位址空間**：recursive mapping 只能修改目前 CR3 的表。
5. **identity map 限制在 4 MiB**：kernel 或 heap 變大超過時 `paging_init` 會 `panic`（KASSERT），要擴大 identity 範圍或改用 higher half。
6. **不是中斷安全的**：不可在 IRQ handler 中呼叫 map/unmap（PMM 也不是）。
7. **新 page table 的短暫空窗**：見第 4 節。
8. **flags 只遮罩到 12 bits**：除了三個定義的旗標，其他位元（PCD、G 等）也會被寫入，呼叫端要自己負責。

## 8. API

| 函式 | 說明 |
|---|---|
| `paging_init()` | 建表、identity map、開啟分頁、對應 Multiboot 資料。在 `pmm_init`、`heap_init`、`idt_init` 之後呼叫一次 |
| `paging_get_phys(virt, &phys)` | 成功回傳 `0`（含 offset），未對應或 `phys == NULL` 回傳 `-1`，失敗時不修改 `*phys` |
| `paging_map(virt, phys, flags)` | 自動加 PRESENT；需要時建立 page table。未對齊、已對應、最高 4 MiB、PMM 用完回傳 `-1`。不擁有 `phys` |
| `paging_unmap(virt)` | 清除 PTE 並 `invlpg`。未對齊、最高 4 MiB、未對應回傳 `-1`。不歸還 frame |

`paging_entry.h`（純計算）：

| 名稱 | 說明 |
|---|---|
| `paging_dir_index(virt)` / `paging_table_index(virt)` | bits 31:22 / 21:12 |
| `paging_entry_make(phys, flags)` | 組合一格 PDE/PTE |
| `paging_entry_get_phys(entry)` / `paging_entry_is_present(entry)` | 解析 |
| `paging_table_virt(N)` | 第 N 張 page table 的 recursive 位址 |
| `PAGING_PRESENT` / `PAGING_WRITABLE` / `PAGING_USER` | 旗標 |
| `PAGING_RECURSIVE_INDEX` / `PAGING_TABLES_VIRT` / `PAGING_DIRECTORY_VIRT` | 1023 / `0xFFC00000` / `0xFFFFF000` |

`arch/x86/cpu/cpu.h`：`cpu_read_cr0`、`cpu_write_cr0`、`cpu_read_cr2`、`cpu_read_cr3`、`cpu_write_cr3`、`cpu_invlpg`。所有 asm 都有 `"memory"` clobber，避免編譯器把分頁表的寫入移到這些指令的另一側。

## 9. 測試

- `tests/host/test_paging_entry.c`：位址切分（VGA、kernel、4 MiB 邊界、極端值）、表項組合與解析（遮罩、A/D 位元）、recursive 位址計算。
- `kernel/test/selftest.c`（QEMU）：
  - 開啟後：CR0.PG、directory[1023] 指向 CR3、identity map 內容、第 0 頁未對應
  - `paging_get_phys`：identity、offset、未對應（PDE 層與 PTE 層）、recursive 區域
  - `paging_map`：同一 frame 對應兩個位址（A 寫 B 讀）、建立新表（PMM 少 1、新表只有 1 格存在）、flags = 0、各種拒絕
  - `paging_unmap`：先讓翻譯進入 TLB 再 unmap，讀取必須 page fault（拿掉 `invlpg` 時這個測試會失敗，已驗證）
  - page fault：CR2 與 error code（未對應的讀取 `0x0`、寫入 `0x2`、null pointer）、WP = 0 時寫入唯讀頁不 fault
  - Multiboot 資料在分頁後可讀

page fault 測試用 `.byte` 寫死 2 bytes 的 `mov` 指令，hook 記錄 CR2 後 `eip += 2` 跳過它；否則 fault 返回會無限重試同一條指令。

沒有自動測試的部分：PMM 用完時 `paging_map` 回傳 `-1`（要先耗盡 3 萬多個 frame）；Multiboot 資料在 4 MiB 以外時的對應（QEMU 與 GRUB 都放在 4 MiB 內，只用暫時性程式碼驗證過一次）。

## 10. 除錯

- `make test-qemu` 帶 `-no-reboot`：triple fault 時 QEMU 直接結束，不會無限重開。
- QEMU 加 `-d int,cpu_reset`：印出每個例外的編號、CR2、EIP，可看出第一個 page fault 的位址。
- QEMU monitor：`info mem`（對應範圍與權限）、`info tlb`（逐頁對應）、`xp /4wx <實體位址>`（直接讀分頁表）。

## 11. 之後的工作

- **CR0.WP + 唯讀 `.text`/`.rodata`**：`linker.ld` 把區段對齊到 4 KiB，再以唯讀對應；`test_paging_fault_read_only` 要跟著改成預期 fault。
- **heap 自動擴充**：用 `pmm_alloc_frame` + `paging_map` 把散落的 frame 接在 heap 結尾之後，不再需要實體連續（`docs/pmm.md` 第 8 節）。
- **higher half kernel**：如果之後需要每個程式獨立的低位址空間。
- **多個位址空間**：每個程式一個 directory，共用 kernel 的 PDE；需要修改非目前位址空間的機制（暫時對應視窗）。
- ~~page fault handler 逐位解讀 error code；`exception.c` 改用 `cpu_read_cr2()`；`EXCEPTION_PAGE_FAULT` 移到 `exception.h`~~：已完成。
