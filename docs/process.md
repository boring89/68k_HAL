# 行程、排程、訊號與管線

`hal/task.h`、`hal/usermode.h`、`hal/mmu.h`（位址空間）、`arch/x86/task/`、`arch/x86/mmu/space.c`、`kernel/user/sched.c`、`process.c`、`signal.c`、`pipe.c`

這份文件寫給想弄懂「這個 OS 怎麼同時跑好幾個程式」的人。（P7 R2，自動排程一次做完，**使用者尚未審核**。）

## 1. 比喻

以前的 OS 像**一間只有一個員工的店**：一次只服務一個客人，客人在店裡想事情（等鍵盤）時，員工也只能站著等。

現在是**同一個員工，但有很多張工作桌**。每個客人（行程）有自己的桌子（記憶體空間）和自己的便條紙（kernel 堆疊，記著做到哪裡）。員工做到一半發現「這位客人在等東西」就**把便條紙夾好、換去別張桌子**，等有事發生再回來。這叫**協作式排程**：只有「客人自己說我要等了」的時候才換桌子；沒有人會被硬拉走（所以一個一直算不停的程式會讓其他人都等著，已知限制）。

名詞：
- **行程（process）**：一個執行中的程式，有自己的位址空間、檔案描述符、目前目錄、訊號設定。
- **位址空間（address space）**：一份分頁表（CPU 的 CR3 指向它）。同一個位址 `0x08048000`，在不同行程裡是不同的記憶體。
- **kernel 堆疊**：行程在 kernel 裡（做系統呼叫）時用的堆疊，每個行程 16 KiB。
- **殭屍（zombie）**：已經結束、但父行程還沒用 `wait` 領走結束狀態的行程。
- **訊號（signal）**：給行程的一個小通知（Ctrl-C = SIGINT、`kill`），行程可以接住（handler）或讓預設動作發生（通常是結束）。

## 2. 各層怎麼分工

```
kernel/user/sched.c     行程表（64 個）、排程、fork、exit、wait
kernel/user/signal.c    訊號：送、擋、handler、sigreturn
kernel/user/pipe.c      匿名管線（16 KiB 環形緩衝）
kernel/user/process.c   載入 ELF、spawn、execve、一條「工作」（shell 的一行）
        │  hal/task.h（context 切換、FPU）、hal/usermode.h（進入 ring 3、fork、暫存器）、hal/mmu.h（hal_space_*）
        ▼
arch/x86/task/          context_switch（task.asm）、hal_task.c
arch/x86/mmu/space.c    每個行程一份分頁目錄
```

## 3. 位址空間

每個行程有一份**分頁目錄**（1024 項，每項管 4 MiB）。kernel 自己的部分（前 4 MiB 的 identity map、heap `0x20000000`、Mac 的 guest RAM `0x10000000`、vmap `0xE0000000`、遞迴對應…）在**所有**目錄裡是同一份分頁表；使用者範圍（`0x08000000–0x0FFFFFFF`、`0x50000000–0x7FFFFFFF`）每個空間自己一份：

```
目錄項          kernel 範圍              使用者範圍
行程 A  [PDE 0]──┐                      [PDE 32..] 只屬於 A
行程 B  [PDE 0]──┼─ 同一張分頁表         [PDE 32..] 只屬於 B
kernel  [PDE 0]──┘
```

這是 `hal_space_*`（`hal/mmu.h`）：`create`（複製 kernel 的目錄項）、`switch`（寫 CR3）、`destroy`（歸還使用者範圍的分頁表和目錄）、`map`（對別的空間映射一頁）、`copy_page`（fork 用）。

**kernel 之後才新增的分頁表**（heap 長大、vmap 映射新區域）怎麼讓所有空間都看得到？選擇：**新增時同步**。`paging_get_or_create_table` 建立 kernel 範圍的新目錄項時，呼叫 `space_propagate_entry` 寫進每一個現有的目錄（目錄都永久映射在 `0xD0000000` 開始的視窗，所以 kernel 可以隨時編輯別人的目錄）。另一種做法是開機時把 kernel 範圍的所有分頁表預先建好（要 2 MiB 以上的 RAM）；我們選同步，因為不浪費記憶體，只在新增分頁表時多寫幾個目錄項。

## 4. 排程

`sched_wait()` 的用法就是等待迴圈：

```c
while (!條件())
    sched_wait();
```

