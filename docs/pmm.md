# PMM（實體記憶體管理）

`kernel/memory/pmm.c`、`kernel/memory/pmm.h`

PMM 負責記住每一塊實體記憶體（frame）有沒有人用，並把空的分配出去。它在記憶體管理的最底層：heap 和分頁都向它要記憶體，它自己不依賴任何其他配置器。

## 1. 基本概念

### 實體記憶體

CPU 用**實體位址**直接存取 RAM 晶片。但不是每個位址後面都是可用的 RAM：

1. **已經有人在用**：BIOS 資料、kernel 本身、GRUB 留下的 Multiboot 資料。
2. **位址後面不是 RAM**：例如 `0xA0000`～`0xFFFFF` 是 VGA 顯示記憶體與 ROM。寫 `0xB8000` 會直接出現在螢幕上。
3. **根本沒有 RAM**：32-bit 位址可以寫到 4 GiB，但 QEMU 預設只有 128 MiB。

所以 kernel 不能自己猜哪裡可以用，要依靠 BIOS 提供、GRUB 轉交的**記憶體表**。

### frame、page、對齊

| 名詞 | 意義 |
|---|---|
| **frame（頁框）** | 實體記憶體中 4 KiB 的一格。PMM 管的是 frame。 |
| **page（頁）** | 虛擬位址空間中 4 KiB 的一格。分頁時才會用到。 |
| **對齊（alignment）** | frame 一定從 4096（`0x1000`）的倍數開始，位址最後 3 個 hex 數字是 `000`。 |

```
frame 編號 = 位址 / 4096 = 位址 >> 12
位址       = frame 編號 × 4096
例：0x00209000 → frame 0x209
```

以 4 KiB 為單位而不是 byte，原因：

1. **帳本小**：128 MiB 只有 32768 個 frame。
2. **硬體這樣切**：x86 分頁以 4 KiB 為單位（Intel SDM Vol. 3A, Chapter 4 "Paging"），PMM 給的 frame 可以直接拿去做分頁。
3. **使用者本來就要大塊**：heap 擴充、page table、kernel stack。小塊的切割交給 heap。

代價是零頭：一個 frame 只要有一點被佔用，整個 frame 就算已用。

### PMM、分頁、heap 的關係

```
        kmalloc / kfree            ← kernel 各模組
               │
        ┌──────▼──────┐
        │    heap     │  切小塊（byte 為單位）
        └──────┬──────┘
               │
        ┌──────▼──────┐
        │   分頁       │  虛擬 page → 實體 frame（見 docs/paging.md）
        └──────┬──────┘
               │
        ┌──────▼──────┐
        │    PMM      │  管理所有 frame
        └──────┬──────┘
               │ 開機時讀一次
        ┌──────▼──────┐
        │ Multiboot   │
        │ 記憶體表     │
        └─────────────┘
```

- PMM **不能**使用 `kmalloc`，因為 heap 要靠 PMM 才存在。
- 還沒有分頁時，虛擬位址 = 實體位址。heap 需要一整塊**實體連續**的記憶體，所以 PMM 必須能分配連續的 frame（`pmm_alloc_frames`）。有了分頁之後，散落的 frame 也能對應成連續的虛擬位址。

## 2. Multiboot 記憶體表

BIOS（INT 15h, E820）回報記憶體配置，GRUB 整理成表，開機時用 `EBX` 把 `multiboot_info_t` 的位址交給 `kernel_main`。

- 必須先在 Multiboot header 的 flags 設 bit 1（`platform/pc/boot/boot.asm`：`0x2`），GRUB 才會提供記憶體資訊。
- `multiboot_info_t.flags` 的 bit 6 為 1 時，`mmap_addr`、`mmap_length` 才有效。

（出處：Multiboot Specification 0.6.96, §3.1.2、§3.3）

### 每一列的格式

```
offset: 0      4               12              20     24
        [size ][addr (8 byte) ][len (8 byte)  ][type ]
```

