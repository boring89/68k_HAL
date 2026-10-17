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
| **SQLite 3.54.1**（公有領域） | sqlite.org 的 amalgamation，用官網公布的 SHA3-256 驗證 | `tools/build_sqlite.sh`（編譯約 35 秒） | `run /bin/sqlite3 /tmp/db .read /data/test.sql`（輸出與 host 的逐字相同，包含 `PRAGMA integrity_check`、`VACUUM`） |
| **dash 0.5.12**（BSD，Herbert Xu；`/bin/dash`，也是 `/bin/sh`） | gondor.apana.org.au 的官方 tarball，SHA-256 驗證 | `tools/build_dash.sh` | `run /bin/dash /data/test.sh`：管線、子 shell、命令替換、函式、here-document、背景工作與 `wait`、`trap` 與 `kill`（輸出與 host 逐字相同）。用到 fork／execve／wait4／pipe／sigsuspend 等（`docs/process.md`） |
| **GNU make 4.4.1**（GPL-3.0-or-later；`/bin/make`） | ftp.gnu.org 的官方 tarball，SHA-256 驗證 | `tools/build_make.sh` | `run /bin/make -f /data/test.mk`：make 用 dash 執行規則，規則呼叫 tcc 編譯並執行程式 |
| **Kilo**（BSD-2-Clause，antirez） | GitHub 固定 commit 的單一檔案 `kilo.c`，SHA-256 驗證 | `tools/build_kilo.sh` | `run /bin/kilo /tmp/note.txt`：Ctrl-S 存檔、Ctrl-Q 結束、Ctrl-F 搜尋 |

Kilo 是終端機的壓力測試：raw 模式、ESC 序列的畫面更新、視窗大小、方向鍵（見 5.5）。它還挖出兩個問題：`ftruncate64` 沒實作；以及「寫滿整行時要延遲換行」（像 xterm，否則狀態列每行多一個空行）。