`sched_wait` 把自己標成「等待中」並換到下一個可以跑的行程。什麼時候「可以跑」？等待中的行程在**有事發生**時才再被挑：硬體中斷（計時器、鍵盤、網卡；`hal_irq_count()` 變了）或別的行程呼叫 `sched_notify()`（管線有資料、行程結束、送了訊號…）。所有人都在等又沒有事發生時，CPU `hlt` 睡到下一個中斷。可能被無故喚醒，所以條件一定要重新檢查。

**kernel 自己也是一個「行程」**（第 0 格，開機時的那條執行緒）：shell 的主迴圈在它裡面。`user_run_job` 啟動程式後，kernel 這條執行緒就在 `sched_wait` 裡等工作結束。

切換（`switch_to`）做的事：存 FPU/SSE 狀態（`fxsave`）、換目前目錄（存舊的、設新的）、換 CR3、設 TSS 的 `esp0`（之後 ring 3 → ring 0 的堆疊）、載入 TLS 段、還原 FPU、`hal_context_switch`（存 `edi esi ebx ebp eflags`、換 `esp`、還原、`ret`）。

## 5. 程式的一生

**spawn**（shell 啟動程式）：讀 ELF → 建一個行程（空的位址空間）→ 暫時切到它的空間把程式載進去、建堆疊 → 切回來 → `sched_start`（設好從 `hal_user_enter` 開始）。

**fork**（`sched_fork`）：建立子行程；複製**整個**使用者記憶體（沒有 copy-on-write，每頁配一個新 frame 複製；映射到裝置的頁面如 `/dev/fb0` 共用）；複製描述符（共用同一個開檔物件，refcount+1）、訊號設定、目前目錄；`hal_user_fork` 把父行程的系統呼叫 frame 複製到子行程的 kernel 堆疊並把 `eax` 設成 0，所以子行程第一次被排到就像「從 fork 回來」，傳回 0。父行程得到子行程的 pid。

**execve**（`user_exec`）：先讀檔、檢查 ELF（失敗就回傳錯誤，原程式不受影響）→ 到這裡不能回頭：釋放舊記憶體、關掉 close-on-exec 的描述符、載入新程式、訊號 handler 回到預設（被忽略的維持忽略）→ 把系統呼叫要回去的位置（`eip`、`esp`）換成新程式的進入點。

**exit**（`sched_exit`）：釋放記憶體與描述符、孩子過繼給 kernel（pid 0）、狀態變殭屍、送 SIGCHLD 給父行程、換走（不會回來）。殭屍的分頁目錄、kernel 堆疊和行程表項目由 `wait` 的人釋放（因為結束的行程自己還站在那個堆疊上）。

**wait4**（`sched_wait_child`）：找符合的殭屍孩子（`pid > 0` 指定、`-1` 任一個、`0` 同行程群組、`< -1` 指定群組）→ 領走、回傳狀態（`exit 碼 << 8`，或被訊號殺死時是訊號編號）；沒有孩子 `ECHILD`；沒有人結束就等（`WNOHANG` 不等）。

**上限**：同時最多 64 個行程（殭屍也算）；`fork` 超過回 `EAGAIN`。`USER_MAX_PROCS`。

## 6. 管線與工作（job）

shell 的一行 `a | b | c` 是一個 **job**（`user_run_job`）：

```
 a ──pipe1──▶ b ──pipe2──▶ c ──▶ console
```

三個行程**同時**存在，各自的標準輸出／輸入是 `pipe_create` 做出來的管線兩端（一般的開檔物件，所以 `dup2`、`fork` 都能用）。管線是 16 KiB 的環形緩衝：空了，讀的人 `sched_wait`；滿了，寫的人 `sched_wait`；所有寫端都關了，讀的人拿到 EOF（0）；所有讀端都關了，寫的人得到 SIGPIPE／`EPIPE`。`<`、`>`、`>>` 取代該段的管線或 console。同一個 job 的行程在同一個**行程群組**，job 結束後如果還有行程活著（孤兒），一律殺掉並收屍。

## 7. 訊號