| type | 意義 |
|---|---|
| 1 | 可用 RAM |
| 2 | 保留 |
| 3 | ACPI 可回收 |
| 4 | ACPI NVS |
| 5 | 壞掉的 RAM |

type 1 由 0.6.96 規範定義；3～5 依照 GRUB 的 `multiboot.h`（與 E820 相同）。PMM 只把 type 1 當成可用。

### 兩個陷阱

1. **必須 `packed`**：欄位在記憶體中緊貼在一起。i386 上 `uint64_t` 只要 4-byte 對齊，所以不加 `packed` 也碰巧正確；但 x86_64（host 測試）會在 `size` 後面塞 4 byte。`multiboot.h` 用 `__attribute__((packed))` 加 `_Static_assert(sizeof == 24)` 固定格式。
2. **`size` 不含自己**：值通常是 20。下一列在 `目前位址 + size + 4`，不能用 `sizeof`，因為規範允許更長的列。

`multiboot_mmap_first()` / `multiboot_mmap_next()` 負責走訪，並拒絕超出 `mmap_length` 或 `size < 20` 的列。

## 3. 實際的記憶體地圖（QEMU，128 MiB）

shell 指令 `mmap` 的輸出（`qemu-system-i386 -kernel`）：

```
Base              Length            Type
00000000:00000000 00000000:0009FC00 1 (available)
00000000:0009FC00 00000000:00000400 2 (reserved)
00000000:000F0000 00000000:00010000 2 (reserved)
00000000:00100000 00000000:07EE0000 1 (available)
00000000:07FE0000 00000000:00020000 2 (reserved)
00000000:FFFC0000 00000000:00040000 2 (reserved)
```

```
0x00000000 ┌─────────────────────────┐ ┐
           │ BIOS 中斷表/資料區       │ │
0x00009500 │ ★ Multiboot 資訊         │ │ type 1，但 PMM 整段保留
0x0009FC00 ├─────────────────────────┤ ┘
           │ EBDA                    │   type 2
0x000A0000 ├─────────────────────────┤
           │ VGA / 顯示卡 ROM         │   表上沒有列出
0x000F0000 ├─────────────────────────┤
           │ BIOS ROM                │   type 2
0x00100000 ├─────────────────────────┤ ┐
           │ kernel（含 128 KiB bitmap）│ │
0x00229D40 ├─ _kernel_end ───────────┤ │
0x0022A000 │ heap（1 MiB，PMM 分配）   │ │ type 1
0x0032A000 ├─────────────────────────┤ │
           │ PMM 可分配的空間         │ │
0x07FE0000 ├─────────────────────────┤ ┘ ← pmm_limit（frame 0x7FE0）
           │ 保留                     │   type 2
0x08000000 ├─────────────────────────┤
           │ 沒有 RAM                 │
0xFFFC0000 ├─────────────────────────┤
           │ BIOS 晶片                │   type 2
0xFFFFFFFF └─────────────────────────┘
```

開機時印出 `PMM: 31926 / 32639 frames free`：

- total 32639 = 低區 159 個完整 frame（`0x9FC00 / 4096`）+ 主區 32480
- free 31926 = 32480 − kernel 298（frame `0x100`～`0x229`）− heap 256（frame `0x22A`～`0x329`，`pmm_alloc_frames(256)` 找到的第一段連續空位）

經過 GRUB（`make run`）時 Multiboot 資訊的位置不同，但處理規則一樣。

## 4. 設計決策

### 演算法：bitmap

每個 frame 1 bit，1 = 已用。

| | bitmap | free stack |
|---|---|---|
| 帳本大小（128 MiB） | 4 KiB | 約 127 KiB |
| 分配單一 frame | O(n)，整字跳過所以實際很快 | O(1) |
| 連續 N 個 frame | 容易 | 很難 |
| 抓 double free | 可以 | 不行 |

選 bitmap：還沒有分頁，heap 需要連續分配；也能抓 double free、容易測試。更進階的 buddy allocator（Linux 使用）兼顧兩者，但目前不需要。（參考：OSDev Wiki〈Page Frame Allocation〉）

### 決策一覽

