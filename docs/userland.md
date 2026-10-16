# 使用者程式與 C 開發工具

`arch/x86/user/`、`hal/usermode.h`、`kernel/user/`、`kernel/vfs/`、`sdk/`、`userland/`、`tools/build_sdk.sh`、`tools/68khal-cc`

這份文件寫給想「把一個 C 程式移植到這個 OS」的人。先講原理，再講怎麼做。

## 1. 這是什麼

比喻：kernel 是一棟大樓的管理室，使用者程式是租戶。租戶不能進管理室（CPU 的 ring 3 不能碰 kernel 記憶體與特權指令），想要服務（讀檔、印字、要記憶體）就按櫃台的鈴（`int 0x80` 系統呼叫）。

為了不用重寫所有軟體，這個 OS 對使用者程式說**和 Linux i386 一樣的語言**：

```
  你的 C 程式 (hello.c)
        │  用 host 上的 gcc 編譯（-m32，靜態連結）
        ▼
  musl（C 函式庫，MIT 授權）  ← 把 printf、malloc、fopen… 轉成系統呼叫
        │  int 0x80，編號與 Linux i386 相同
        ▼
  kernel/user/syscall.c       ← 本專案實作的那一小部分 Linux 系統呼叫
        │
        ▼
  kernel/vfs（檔案）、kernel/memory、console、時鐘…
```

好處：任何能編成「靜態 32 位元 Linux 程式」的 C 程式，只要用到的系統呼叫我們有實作，就能直接跑。你在 host 上也能直接執行同一個檔案（x86-64 Linux 通常能跑 32 位元程式），所以可以用 host 當「標準答案」比對輸出。

名詞：
- **ring 3**：x86 CPU 的低權限模式；kernel 在 ring 0。
- **系統呼叫（syscall）**：程式向 kernel 要服務的方式。編號放 EAX，參數放 EBX、ECX、EDX、ESI、EDI、EBP，結果回 EAX（負數 = `-errno`）。
- **ELF**：Linux 的執行檔格式。**靜態連結**：所有函式庫都已經包在檔案裡，不需要動態連結器。
- **musl**：精簡的 C 函式庫，原生支援 i386 Linux。
- **initrd**：開機時一起載入的「程式光碟」，是一個 tar 檔（GRUB 的 `module`，名稱 `initrd`）。

## 2. 快速開始

需要：`gcc`（能 `-m32` 編譯即可，不需要 32 位元的 libc）、`curl`、`tar`、`qemu-system-i386`、`grub-mkrescue`。第一次會從 musl 官網下載原始碼（約 1 MB）到 `third_party/downloads/musl/`（不進 git），並驗證 SHA-256。

```bash
make sdk        # 建立 SDK：build/sdk/sysroot（musl 的 libc.a、標頭、crt）
make initrd     # 編譯 userland/*.c 並打包成 build/initrd.tar
make run        # 開機（ISO 內含 initrd）；在 HAL> 提示輸入：run /bin/hello
```

你自己的程式：

```bash
tools/68khal-cc -O2 myprog.c -o myprog      # 與 gcc 用法相同，輸出靜態 i386 ELF
./myprog                                     # host 也能直接跑，確認程式本身沒問題
```

把它放進 OS：

1. 放到 `userland/myprog.c`（Makefile 會自動編譯進 `/bin/myprog`），
2. `make initrd && make run`，
3. 在 shell 輸入 `run /bin/myprog 參數…`。

想要檔案資料：放進 `userland/data/`，執行時在 `/data/…`。

## 2.5 已移植的軟體（證明 SDK 夠用）

| 程式 | 來源 | 怎麼建 | 試試看 |
|---|---|---|---|
| **Lua 5.4.7**（MIT） | lua.org 官方 tarball，SHA-256 驗證 | `tools/build_lua.sh`（`make initrd` 會呼叫） | `run /bin/lua -e print(6*7)` 或 `run /bin/lua /data/test.lua` |
| **TinyCC 0.9.27**（LGPL-2.1） | download.savannah.gnu.org，SHA-256 驗證 | `tools/build_tcc.sh` | 見下 |

原始碼放在 `third_party/downloads/`（被 `.gitignore` 排除，不進 repo）。