SQLite 是很好的「系統呼叫壓力測試」：它用到 `fcntl` 檔案鎖（單一程式，所以鎖一律成功）、`pread/pwrite`、`fsync`、`ftruncate`、`fchmod/fchown`（接受並忽略）、日誌檔的建立與刪除。移植過程發現並修正：程式映像一頁一個 region 會超過 128 個上限（現在相鄰的 region 會合併）；缺 `pwrite64`、檔案鎖與 `fchmod`。

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
/proc    version、uptime、meminfo、cpuinfo（見 5.10）
/dev     console（螢幕 + 序列埠輸出、鍵盤輸入）、tty、null、zero、urandom、fb0（圖形模式）、input/event0（鍵盤與滑鼠事件）
/hfs     Mac 磁碟的 HFS volume（唯讀；資料叉；Mac 文字檔用 CR 換行）
```

- 有「目前目錄」（見 5.7）：相對路徑從那裡開始；全系統只有一個（shell 與它執行的程式共用，程式結束後還原）。`openat` 的 `dirfd` 被忽略（只有 `AT_FDCWD` 正確）。
- `/` 的目錄列表不會顯示 `/tmp`、`/dev`、`/hfs`（掛載點），但直接開它們沒問題。
- stdin（fd 0）是鍵盤，一次一行，有回顯與 Backspace；沒有 Ctrl-D（EOF）。stdout/stderr 同時出現在螢幕與序列埠。

## 4. 位址空間

```
0x08048000 …  程式本身（ELF 的 PT_LOAD），其後是 brk 堆積（最多到 0x0F000000）
0x50000000 …  mmap 區（匿名記憶體與檔案的私有副本），由低往高配置
0x7FE00000 …  堆疊（2 MiB），頂端 0x80000000
其他          kernel 與裝置：使用者程式碰到會被殺掉（見下）
```

**頁面保護**：沒有寫入旗標的 ELF segment（程式碼、常數）載入後改成唯讀（`hal_mmu_protect`），所以寫入常數會被殺掉（結束碼 139）；`mprotect` 與 `mmap` 的 `prot` 真的有效：`PROT_NONE` = 使用者碰不到的保護頁，唯讀頁寫入會 page fault。沒有 NX（需要 PAE），所以「可讀」等於「可執行」。kernel 替程式寫入使用者記憶體前會檢查頁面可寫（`user_range_writable`），所以 `read()` 到唯讀頁回 `EFAULT` 而不是讓 kernel 當掉。kernel 的頁面沒有 User 位元，程式碰到就 page fault；kernel 把程式殺掉並印出：

```
/bin/crash: segmentation fault at 100000 (eip 8049123)
```

結束碼是 128 + 訊號編號（SIGSEGV = 139）。可以同時有很多個行程（`fork`、`execve`、管線、訊號，見 `docs/process.md`）。

## 5. 已實作的系統呼叫

`kernel/user/syscall.c`。編號是 Linux i386 的（`arch/x86/entry/syscalls/syscall_32.tbl`）。

| 類別 | 系統呼叫 |
|---|---|
| 結束 | `exit`、`exit_group` |
| 檔案 | `fdatasync`、`chmod/fchmod/chown32/fchown32`（空操作）、檔案鎖（`fcntl` 的 `F_SETLK` 等，一律成功）、`read`、`write`、`readv`、`writev`、`open`、`openat`（僅絕對路徑）、`close`、`lseek`、`_llseek`、`pread64`、`ftruncate`、`ftruncate64`、`fsync`、`dup`、`dup2`、`fcntl64` |
| 檔案資訊 | `stat64`、`lstat64`、`fstat64`、`fstatat64`、`statx`、`getdents64`、`access`、`faccessat`、`readlink`（一律 EINVAL） |
| 目錄 | `mkdir`、`mkdirat`、`unlink`、`unlinkat`、`rmdir`、`rename`、`renameat`、`getcwd`、`chdir` |
| 記憶體 | `brk`、`mmap2`（匿名與檔案私有，`prot` 有效）、`munmap`、`mremap`（一律搬移）、`mprotect`（真的改頁面權限）、`madvise`（忽略） |
| 時間 | `clock_gettime`、`clock_gettime64`、`gettimeofday`、`time`、`nanosleep`、`clock_nanosleep(_time64)` |
| 行程資訊 | `getpid`、`gettid`、`getppid`、`getuid32` 等、`uname`、`getrlimit`、`ugetrlimit`、`umask` |
| 執行緒 | `set_thread_area`（TLS，GDT 項目 0x33）、`set_tid_address` |
| 行程與訊號 | `fork`、`vfork`、`clone`（fork 語意）、`execve`、`waitpid`、`wait4`、`pipe`、`pipe2`、`dup3`、`kill`、`tkill`、`tgkill`、`rt_sigaction`、`rt_sigprocmask`、`rt_sigpending`、`rt_sigsuspend`、`rt_sigreturn`、`pause`、`getpgid`、`setpgid`、`setsid`（見 `docs/process.md`） |
| 網路 | `socketcall`（`socket`、`bind`、`connect`、`listen`、`accept`、`send`/`recv`/`sendto`/`recvfrom`/`sendmsg`/`recvmsg`、`shutdown`、`getsockname`/`getpeername`、`setsockopt`/`getsockopt`，見 `docs/net.md` 3.5） |
| 等待 | `poll`、`ppoll`、`_newselect`、`pselect6`（檔案、console 與 socket） |
| 其他 | `ioctl`（console：termios、視窗大小、`FIONREAD`，見 5.5；`/dev/fb0`，見 5.6；其他檔案 `ENOTTY`）、`getrandom` |

沒實作的呼叫回傳 `-ENOSYS`，並在 console 印一次：

```
/bin/foo: unimplemented system call 119
```

### 5.5 終端機：ANSI 輸出與 termios 輸入

比喻：console 以前像一台只能印字的電報機；現在是真正的終端機：程式可以叫它「移到第 3 列第 5 欄」「用紅色印」，也可以叫鍵盤「不要等 Enter，按一下就給我」。

**輸出（`kernel/terminal/ansi.c`）**：程式寫到 console 的位元組先經過 ANSI 解析器（ECMA-48 的 CSI 子集）：游標移動（`A B C D E F G H f d`、`s u` 存取）、清除（`J K`）、顏色（`m`：0、1、7、22、27、30–37、40–47、90–97、100–107、39、49；ANSI 的顏色順序轉成 VGA 的順序，粗體 = 亮色）。`\r` 回到行首、`\t` 到下一個 8 的倍數。不認得的序列讀完後忽略。原始位元組**照樣**送到序列埠，所以接在序列埠另一頭的終端機程式看到一樣的畫面。`isatty(1)` 現在是真的，所以 stdout 變成行緩衝。

**輸入（`kernel/user/console.c`、`kernel/keyboard/keyseq.c`）**：console 有 Linux 風格的 `struct termios`（`TCGETS/TCSETS/TCSETSW/TCSETSF`，36 位元組的核心格式）：

| 設定 | 效果 |
|---|---|
| `ICANON`（預設開） | 一次一行，行編輯（Backspace、Ctrl-U、Ctrl-D）；方向鍵與功能鍵在行編輯時忽略 |
| 關掉 `ICANON`（raw） | 每個按鍵立刻可讀；`VMIN`／`VTIME` 決定 `read` 等多久（`termios(3)` 的四種組合） |
| `ECHO` | 打的字顯示在螢幕上 |
| `ISIG` | Ctrl-C（`VINTR`）結束程式（狀態 130），Ctrl-\（`VQUIT`）= 131；只在程式讀鍵盤時（`read`/`poll`）才會處理，空轉的迴圈停不下來 |
| `ICRNL` | Enter 的 `\r` 轉成 `\n` |

按鍵變成的位元組（xterm／Linux console）：方向鍵 `ESC [ A/B/C/D`、Home／End `ESC [ H`／`ESC [ F`、Delete `ESC [ 3 ~`、F1–F4 `ESC O P…S`、F5–F12 `ESC [ 15~…24~`、Alt+鍵 = ESC 加上該鍵、Backspace = 0x7F、Enter = `\r`。程式結束（或被 Ctrl-C 終止）時 termios 與顏色回到預設，所以 shell 不會被留在 raw 模式。`ioctl(TIOCGWINSZ)` 回傳螢幕的列數與欄數（文字模式 25×80）。

### 5.6 圖形與輸入：`/dev/fb0`、`/dev/input/event0`

比喻：framebuffer 是一張「直接貼在螢幕上的畫布」；程式把畫布借過來（`mmap`），畫什麼螢幕就顯示什麼，不用經過任何 kernel 呼叫。輸入事件則像一條「按鍵與滑鼠的傳送帶」，每個事件是一張 16 位元組的小卡片（誰、按下或放開）。

**`/dev/fb0`**（`kernel/user/fbdev.c`，Linux fbdev 介面；標頭 `<linux/fb.h>` 在 `sdk/include/linux/`）：

```c
int fd = open("/dev/fb0", O_RDWR);
struct fb_var_screeninfo var;  ioctl(fd, FBIOGET_VSCREENINFO, &var);   /* xres, yres, bits_per_pixel = 32, red.offset = 16 */
struct fb_fix_screeninfo fix;  ioctl(fd, FBIOGET_FSCREENINFO, &fix);   /* line_length（每列位元組數） */
uint32_t *px = mmap(NULL, fix.line_length * var.yres, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
px[y * (fix.line_length / 4) + x] = 0x00RRGGBB;
```

`mmap` 把使用者頁面**直接映射到顯示卡的記憶體**（`vfs_file_ops_t.mmap` 回傳實體位址；`user_map_device` 記錄這段範圍，所以 `munmap` 與程式結束時不會把這些 frame 還給 PMM，它們不是 RAM）。也可以 `lseek` + `write`。`FBIOPUT_VSCREENINFO` 給另一個 `xres/yres` 會要求顯示卡切模式（QEMU 標準 VGA 可以，見 `docs/gpu.md`）。程式結束時 kernel 把文字終端重畫（`fbdev_program_ended`；可能是新模式）。文字模式的核心沒有 framebuffer：`open` 回 `ENODEV`，`ls /dev` 也不列出 `fb0`；請用 GRUB 的 graphics 項目。

**`/dev/input/event0`**（`kernel/user/inputdev.c`，標頭 `<linux/input.h>`）：鍵盤與滑鼠合在一起，格式與 Linux evdev 相同：

| 事件 | type / code / value |
|---|---|
| 按鍵 | `EV_KEY` / Linux 的 `KEY_*`（`kernel/input/linux_keys.c`：Q = 16、Enter = 28、↑ = 103…）/ 1 按下、0 放開（沒有重複） |
| 滑鼠移動 | `EV_REL` / `REL_X` 0、`REL_Y` 1（+y 向下）/ 位移 |
| 滑鼠按鍵 | `EV_KEY` / `BTN_LEFT` 0x110、`BTN_RIGHT` 0x111、`BTN_MIDDLE` 0x112 / 1、0 |
| 一組結束 | `EV_SYN` / 0 / 0 |

`read` 一次給整數個事件（緩衝區至少 16 位元組）；沒有事件時等待，`O_NONBLOCK` 回 `EAGAIN`；`poll`/`select` 可用。打開時丟掉之前累積的事件。注意：鍵盤事件同時只有一個消費者：讀 `event0` 的程式在跑的時候，console 的行編輯收不到按鍵。

範例：`userland/evtest.c`（印出事件，Q 結束）、`userland/fbdemo.c`（在 `/dev/fb0` 畫方塊，Q 結束）。

### 5.7 目前目錄與工具箱 `box`

**目前目錄**（`kernel/vfs/vfs.c`）：`vfs_getcwd()`／`vfs_chdir()`。不是以 `/` 開頭的路徑，在 `vfs_normalize` 前面接上目前目錄再整理（`.`、`..`、重複的斜線）。shell 有 `cd [路徑]` 和 `pwd`；程式用 `chdir`／`getcwd`。`user_run` 執行前記下目前目錄、結束後還原，所以程式裡的 `chdir` 不會把 shell 帶走。

**工具箱 `/bin/box`**（`userland/box.c`，自己寫的，GPL-3.0）：BusyBox 的做法，一個程式裝很多小指令，依啟動時 `argv[0]` 的名字決定用哪一個：`cat cp mv rm [-rf] mkdir [-p] rmdir touch ls [-al] pwd echo wc [-lwc] head tail [-n] grep [-ivnc]（固定字串）sort [-rn] uniq tee [-a] hexdump [-C] seq sleep uname [-a] date env basename dirname stat clear true false`。用法：

- 每個指令在 initrd 裡是 `/bin/box` 的**硬連結**（`/bin/cp`、`/bin/mkdir`…；tar 的 hard link 項目，`tarfs` 讀得懂，不佔額外空間；與 `userland/` 裡同名的獨立程式如 `sort` 並存時以獨立程式為準）。shell 打不認得的指令時，會找 `/bin/<名稱>` 執行（名稱帶斜線就當路徑），所以直接打 `cp a b`、`mkdir x`、`grep foo file`、`wc -l file` 就行；找不到才說 `Unknown command`；
- `box ls /tmp`、`box --list`（列出指令）。

shell 內建的 `ls`、`cat`、`echo`、`date` 是舊的（`ls`／`cat` 讀 HFS 磁碟），內建優先；要用 box 的版本請打 `box ls …`。系統限制：只有 `/tmp` 可以建立檔案，沒有權限、連結、擁有者（所以沒有 `chmod`、`ln`、`chown`）。

### 5.8 shell 的行編輯

`kernel/terminal/input.c`：← → Home End Delete Backspace、Ctrl-A/E/U/K/W、↑ ↓ 歷史（16 行）、Tab 補全（第一個字：內建指令與 `/bin` 的程式；其他字：目前目錄的檔案，含斜線的路徑在該目錄找；`kernel/shell/complete.c`）、Ctrl-C 放棄這一行、Ctrl-L 清畫面。原理：編輯器記得「這一行第一個字元在螢幕上的線性位置」（列 × 寬 + 欄），每次修改就從改動處往後重畫，並處理換行與畫面捲動（捲動後那一行整體上移，用實際游標位置校正）。序列埠只看到鍵入的字（重畫時暫停鏡射）。沒有顯示器時只支援在行尾輸入與 Backspace。測試：`tests/host/test_input.c`、`test_shell_complete.c`、`make test-shell`。

### 5.9 重導向與管線

shell 行裡的 `|`、`<`、`>`、`>>`（各自要用空白隔開）：`seq 20 | grep 1 | wc -l`、`seq 3 > /tmp/n.txt`、`wc -l < /tmp/n.txt`、`echo more >> /tmp/n.txt`。各段是**同時執行**的行程，之間用真正的管線（`kernel/user/pipe.c`，見 `docs/process.md` 第 6 節），所以 `seq 1000000 | head` 這種前一段輸出很多的也行；Ctrl-C 對整個管線有效。只有程式參與，內建指令不行（行裡有運算子時 `echo`、`cat` 等用 `/bin` 的版本）；最多 4 段。

### 5.10 `/proc`

`kernel/vfs/procfs.c`（通用：唯讀文字檔，內容在 `open` 時才產生，像 Linux 的 /proc；產生函式註冊在表裡）與 `kernel/user/procfiles.c`（實際的檔案）：`/proc/version`、`/proc/uptime`（「秒.百分之一秒 0.00」）、`/proc/meminfo`（MemTotal、MemFree、HeapUsed、HeapTotal，單位 kB）、`/proc/processes`（行程表：pid、ppid、pgid、狀態 R/S/Z、名稱；box 的 `ps` 讀它，`kill [-n] pid` 送訊號）、`/proc/cpuinfo`（每個執行中的處理器一段，格式同 Linux）。內容在 `open` 的那一刻定格（讀到一半不會變）。box 的 `free`、`uptime` 讀它們（`free` 與 shell 內建的 `free` 同名，請打 `box free`）。

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

- 協作式多工、沒有執行緒、`alarm`、作業控制（見 `docs/process.md` 第 9 節）。
- 終端機：沒有 UTF-8（VGA 文字模式只有 CP437）、不是所有 CSI 都支援（沒有捲動區域、替代螢幕、滑鼠）、圖形模式下沒有畫面（只有序列埠）；termios 沒有 `IXON`/`OPOST` 的實際作用、鮑率設定被忽略。
- 沒有 NX（需要 PAE）：可讀的頁面都能執行；`PROT_EXEC` 被忽略。
- 堆疊固定 2 MiB（用到的頁面才配置，見 `docs/process.md`），超過就 segmentation fault。
- `mmap` 的位址只增不減（`munmap` 釋放頁面，但位址不重用）；不支援 `MAP_FIXED`。
- FPU/SSE 狀態不會在 kernel 與程式之間保存（kernel 不用浮點）。
- 檔案系統沒有權限、時間戳記（`mtime` 為 0）、符號連結。

## 8. 測試

- `tests/host/test_elf.c`、`test_user_stack.c`、`test_region.c`、`test_vfs.c`：不需硬體的部分。
- `kernel/test/selftest.c` 的 `test_user_programs`：在 QEMU 中執行 `userland/` 的範例程式，比對輸出與 `userland/expected/*.txt`（同一份輸出在 host 上用 `unshare -Ur chroot` 跑也相同，這就是預期檔的來源）。
- `arch/x86/test/selftest_x86.c`：手寫機器碼測試 ring 3 切換、系統呼叫、page fault、特權指令。
- `make test-shell`：開機 ISO，用 QEMU monitor 打字執行 `run …`，檢查序列埠輸出（`tools/shell_test.py`；`{up}`、`{f1}`、`{ctrl-c}` 這種寫法是按那個鍵）。raw 模式的 `rawkeys` 驗證按鍵序列，Ctrl-C 結束 `readline`；`evtest` 驗證鍵盤與滑鼠事件（`{mouse_move 7 3}` 是移動滑鼠）。`make test-fb`（`tools/fb_check.py`）開圖形模式執行 `fbdemo`，從 screendump 檢查 `/dev/fb0` 畫出的方塊，並檢查結束後終端回來。
- `tests/host/test_ansi.c`（解析器）、`test_keyseq.c`（按鍵序列）；`selftest` 的 `test_user_ansi` 把螢幕換成記錄用的假螢幕，執行 `userland/ansi.c`，檢查字元、位置與顏色；`userland/termios.c`（raw、VMIN/VTIME、還原）在 `user_tests` 表。