| 項目 | 決定 | 理由 |
|---|---|---|
| bitmap 位置 | `.bss` 中固定 128 KiB 的 static 陣列，涵蓋 4 GiB | 不需要分配，避開「帳本放哪」的雞生蛋問題。代價：128 MiB 機器浪費約 124 KiB |
| 低 1 MiB | 整段保留 | 一次避開 BIOS 資料、EBDA、VGA、ROM；frame 0 永不分配，所以 `0` 可以當失敗值 |
| kernel 範圍 | `pmm_init(kernel_start, kernel_end)` 的參數 | 不可能忘記；host 測試可傳假值（`_kernel_end` 是 linker 符號，host 無法連結） |
| API 單位 | 實體位址 | 呼叫端不用換算 |
| 錯誤處理 | 回傳 `-1` / `0`，不 panic | 符合專案慣例，可在 host 測試 |
| 連續分配的長度 | 不記，釋放時由呼叫端傳 `count` | bitmap 存不下長度 |
| frame 內容 | 分配時不清 0、釋放時不填值 | PMM 只碰 bitmap：host 測試可用假位址；分頁後 PMM 不一定能存取任意實體位址 |
| 4 GiB 以上 | 忽略；跨越 4 GiB 的區域截斷 | 沒有 PAE 時 CPU 存取不到 |

### 初始化（`pmm_init`）

```
1. bitmap 全部設 1（先假設都不能用）
2. Pass 1：type 1 區域 → 0     往內取整，只算完整的 frame
   total = 此時的 free 數；記下 limit = 最高可用 frame + 1
3. Pass 2：非 type 1 區域 → 1  往外取整
4. Pass 3：低 1 MiB、kernel、Multiboot 資訊、記憶體表 → 1
```

- **先全部已用、再開放、再保留**：BIOS 回報的區域若重疊，結果偏向保守（寧可少用，不可誤用）。
- **取整方向**：可用區域往內（`0x9FC00` 結尾 → 只到 `0x9F000`）；保留區域往外（碰到一點就整個 frame 已用）。
- 位址計算用 `uint64_t`：表上的位址是 64-bit，且 4 GiB 本身放不下 32-bit。
- 計數只在 bit 真的改變時調整，所以重疊、重複保留不會算錯。

### 搜尋：hint 與 limit

- **`pmm_hint`**：保證 hint 以下全部已用。分配從 hint 開始找，結果永遠是最低的空 frame（和從頭找一樣），但不用重複掃過前面。
  - 分配後推到 frame + 1；全滿時設成 limit。
  - 釋放的 frame 比 hint 低時拉回來；只往下拉，不往上推。
  - `pmm_reserve_range` 只會把 bit 設成 1，不影響保證。
- **`pmm_limit`**：掃描上限。bitmap 涵蓋 4 GiB，但 RAM 只到 frame `0x7FE0`，不設上限的話全滿時會白掃 3 萬多個字。
- 在一個字裡找第一個 0 bit：`__builtin_ctz(~word)`，編譯成 `BSF` 指令（Intel SDM Vol. 2A, "BSF—Bit Scan Forward"）。`word` 全為 1 時 `ctz(0)` 沒有定義，所以先跳過 `0xFFFFFFFF` 的字。

```
字:   [16]       [17]       [18]
值:   FFFFFFFF   FFFFFE00   00000000
                 ↑ ~值 = 000001FF，ctz = 9
                 → frame = 17 × 32 + 9 = 0x229
```

### 連續分配（`pmm_alloc_frames`）

一次掃過、邊走邊數：遇到 0 計數 +1，遇到 1 歸零，數到 count 就找到。

- 找到時：區塊從掃描途中第一個空 frame 開始 → hint 推到區塊結尾；否則（跳過了太短的空位）→ hint 設成那個空位。
- 找不到時 hint 不動：可能還有零散的空 frame。
- `count > free 數` 直接失敗，不掃描。

### 釋放（`pmm_free_frame` / `pmm_free_frames`）