**在 OS 裡編譯 C 程式**：tcc 是用 SDK 編成 i386 靜態程式，initrd 帶著它需要的標頭與函式庫（`/usr/include`、`/usr/lib`）。

```
HAL> run /bin/tcc -static -o /tmp/h /data/tcc_hello.c
HAL> run /tmp/h
compiled by tcc: fib(20)=6765 1099511627 ok 6.28
```

重點：一定要加 `-static`（沒有動態連結器）；輸出放 `/tmp`（唯一可寫）；`run` 可以執行任何 VFS 上的 ELF。想編自己的程式，可以放進 `userland/data/` 再 `make initrd`。tcc 的程式碼產生品質不如 gcc，但足夠做小工具與實驗。

## 3. 檔案樹

```
/        程式光碟（initrd，唯讀）：/bin、/data、/expected
/tmp     RAM 檔案系統（可讀寫，重開機就消失）
/dev     console（螢幕 + 序列埠輸出、鍵盤輸入）、tty、null、zero、urandom
/hfs     Mac 磁碟的 HFS volume（唯讀；資料叉；Mac 文字檔用 CR 換行）
```

- 沒有「目前目錄」：路徑一律從 `/` 開始（`getcwd` 回 `/`）；相對路徑會失敗。
- `/` 的目錄列表不會顯示 `/tmp`、`/dev`、`/hfs`（掛載點），但直接開它們沒問題。
- stdin（fd 0）是鍵盤，一次一行，有回顯與 Backspace；沒有 Ctrl-D（EOF）。stdout/stderr 同時出現在螢幕與序列埠。

## 4. 位址空間

```
0x08048000 …  程式本身（ELF 的 PT_LOAD），其後是 brk 堆積（最多到 0x0F000000）
0x50000000 …  mmap 區（匿名記憶體與檔案的私有副本），由低往高配置
0x7FE00000 …  堆疊（2 MiB），頂端 0x80000000
其他          kernel 與裝置：使用者程式碰到會被殺掉（見下）
```

所有使用者頁面都以「可讀寫」映射（`mprotect` 是空操作）。kernel 的頁面沒有 User 位元，程式碰到就 page fault；kernel 把程式殺掉並印出：

```
/bin/crash: segmentation fault at 100000 (eip 8049123)
```

結束碼是 128 + 訊號編號（SIGSEGV = 139）。一次只能執行一個程式（還沒有多工）。

## 5. 已實作的系統呼叫

`kernel/user/syscall.c`。編號是 Linux i386 的（`arch/x86/entry/syscalls/syscall_32.tbl`）。

| 類別 | 系統呼叫 |
|---|---|
| 結束 | `exit`、`exit_group` |
| 檔案 | `read`、`write`、`readv`、`writev`、`open`、`openat`（僅絕對路徑）、`close`、`lseek`、`_llseek`、`pread64`、`ftruncate`、`fsync`、`dup`、`dup2`、`fcntl64` |
| 檔案資訊 | `stat64`、`lstat64`、`fstat64`、`fstatat64`、`statx`、`getdents64`、`access`、`faccessat`、`readlink`（一律 EINVAL） |
| 目錄 | `mkdir`、`mkdirat`、`unlink`、`unlinkat`、`rmdir`、`rename`、`renameat`、`getcwd` |
| 記憶體 | `brk`、`mmap2`（匿名與檔案私有）、`munmap`、`mremap`（一律搬移）、`mprotect`、`madvise` |
| 時間 | `clock_gettime`、`clock_gettime64`、`gettimeofday`、`time`、`nanosleep`、`clock_nanosleep(_time64)` |
| 行程資訊 | `getpid`、`gettid`、`getppid`、`getuid32` 等、`uname`、`getrlimit`、`ugetrlimit`、`umask` |
| 執行緒 | `set_thread_area`（TLS，GDT 項目 0x33）、`set_tid_address` |
| 訊號 | `rt_sigaction`、`rt_sigprocmask`（接受並忽略）；`kill`/`tgkill` 對自己 = 結束程式 |
| 其他 | `ioctl`（一律 `ENOTTY`）、`getrandom` |

沒實作的呼叫回傳 `-ENOSYS`，並在 console 印一次：

```
/bin/foo: unimplemented system call 119
```

### 遇到 ENOSYS 時怎麼補

