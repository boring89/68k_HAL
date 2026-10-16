# CPU：CPUID、FPU/SSE、TSC、ACPI、SMP

`arch/x86/cpu/`（cpuid、hal_cpu）、`platform/pc/timer/tsc*.c`、`platform/pc/acpi/`、`platform/pc/smp/`、`platform/pc/vmap/`

這份文件整理「這台電腦的處理器是什麼、有幾顆、怎麼量時間」。寫給沒有參與開發過程的人看。

## 1. CPUID：問 CPU「你是誰」

比喻：CPU 裡有一個櫃台，你報上「編號」（EAX），它回答一組暗號（EAX/EBX/ECX/EDX）。`CPUID` 指令就是這個櫃台（Intel SDM Vol.2A "CPUID"）。

| 編號（leaf） | 內容 |
|---|---|
| 0 | 最大基本編號、廠商字串（EBX+EDX+ECX = `GenuineIntel`） |
| 1 | family/model/stepping（EAX）、功能旗標（ECX、EDX） |
| 0x80000001 | 擴充旗標（NX 位元在 EDX bit 20） |
| 0x80000002–4 | 品牌字串（48 字元） |
| 0x80000007 | EDX bit 8：invariant TSC |

- `arch/x86/cpu/cpuid.c` 只負責「讀」（含檢查 CPU 有沒有 CPUID：EFLAGS.ID 位元能不能翻轉）。
- `cpuid_decode.c` 把原始暫存器值轉成 `hal_cpu_info_t`，是純函式，host 測試（`tests/host/test_cpuid.c`）可以餵假資料。
- family/model 的「擴充」規則：family = 0xF 時加上 extended family；family 為 6 或 0xF 時 model 要加上 extended model << 4。
- shell 指令 `cpuinfo` 顯示結果。

## 2. 開啟 FPU 與 SSE

CPU 開機後浮點單元（x87）與 SSE 預設是「半關閉」的（`CR0.EM` 可能為 1，作業系統必須宣告自己會處理）。`hal_cpu_init()` 照 Intel SDM Vol.3A 13.1.2、13.1.3 做：

```
CR0.EM = 0   不要把 x87/SSE 指令當成需要模擬
CR0.MP = 1   WAIT 指令遵守 TS 位元
CR0.NE = 1   x87 錯誤用 #MF 例外回報
CR4.OSFXSR = 1, CR4.OSXMMEXCPT = 1   作業系統會用 FXSAVE，允許 SSE
FNINIT       x87 回到初始狀態
```

kernel 自己不用浮點，所以中斷發生時**不**儲存 FPU/SSE 暫存器。使用者程式一次只跑一個（沒有多工），這樣足夠；將來做多工時，必須在切換時用 FXSAVE/FXRSTOR 保存。

## 3. TSC 與奈秒計時

TSC（time stamp counter）是每個 CPU 週期加 1 的 64 位元計數器，用 `RDTSC` 讀。它的頻率不知道，要用已知頻率的東西量：**PIT 通道 2**（1,193,182 Hz）。

```
 設 PIT 通道 2 倒數 23864 次（約 20 ms）
 讀 TSC -> 開始 gate -> 等 port 0x61 的 OUT2 位元變 1 -> 讀 TSC
 頻率(kHz) = TSC 差 × 1193182 / (23864 × 1000)
```

重複 3 次取最小值（最不受干擾的那次）。通道 2 不需要中斷，所以在中斷關閉的開機階段就能量。

奈秒 = 週期數 × `mult` >> 24，其中 `mult = 10^6 × 2^24 / kHz`。這樣不需要 64 位元除法（kernel 沒有 libgcc）。`hal_timer_ns()` 回傳自 `tsc_init` 起的奈秒；沒有 TSC 時退回用 PIT tick（精度 10 ms）。

## 4. ACPI：韌體留下的機器說明書

BIOS 在 `0xE0000–0xFFFFF` 放一個 RSDP（"RSD PTR " 開頭、16 位元組對齊、checksum 為 0）。它指向 RSDT，RSDT 是一串表格的實體位址；其中簽名 `APIC` 的表是 MADT，列出：

- 每顆處理器的 local APIC ID（flags bit 0 = 啟用）
- local APIC 的實體位址（通常 `0xFEE00000`）
- I/O APIC 的位址
- flags bit 0：機器還有舊式 8259 PIC

這些表格在實體記憶體高處（QEMU：靠近 RAM 頂端），超出開機的 4 MiB identity map，所以用 `vmap()`（`platform/pc/vmap/`）映射到 `0xE0000000–0xF0000000` 的視窗。`vmap` 只增不減，不歸還。MMIO 用 `HAL_MMU_UNCACHED`（PCD+PWT）映射。

目前只搜尋 BIOS 區域，不搜尋 EBDA（它在 page 0，那頁是 null pointer 防護頁，沒有映射）。解析是純函式（`acpi_parse.c`），host 測試涵蓋。shell 指令 `acpi`。

## 5. SMP：叫醒其他核心

開機時只有一顆 CPU（BSP）在跑，其他（AP）在等 INIT。流程（Intel SDM Vol.3A 8.4.4，MP Initialization Protocol）：

```
BSP                                   AP
 複製 trampoline 到 0x7000
 寫入該 AP 的堆疊、C 入口
 送 INIT IPI ------------------------> 重置
 等 10 ms
 送 SIPI (vector 0x07) ---------------> 從實體位址 0x7000 以 16 位元實模式開始
 等 200 us，再送一次 SIPI
                                        trampoline：載入 GDT -> 開 CR0.PE -> 跳 32 位元
                                        設堆疊 -> 呼叫 smp_ap_entry
                                        lock inc ap_online；cli; hlt
 等 ap_online 增加（逾時 100 ms）
```

IPI 是寫 local APIC 的 ICR 暫存器（0x300/0x310）。AP **不開分頁、不開中斷**：kernel 是 identity map，程式位址相同，AP 不需要頁表。**中斷仍由 BSP 的 8259 處理**；改用 APIC 是架構大改，列在待決定。

驗證：`make test-qemu` 以 `-smp 4` 啟動，selftest 檢查 ACPI 列出 4 顆、`hal_cpu_online_count() == 4`。

## 6. 之後

- 讓 AP 真的做事需要：每顆 CPU 的堆疊/資料（GS 指向 per-CPU 結構）、kernel 鎖、APIC 中斷（取代 8259）、TLB shootdown。這些尚未做。