- `pmm_free_frame(a)` 就是 `pmm_free_frames(a, 1)`，規則只寫一次。
- 先檢查範圍（對齊、≥ 1 MiB、< limit、`count ≤ limit − 起點`），全部通過才讀 bitmap。用數量比較而不是算結尾位址，避免 `count × 4096` 溢位。
- **全有或全無**：整段都是已用才清除，否則回傳 `-1`、bitmap 不變。

## 5. 已接受的限制

1. **無法區分「保留」與「已分配」**：bitmap 只有 0/1。釋放 kernel 的 frame（≥ 1 MiB、< limit）會成功。呼叫端只能釋放自己分配的。
2. **1 MiB 以上、可用區域之間的空洞**：也是 1，同樣擋不住錯誤的釋放（QEMU 沒有這種空洞，真實硬體常見）。
3. **跨越多次分配的釋放會成功**：bitmap 不記區塊邊界。
4. **碎片化**：free 數夠，但最長的連續空位可能不夠，`pmm_alloc_frames` 會失敗。開了分頁、heap 不再需要實體連續後就不是問題。
5. **不是中斷安全的**：不可在 IRQ handler 中呼叫，沒有加鎖。
6. **`pmm_init` 會重置所有狀態**：kernel 只應呼叫一次（host 測試利用這點在每個測試重新初始化）。

## 6. API

| 函式 | 說明 |
|---|---|
| `pmm_init(kernel_start, kernel_end)` | 依記憶體表建立 bitmap。需先呼叫 `multiboot_init` |
| `pmm_reserve_range(start, size)` | 把碰到的 frame 標已用；`size = 0` 回傳 `-1` |
| `pmm_alloc_frame()` | 最低的空 frame，失敗回傳 `0` |
| `pmm_alloc_frames(count)` | 連續 count 個，失敗或 `count = 0` 回傳 `0` |
| `pmm_free_frame(address)` | 沒對齊、超出範圍、double free 回傳 `-1` |
| `pmm_free_frames(address, count)` | 同上，全有或全無 |
| `pmm_is_frame_used(address)` | 包含 address 的 frame 已用（或超出範圍）回傳 `1`，空的回傳 `0` |
| `pmm_get_total_frames()` | 表上 type 1 的完整 frame 數 |
| `pmm_get_free_frames()` | 目前空的 frame 數 |

## 7. 測試

- `tests/host/test_multiboot.c`：結構 offset、bit 6、走訪、較長的列、截斷與太短的列。
- `tests/host/test_pmm.c`：初始化計數、取整、重疊、4 GiB 截斷、查詢、分配順序、字邊界、空洞、用盡、釋放與 hint、double free、錯誤位址、連續分配與碎片化、溢位。
- `kernel/test/selftest.c`（`test_heap_region_reserved_in_pmm`）：開機後 heap 區域的每個 frame 在 PMM 中都是已用。

`multiboot_info_t` 存的是 32-bit 位址，所以 host 測試用 `mmap(MAP_32BIT)` 把假的 Multiboot 資料放在 4 GiB 以下。PMM 只讀寫自己的 bitmap，不碰假的實體位址。

## 8. 之後的工作

- ~~heap 改向 PMM 要記憶體~~：已完成。`kernel_main` 在 `pmm_init` 之後用 `pmm_alloc_frames(KERNEL_HEAP_SIZE / PMM_FRAME_SIZE)` 取得 1 MiB 連續區域交給 `heap_init`，失敗時 `panic`；過渡期的 `pmm_reserve_range` 已移除。
- heap 自動擴充：用完 1 MiB 時再向 PMM 要 frame。新的 frame 不一定接在舊區域後面；分頁已完成，可以用 `pmm_alloc_frame` + `paging_map` 把它們接在 heap 結尾的虛擬位址之後（`docs/paging.md` 第 11 節）。
- ~~分頁（`kernel/memory/paging`）~~：已完成，見 `docs/paging.md`。page table 的 frame 由 `pmm_alloc_frame` 提供，由分頁模組清 0；開機時的 directory 與第一張 page table 是 heap 之後的頭兩個 frame（QEMU 上為 `0x32A000`、`0x32B000`）。