1. 用編號查 `syscall_32.tbl`（或 `man 2 <名稱>`）知道它叫什麼、參數是什麼。
2. 在 host 上用 `strace ./myprog` 看實際怎麼呼叫（程式在 host 也能跑）。
3. 在 `kernel/user/syscall.c`：加 `#define SYS_xxx 編號`、寫一個 `sys_xxx()`、在 `user_syscall()` 的 `switch` 加 `case`。
4. 所有使用者指標都要先用 `user_range_ok()` / `copy_in()` / `copy_out()` / `user_copy_string()` 檢查，錯誤回 `-K_EFAULT`。
5. 檔案相關的操作放 `kernel/vfs/`（新的檔案系統實作 `vfs_fs_t`，用 `vfs_mount()` 掛上去）。
6. 寫一個測試程式放 `userland/`，並在 `userland/expected/<名稱>.txt` 放預期輸出；`kernel/test/selftest.c` 的 `user_tests` 表加一行。

## 6. 它是怎麼運作的（給想讀程式碼的人）

**進入 ring 3**（`arch/x86/user/user.asm`）：GDT 有 ring 3 的程式碼/資料段（選擇子 0x1B、0x23）和 TSS（0x28）。`iret` 可以「回到」一個本來不存在的呼叫者：我們手工在堆疊上放好 `ss, esp, eflags, cs, eip`，執行 `iret`，CPU 就切到 ring 3。TSS 的 `esp0` 告訴 CPU：之後程式 `int 0x80` 或被中斷時，換到哪個 kernel 堆疊（Intel SDM Vol.3A 6.12.1、7.2）。

**回到 kernel**：`exit` 系統呼叫呼叫 `hal_user_exit()`，它把堆疊指標直接設回進入前存下的位置並 `ret`，彷彿 `hal_user_run()` 剛剛才結束（像 `longjmp`）。

**分頁**：使用者頁面用 `HAL_MMU_USER` 映射（PTE 與 PDE 的 U/S 位元，兩層都為 1 才能存取，Intel SDM Vol.3A 4.6.1）。kernel 頁面的 PTE U/S = 0，所以 ring 3 碰不到。

**載入**（`kernel/user/process.c`、`elf.c`）：把檔案讀進記憶體 → `elf_parse` 檢查 → 對每個 `PT_LOAD` 配頁面並複製 → 建堆疊（`stack.c`：argc、argv、envp、auxiliary vector：`AT_PHDR`、`AT_PAGESZ`、`AT_RANDOM`…，musl 需要它們）→ `hal_user_run`。

**安全檢查**：`user_regions_t` 記錄程式擁有哪些位址範圍；每個系統呼叫收到的指標先檢查在範圍內才讀寫，所以壞指標回 `-EFAULT` 而不是讓 kernel 當掉。

**TLS**：musl 的 i386 版用 `set_thread_area` 設一個段，`%gs:0` 指向執行緒資料。kernel 的 GDT 項目 0x33 就是它。

## 7. 限制（已知）

- 一次一個程式；沒有 `fork`/`execve`/`pipe`/`socket`/`poll`、執行緒、訊號。
- 所有頁面可寫可執行；沒有 NX。
- 堆疊固定 2 MiB，不會自動成長。
- `mmap` 的位址只增不減（`munmap` 釋放頁面，但位址不重用）；不支援 `MAP_FIXED`。
- FPU/SSE 狀態不會在 kernel 與程式之間保存（kernel 不用浮點）。
- 檔案系統沒有權限、時間戳記（`mtime` 為 0）、符號連結。

## 8. 測試

- `tests/host/test_elf.c`、`test_user_stack.c`、`test_region.c`、`test_vfs.c`：不需硬體的部分。
- `kernel/test/selftest.c` 的 `test_user_programs`：在 QEMU 中執行 `userland/` 的範例程式，比對輸出與 `userland/expected/*.txt`（同一份輸出在 host 上用 `unshare -Ur chroot` 跑也相同，這就是預期檔的來源）。
- `arch/x86/test/selftest_x86.c`：手寫機器碼測試 ring 3 切換、系統呼叫、page fault、特權指令。
- `make test-shell`：開機 ISO，用 QEMU monitor 打字執行 `run …`，檢查序列埠輸出（`tools/shell_test.py`）。