- 送：`signal_send(p, sig)` 在對方的 `sig_pending` 位元上設一位（被忽略的訊號直接丟掉）並 `sched_notify()`。
- 收：行程**自己**在系統呼叫要回到使用者模式之前（`user_syscall` 的結尾）檢查，所以沒有「在別人執行到一半時打斷他」的問題。預設動作＝結束（被訊號殺死，結束碼 128+n，並印 `名稱: killed by signal n`；SIGINT、SIGQUIT、SIGPIPE 不印）；SIGCHLD、SIGURG、SIGWINCH、SIGCONT 預設忽略；停止類訊號（SIGSTOP 等）目前也忽略（沒有作業控制）。SIGKILL 不能被擋也不能被接。
- 等待中的系統呼叫（讀 console、讀空管線、`nanosleep`、`poll`、`wait4`…）會在有可處理的訊號時回傳 `EINTR`。
- handler：在使用者堆疊上放一個 `signal_frame_t`（回傳位址＝C 函式庫給的 restorer、訊號編號、原暫存器、原遮罩），把 `eip` 改成 handler；handler 結束 `ret` 到 restorer，它呼叫 `sigreturn`／`rt_sigreturn`，kernel 從堆疊上還原暫存器與遮罩。（musl 的舊式 `__restore` 還會 `pop` 掉訊號編號，所以 `sigreturn` 的 frame 在 `esp-8`，`rt_sigreturn` 在 `esp-4`。）
- `kill`／`tkill`／`tgkill`、`rt_sigaction`、`rt_sigprocmask`、`rt_sigpending`、`rt_sigsuspend`、`pause`。**沒有** `alarm`／`setitimer`。
- **Ctrl-C**：console 的鍵盤事件經過一個 hook（`input_event_set_hook`，在鍵盤中斷裡執行）數出 Ctrl-C 與 Ctrl-\ 的次數，`user_console_pump`（工作等待迴圈每次被喚醒時呼叫）把它們變成對前景行程群組的 SIGINT／SIGQUIT（`ISIG` 開著才算）。所以即使程式在睡覺、在等管線、不讀鍵盤，Ctrl-C 也有用；按鍵本身仍留在事件緩衝區（type-ahead 不會掉）。

## 8. 新增的系統呼叫

`fork`、`vfork`、`clone`（只有 fork 語意；`CLONE_THREAD` 回 `ENOSYS`；`child_stack` 有效）、`execve`、`waitpid`、`wait4`、`pipe`、`pipe2`、`dup3`、`dup`／`dup2`／`fcntl(F_DUPFD、F_GETFD、F_SETFD)` 與 `O_CLOEXEC`、`kill` 等訊號呼叫、`getpid`／`getppid`／`getpgid`／`getpgrp`／`setpgid`／`setsid`、`pause`、`rt_sigsuspend`、`rt_sigpending`、`rt_sigreturn`、`sigreturn`。

## 9. 限制（已知）

- 協作式：不會自己讓出 CPU 的程式（一直算）會讓全部卡住。之後可升級為搶佔式（計時器中斷裡切換），要先把 kernel 裡共用資料的臨界區處理好。
- fork 複製整個位址空間（慢、吃記憶體）；沒有 copy-on-write。
- 沒有執行緒（`CLONE_THREAD`）、`alarm`、作業控制（停止／繼續）、`SA_RESTART`（被訊號打斷的呼叫一律回 `EINTR`）、`SA_ONSTACK`、`siginfo` 的內容（handler 收到的是全 0 的結構，`si_signo` 除外）。
- 工作結束時還活著的孤兒行程會被殺掉（沒有真正的背景工作）。
- 行程群組只有最簡單的語意（沒有 session、控制終端）；`TIOCSPGRP` 被接受但前景群組由 shell 決定。
- 一次只有一個前景工作；shell 還不能在背景執行（沒有 `&`）。

## 9.5 移植的程式：dash 與 GNU make

兩個用得到所有行程功能的真實程式已經移植（`tools/build_dash.sh`、`tools/build_make.sh`，下載、校驗與建置方式同 Lua 等）。移植時找到並修正的問題：**`rt_sigsuspend`**：訊號要在**暫時的遮罩**下送達 handler，之後才恢復舊遮罩（原本先恢復舊遮罩，被擋住的 SIGCHLD 就不會再送達，dash 的 `wait` 永遠醒不來；現在 `sig_saved`／`sig_restore`，由 `signal_deliver` 或 `sigreturn` 還原）；缺 `faccessat2`（439，musl 的 `faccessat` 用它）。`make → dash → tcc → 程式` 這條鏈在 OS 裡完整跑通（`userland/data/test.mk`）。

## 10. 測試

- `userland/forkexec.c`、`pipes.c`、`signals.c`（輸出與 host 相同，預期檔在 `userland/expected/`）：`make test-qemu` 在 OS 內執行並比對。
- `test_process_limit`（64 個上限、歸還所有 frame）、`test_process_no_leak`（反覆執行會 fork／exec／pipe 的程式後，空閒 frame 數不變）。
- `make test-shell`：`seq 1000 | grep 7 | wc -l`（三個行程同時）、`sleep 30` 與 `seq 3 | sleep 30` 加 Ctrl-C（結束碼 130）。
