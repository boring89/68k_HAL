/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "process.h"

#include "hal/clock.h"
#include "hal/mmu.h"
#include "hal/cpu.h"
#include "hal/timer.h"
#include "hal/usermode.h"

#include "kernel/lib/k_errno.h"
#include "kernel/lib/k_memory.h"
#include "kernel/lib/k_string.h"
#include "kernel/lib/k_time.h"
#include "kernel/net/net.h"
#include "kernel/printf/printf.h"
#include "kernel/vfs/vfs.h"

/*
 * Linux i386 system calls (the numbers are from arch/x86/entry/syscalls/
 * syscall_32.tbl; the structure layouts are the i386 ones, "struct
 * stat64" and so on). A program built with musl uses only a small
 * part of them. The ones that are not implemented return -ENOSYS and
 * are listed on the console, so it is easy to see what a new program
 * needs (docs/userland.md).
 */
#define SYS_exit 1
#define SYS_read 3
#define SYS_write 4
#define SYS_open 5
#define SYS_close 6
#define SYS_unlink 10
#define SYS_time 13
#define SYS_access 33
#define SYS_kill 37
#define SYS_rename 38
#define SYS_mkdir 39
#define SYS_rmdir 40
#define SYS_dup 41
#define SYS_brk 45
#define SYS_ioctl 54
#define SYS_umask 60
#define SYS_dup2 63
#define SYS_getppid 64
#define SYS_getrlimit 76
#define SYS_gettimeofday 78
#define SYS_readlink 85
#define SYS_munmap 91
#define SYS_ftruncate 93
#define SYS_fsync 118
#define SYS_uname 122
#define SYS_mprotect 125
#define SYS__llseek 140
#define SYS_readv 145
#define SYS_writev 146
#define SYS_nanosleep 162
#define SYS_rt_sigreturn 173
#define SYS_rt_sigaction 174
#define SYS_rt_sigprocmask 175
#define SYS_pread64 180
#define SYS_pwrite64 181
#define SYS_getcwd 183
#define SYS_ugetrlimit 191
#define SYS_mmap2 192
#define SYS_stat64 195
#define SYS_lstat64 196
#define SYS_fstat64 197
#define SYS_getuid32 199
#define SYS_getgid32 200
#define SYS_geteuid32 201
#define SYS_getegid32 202
#define SYS_getdents64 220
#define SYS_fcntl64 221
#define SYS_gettid 224
#define SYS_tkill 238
#define SYS_set_thread_area 243
#define SYS_exit_group 252
#define SYS_set_tid_address 258
#define SYS_clock_gettime 265
#define SYS_clock_getres 266
#define SYS_tgkill 270
#define SYS_openat 295
#define SYS_mkdirat 296
#define SYS_fstatat64 300
#define SYS_unlinkat 301
#define SYS_renameat 302
#define SYS_faccessat 307
#define SYS_getrandom 355
#define SYS_clock_gettime64 403
#define SYS_clock_nanosleep_time64 407
#define SYS_mremap 163
#define SYS_statx 383
#define SYS_socketcall 102
#define SYS__newselect 142
#define SYS_poll 168
#define SYS_pselect6 308
#define SYS_ppoll 309
#define SYS_pselect6_time64 413
#define SYS_ppoll_time64 414
#define SYS_chmod 15
#define SYS_fchmod 94
#define SYS_fdatasync 148
#define SYS_chown32 212
#define SYS_fchown32 207
#define SYS_getpid 20
#define SYS_lseek 19
#define SYS_getrusage 77
#define SYS_madvise 219
#define SYS_futex 240
#define SYS_clock_nanosleep 267
#define SYS_clock_getres_time64 406
#define SYS_getpgid 132
#define SYS_getsid 147

#define AT_FDCWD (-100)
#define PAGE 4096u

/* Syscall numbers seen without an implementation, so each is reported once. */
static uint16_t reported[32];
static unsigned int reported_count;

static int32_t enosys(uint32_t number)
{
    for (unsigned int i = 0; i < reported_count; i++)
    {
        if (reported[i] == number)
            return -K_ENOSYS;
    }

    if (reported_count < sizeof(reported) / sizeof(reported[0]))
        reported[reported_count++] = (uint16_t)number;

    kernel_printf("%s: unimplemented system call %u\n", user_process.name, number);

    return -K_ENOSYS;
}

/* --- helpers for user memory --- */

static int32_t copy_out(uint32_t address, const void *data, uint32_t length)
{
    if (!user_range_writable(address, length))
        return -K_EFAULT;

    k_memcpy((void *)(uintptr_t)address, data, length);

    return 0;
}

static int32_t copy_in(void *data, uint32_t address, uint32_t length)
{
    if (!user_range_ok(address, length))
        return -K_EFAULT;

    k_memcpy(data, (const void *)(uintptr_t)address, length);

    return 0;
}

static int32_t get_path(char *buffer, uint32_t address)
{
    int32_t length = user_copy_string(buffer, VFS_PATH_MAX, address);

    if (length < 0)
        return length;

    if (length == 0)
        return -K_ENOENT;

    return 0;
}

/* --- files --- */

static int32_t sys_open(uint32_t path_address, uint32_t flags, uint32_t mode)
{
    char path[VFS_PATH_MAX];
    int32_t status = get_path(path, path_address);

    if (status != 0)
        return status;

    vfs_file_t *file;

    status = vfs_open(path, flags, mode, &file);

    if (status != 0)
        return status;

    int32_t fd = user_fd_install(file, 0);

    if (fd < 0)
        vfs_file_close(file);

    return fd;
}

static int32_t sys_read(int fd, uint32_t address, uint32_t count)
{
    vfs_file_t *file = user_fd_get(fd);

    if (file == NULL)
        return -K_EBADF;

    if (!user_range_writable(address, count))
        return -K_EFAULT;

    return vfs_read(file, (void *)(uintptr_t)address, count);
}

static int32_t sys_write(int fd, uint32_t address, uint32_t count)
{
    vfs_file_t *file = user_fd_get(fd);

    if (file == NULL)
        return -K_EBADF;

    if (!user_range_ok(address, count))
        return -K_EFAULT;

    return vfs_write(file, (const void *)(uintptr_t)address, count);
}

/* struct iovec: { void *base; size_t len; } */
static int32_t sys_vector_io(int fd, uint32_t iov_address, uint32_t count, int writing)
{
    if (count > 1024)
        return -K_EINVAL;

    int32_t total = 0;

    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t entry[2];
        int32_t status = copy_in(entry, iov_address + i * 8, 8);

        if (status != 0)
            return total > 0 ? total : status;

        if (entry[1] == 0)
            continue;

        int32_t n = writing ? sys_write(fd, entry[0], entry[1])
                            : sys_read(fd, entry[0], entry[1]);

        if (n < 0)
            return total > 0 ? total : n;

        total += n;

        if ((uint32_t)n < entry[1])
            break;
    }

    return total;
}

static int32_t sys_lseek(int fd, int64_t offset, int whence, uint32_t result_address)
{
    vfs_file_t *file = user_fd_get(fd);

    if (file == NULL)
        return -K_EBADF;

    int64_t position = vfs_seek(file, offset, whence);

    if (position < 0)
        return (int32_t)position;

    if (result_address == 0)
        return (int32_t)position;

    /* _llseek returns the 64-bit position through a pointer. */
    return copy_out(result_address, &position, 8);
}

/* i386 struct stat64, 96 bytes (arch/x86/include/uapi/asm/stat.h). */
static int32_t fill_stat64(uint32_t address, const vfs_stat_t *st)
{
    uint8_t buffer[96];
    uint32_t time = st->mtime;

    k_memset(buffer, 0, sizeof(buffer));

#define PUT32(offset, value)                                     \
    do                                                           \
    {                                                            \
        uint32_t v_ = (value);                                   \
        k_memcpy(buffer + (offset), &v_, 4);                     \
    } while (0)

    PUT32(0, 1);                          /* st_dev */
    PUT32(12, st->inode);                 /* __st_ino */
    PUT32(16, st->mode);
    PUT32(20, 1);                         /* st_nlink */
    k_memcpy(buffer + 44, &st->size, 8);  /* st_size */
    PUT32(52, 4096);                      /* st_blksize */
    uint64_t blocks = (st->size + 511) / 512;
    k_memcpy(buffer + 56, &blocks, 8);
    PUT32(64, time);                      /* atime */
    PUT32(72, time);                      /* mtime */
    PUT32(80, time);                      /* ctime */
    PUT32(88, st->inode);                 /* st_ino (64-bit, low word) */
#undef PUT32

    return copy_out(address, buffer, sizeof(buffer));
}

static int32_t sys_stat(uint32_t path_address, uint32_t out_address)
{
    char path[VFS_PATH_MAX];
    int32_t status = get_path(path, path_address);

    if (status != 0)
        return status;

    vfs_stat_t st;

    status = vfs_stat(path, &st);

    return status != 0 ? status : fill_stat64(out_address, &st);
}

static int32_t sys_fstat(int fd, uint32_t out_address)
{
    vfs_file_t *file = user_fd_get(fd);

    if (file == NULL)
        return -K_EBADF;

    vfs_stat_t st;
    int32_t status = vfs_fstat(file, &st);

    return status != 0 ? status : fill_stat64(out_address, &st);
}

/* linux_dirent64: { u64 d_ino; s64 d_off; u16 d_reclen; u8 d_type; char d_name[]; } */
static int32_t sys_getdents64(int fd, uint32_t address, uint32_t count)
{
    vfs_file_t *file = user_fd_get(fd);

    if (file == NULL)
        return -K_EBADF;

    if (!user_range_writable(address, count))
        return -K_EFAULT;

    uint32_t used = 0;

    for (;;)
    {
        vfs_dirent_t entry;
        int32_t status = vfs_readdir(file, &entry);

        if (status < 0)
            return used > 0 ? (int32_t)used : status;

        if (status == 1)
            break;

        uint32_t name_length = (uint32_t)k_strlen(entry.name);
        uint32_t record = (19 + name_length + 1 + 7) & ~7u;

        if (used + record > count)
        {
            /* Does not fit: take the entry back for the next call. */
            file->pos--;

            if (used == 0)
                return -K_EINVAL;

            break;
        }

        uint8_t *p = (uint8_t *)(uintptr_t)(address + used);
        uint64_t inode = entry.inode;
        int64_t next = (int64_t)file->pos;
        uint16_t length = (uint16_t)record;

        k_memset(p, 0, record);
        k_memcpy(p, &inode, 8);
        k_memcpy(p + 8, &next, 8);
        k_memcpy(p + 16, &length, 2);
        p[18] = entry.type;
        k_memcpy(p + 19, entry.name, name_length + 1);
        used += record;
    }

    return (int32_t)used;
}

static int32_t sys_fcntl(int fd, uint32_t cmd, uint32_t arg)
{
    vfs_file_t *file = user_fd_get(fd);

    if (file == NULL)
        return -K_EBADF;

    switch (cmd)
    {
    case 0: /* F_DUPFD */
    case 1030: /* F_DUPFD_CLOEXEC */
    {
        int32_t n = user_fd_install(file, (int)arg);

        if (n >= 0)
            vfs_file_ref(file);

        return n;
    }

    case 1: /* F_GETFD */
    case 2: /* F_SETFD */
        return 0;

    case 3: /* F_GETFL */
        return (int32_t)file->flags;

    /* Locks: with one program running there is nobody to lock against, so every lock succeeds (SQLite needs this). */
    case 5:  /* F_GETLK */
    case 12: /* F_GETLK64 */
    {
        /* struct flock starts with the lock type: report "not locked" (F_UNLCK = 2). */
        int16_t unlocked = 2;

        return arg == 0 ? -K_EFAULT : copy_out(arg, &unlocked, 2);
    }

    case 6:  /* F_SETLK */
    case 7:  /* F_SETLKW */
    case 13: /* F_SETLK64 */
    case 14: /* F_SETLKW64 */
        return 0;

    case 4: /* F_SETFL: the flags a descriptor may change; sockets look at O_NONBLOCK */
        file->flags = (file->flags & ~04000u) | (arg & 04000u);
        user_socket_set_nonblock(file, (arg & 04000u) != 0);

        return 0;

    default:
        return -K_EINVAL;
    }
}

static int32_t sys_dup2(int old_fd, int new_fd)
{
    vfs_file_t *file = user_fd_get(old_fd);

    if (file == NULL)
        return -K_EBADF;

    if (new_fd < 0 || new_fd >= USER_MAX_FDS)
        return -K_EBADF;

    if (old_fd == new_fd)
        return new_fd;

    user_fd_close(new_fd);
    user_process.fd[new_fd] = vfs_file_ref(file);

    return new_fd;
}

static int32_t sys_ioctl(int fd, uint32_t request, uint32_t arg)
{
    vfs_file_t *file = user_fd_get(fd);

    if (file == NULL)
        return -K_EBADF;

    return vfs_ioctl(file, request, arg);
}

static int32_t sys_mkdir(uint32_t path_address, uint32_t mode)
{
    char path[VFS_PATH_MAX];
    int32_t status = get_path(path, path_address);

    return status != 0 ? status : vfs_mkdir(path, mode);
}

static int32_t sys_unlink(uint32_t path_address)
{
    char path[VFS_PATH_MAX];
    int32_t status = get_path(path, path_address);

    return status != 0 ? status : vfs_unlink(path);
}

static int32_t sys_rmdir(uint32_t path_address)
{
    char path[VFS_PATH_MAX];
    int32_t status = get_path(path, path_address);

    return status != 0 ? status : vfs_rmdir(path);
}

static int32_t sys_rename(uint32_t from_address, uint32_t to_address)
{
    char from[VFS_PATH_MAX];
    char to[VFS_PATH_MAX];
    int32_t status = get_path(from, from_address);

    if (status == 0)
        status = get_path(to, to_address);

    return status != 0 ? status : vfs_rename(from, to);
}

static int32_t sys_access(uint32_t path_address)
{
    char path[VFS_PATH_MAX];
    int32_t status = get_path(path, path_address);

    if (status != 0)
        return status;

    vfs_stat_t st;

    return vfs_stat(path, &st);
}

static int32_t sys_getcwd(uint32_t address, uint32_t size)
{
    if (size < 2)
        return -K_ERANGE;

    int32_t status = copy_out(address, "/", 2);

    return status != 0 ? status : 2; /* the length including the NUL */
}

/* --- memory --- */

static int32_t sys_brk(uint32_t requested)
{
    user_process_t *p = &user_process;

    if (requested == 0 || requested < p->brk_start || requested > USER_BRK_LIMIT)
        return (int32_t)p->brk;

    uint32_t old_end = (p->brk + PAGE - 1) & ~(PAGE - 1);
    uint32_t new_end = (requested + PAGE - 1) & ~(PAGE - 1);

    if (new_end > old_end)
    {
        /* Each page is its own region: it joins the heap, and user_unmap can take it back. */
        if (user_map(old_end, new_end) != 0)
            return (int32_t)p->brk;
    }
    else if (new_end < old_end)
    {
        user_unmap(new_end, old_end);
    }

    p->brk = requested;

    return (int32_t)p->brk;
}

#define MAP_FIXED 0x10u
#define MAP_ANONYMOUS 0x20u

static int32_t protect_pages(uint32_t addr, uint32_t length, uint32_t prot);

static int32_t sys_mmap2(uint32_t addr, uint32_t length, uint32_t prot, uint32_t flags,
                         int fd, uint32_t pgoffset)
{
    user_process_t *p = &user_process;

    (void)addr;

    if (length == 0 || (flags & MAP_FIXED))
        return -K_EINVAL;

    uint32_t size = (length + PAGE - 1) & ~(PAGE - 1);

    if (size < length || size > USER_MMAP_LIMIT - p->mmap_next)
        return -K_ENOMEM;

    vfs_file_t *file = NULL;

    if (!(flags & MAP_ANONYMOUS))
    {
        file = user_fd_get(fd);

        if (file == NULL)
            return -K_EBADF;
    }

    uint32_t start = p->mmap_next;
    int32_t status = user_map(start, start + size);

    if (status != 0)
        return status;

    p->mmap_next = start + size;

    if (file != NULL)
    {
        /* A private mapping of a file is a copy of its contents. */
        uint64_t saved = file->pos;
        uint32_t done = 0;

        file->pos = (uint64_t)pgoffset * PAGE;

        while (done < length)
        {
            int32_t n = vfs_read(file, (void *)(uintptr_t)(start + done), length - done);

            if (n <= 0)
                break;

            done += (uint32_t)n;
        }

        file->pos = saved;
    }

    /* A mapping that is not writable (or not even readable: a guard area). */
    if (!(prot & 2))
        protect_pages(start, size, prot);

    return (int32_t)start;
}

/*
 * mprotect: PROT_READ (1), PROT_WRITE (2), PROT_EXEC (4). The CPU (without PAE)
 * has no execute bit, so readable means executable; PROT_NONE makes the page
 * unreachable for the program (and a guard page).
 */
static int32_t protect_pages(uint32_t addr, uint32_t length, uint32_t prot)
{
    uint32_t end = (addr + length + PAGE - 1) & ~(PAGE - 1);
    uint32_t flags = prot == 0 ? 0 : (HAL_MMU_USER | ((prot & 2) ? HAL_MMU_WRITABLE : 0));

    if ((addr & (PAGE - 1)) || end < addr)
        return -K_EINVAL;

    if (length == 0)
        return 0;

    if (!user_regions_contains(&user_process.regions, addr, end - addr))
        return -K_ENOMEM;

    for (uint32_t page = addr; page < end; page += PAGE)
        hal_mmu_protect(page, flags);

    return 0;
}

static int32_t sys_munmap(uint32_t addr, uint32_t length)
{
    if (addr & (PAGE - 1) || length == 0)
        return -K_EINVAL;

    uint32_t end = (addr + length + PAGE - 1) & ~(PAGE - 1);

    if (addr < USER_MMAP_BASE || end > USER_MMAP_LIMIT || end < addr)
        return -K_EINVAL;

    /* Pages that are not mapped are skipped, like Linux. */
    for (uint32_t page = addr; page < end; page += PAGE)
    {
        if (user_range_ok(page, 1))
            user_unmap(page, page + PAGE);
    }

    return 0;
}

/*
 * mremap: always moves the block (new pages, copy, free the old ones),
 * which is allowed without MREMAP_FIXED. Shrinking keeps the address.
 */
static int32_t sys_mremap(uint32_t old_addr, uint32_t old_size, uint32_t new_size)
{
    if ((old_addr & (PAGE - 1)) || old_size == 0 || new_size == 0)
        return -K_EINVAL;

    uint32_t old_pages = (old_size + PAGE - 1) & ~(PAGE - 1);
    uint32_t new_pages = (new_size + PAGE - 1) & ~(PAGE - 1);

    if (!user_range_ok(old_addr, old_pages))
        return -K_EFAULT;

    if (new_pages <= old_pages)
    {
        if (new_pages < old_pages)
            user_unmap(old_addr + new_pages, old_addr + old_pages);

        return (int32_t)old_addr;
    }

    int32_t start = sys_mmap2(0, new_pages, 3, MAP_ANONYMOUS, -1, 0);

    if (start < 0)
        return start;

    k_memcpy((void *)(uintptr_t)start, (const void *)(uintptr_t)old_addr, old_size);
    sys_munmap(old_addr, old_pages);

    return start;
}

/* struct statx (linux/stat.h), 256 bytes; only the basic fields are filled. */
static int32_t fill_statx(uint32_t address, const vfs_stat_t *st)
{
    uint8_t b[256];
    uint32_t mask = 0x7FFu; /* STATX_BASIC_STATS */
    uint32_t blksize = 4096;
    uint32_t nlink = 1;
    uint16_t mode = (uint16_t)st->mode;
    uint64_t inode = st->inode;
    uint64_t blocks = (st->size + 511) / 512;
    int64_t time = st->mtime;

    k_memset(b, 0, sizeof(b));
    k_memcpy(b + 0, &mask, 4);
    k_memcpy(b + 4, &blksize, 4);
    k_memcpy(b + 16, &nlink, 4);
    k_memcpy(b + 28, &mode, 2);
    k_memcpy(b + 32, &inode, 8);
    k_memcpy(b + 40, &st->size, 8);
    k_memcpy(b + 48, &blocks, 8);
    k_memcpy(b + 64, &time, 8);  /* atime */
    k_memcpy(b + 96, &time, 8);  /* ctime */
    k_memcpy(b + 112, &time, 8); /* mtime */

    return copy_out(address, b, sizeof(b));
}

/* statx(dirfd, path, flags, mask, buf): AT_EMPTY_PATH (0x1000) with "" means the descriptor itself. */
static int32_t sys_statx(int dirfd, uint32_t path_address, uint32_t flags, uint32_t buf)
{
    char path[VFS_PATH_MAX];
    vfs_stat_t st;
    int32_t status = user_copy_string(path, sizeof(path), path_address);

    if (status < 0)
        return status;

    if (path[0] == '\0' && (flags & 0x1000))
    {
        vfs_file_t *file = user_fd_get(dirfd);

        if (file == NULL)
            return -K_EBADF;

        status = vfs_fstat(file, &st);
    }
    else
    {
        if (path[0] != '/')
            return -K_ENOSYS;

        status = vfs_stat(path, &st);
    }

    return status != 0 ? status : fill_statx(buf, &st);
}

/* --- time --- */

static int32_t clock_ns(uint32_t clock, int64_t *seconds, int64_t *nanoseconds)
{
    if (clock == 0 || clock == 5 || clock == 8) /* REALTIME, REALTIME_COARSE, BOOTTIME-ish */
    {
        /*
         * The RTC is read once; after that the time is that moment plus the
         * monotonic clock's progress, so it never jumps backwards (the RTC
         * only has whole seconds and ticks at its own phase).
         */
        static int have_base;
        static uint64_t base_ns;
        static uint64_t timer_at_base;

        if (!have_base)
        {
            hal_datetime_t now;

            if (hal_clock_now(&now) == 0)
                base_ns = (uint64_t)k_epoch_from_civil(now.year, now.month, now.day, now.hour,
                                                       now.minute, now.second) * 1000000000u;

            timer_at_base = hal_timer_ns();
            have_base = 1;
        }

        uint64_t ns = base_ns + (hal_timer_ns() - timer_at_base);

        *seconds = (int64_t)(ns / 1000000000u);
        *nanoseconds = (int64_t)(ns % 1000000000u);

        return 0;
    }

    if (clock == 1 || clock == 4 || clock == 6 || clock == 7 || clock == 2 || clock == 3)
    {
        uint64_t ns = hal_timer_ns();

        *seconds = (int64_t)(ns / 1000000000u);
        *nanoseconds = (int64_t)(ns % 1000000000u);

        return 0;
    }

    return -K_EINVAL;
}

static int32_t sys_clock_gettime(uint32_t clock, uint32_t address, int wide)
{
    int64_t sec;
    int64_t nsec;
    int32_t status = clock_ns(clock, &sec, &nsec);

    if (status != 0)
        return status;

    if (wide)
    {
        int64_t out[2] = {sec, nsec};

        return copy_out(address, out, 16);
    }

    int32_t out[2] = {(int32_t)sec, (int32_t)nsec};

    return copy_out(address, out, 8);
}

static int32_t sys_gettimeofday(uint32_t address)
{
    int64_t sec;
    int64_t nsec;

    if (address == 0)
        return 0;

    clock_ns(0, &sec, &nsec);

    int32_t out[2] = {(int32_t)sec, (int32_t)(nsec / 1000)};

    return copy_out(address, out, 8);
}

static void sleep_ns(uint64_t ns)
{
    uint64_t end = hal_timer_ns() + ns;

    hal_cpu_enable_interrupts();

    while (hal_timer_ns() < end)
        hal_cpu_idle();
}

static int32_t sys_nanosleep(uint32_t address, int wide)
{
    uint64_t sec;
    uint64_t nsec;

    if (wide)
    {
        int64_t in[2];
        int32_t status = copy_in(in, address, 16);

        if (status != 0)
            return status;

        sec = (uint64_t)in[0];
        nsec = (uint64_t)in[1];
    }
    else
    {
        int32_t in[2];
        int32_t status = copy_in(in, address, 8);

        if (status != 0)
            return status;

        sec = (uint32_t)in[0];
        nsec = (uint32_t)in[1];
    }

    if (nsec >= 1000000000u)
        return -K_EINVAL;

    sleep_ns(sec * 1000000000u + nsec);

    return 0;
}

/* --- poll and select --- */

/* Wait: let the network and the time move on, for a while. */
static void wait_a_little(void)
{
    net_poll();
    hal_cpu_enable_interrupts();
    hal_cpu_idle();
}

/*
 * poll: struct pollfd { int fd; short events; short revents; } (8 bytes).
 * timeout_ms < 0 waits for ever. Returns the number of descriptors with
 * something to report.
 */
static int32_t sys_poll(uint32_t fds_address, uint32_t count, int32_t timeout_ms)
{
    if (count > USER_MAX_FDS * 4)
        return -K_EINVAL;

    if (!user_range_writable(fds_address, count * 8))
        return -K_EFAULT;

    uint64_t end = timeout_ms > 0 ? hal_timer_ns() + (uint64_t)timeout_ms * 1000000u : 0;

    for (;;)
    {
        int32_t ready = 0;

        for (uint32_t i = 0; i < count; i++)
        {
            int32_t *entry = (int32_t *)(uintptr_t)(fds_address + i * 8);
            int16_t *events = (int16_t *)(entry + 1);
            int fd = entry[0];

            events[1] = 0;

            if (fd < 0)
                continue;

            vfs_file_t *file = user_fd_get(fd);

            if (file == NULL)
            {
                events[1] = VFS_POLLNVAL;
                ready++;
                continue;
            }

            events[1] = (int16_t)vfs_poll(file, (uint32_t)events[0]);

            if (events[1] != 0)
                ready++;
        }

        if (ready > 0 || timeout_ms == 0 || (end != 0 && hal_timer_ns() >= end))
            return ready;

        wait_a_little();
    }
}

/* select: three bit sets of descriptors (n bits each); the sets are changed to the ready ones. */
static int32_t sys_select(uint32_t n, uint32_t read_set, uint32_t write_set, uint32_t except_set,
                          int64_t timeout_ms)
{
    uint32_t bytes = (n + 7) / 8;
    uint32_t sets[3] = {read_set, write_set, except_set};
    uint8_t original[3][128];

    if (n > 1024)
        return -K_EINVAL;

    for (int k = 0; k < 3; k++)
    {
        if (sets[k] == 0)
            continue;

        if (!user_range_writable(sets[k], bytes))
            return -K_EFAULT;

        k_memcpy(original[k], (const void *)(uintptr_t)sets[k], bytes);
    }

    uint64_t end = timeout_ms > 0 ? hal_timer_ns() + (uint64_t)timeout_ms * 1000000u : 0;

    for (;;)
    {
        uint8_t result[3][128];
        int32_t ready = 0;

        k_memset(result, 0, sizeof(result));

        for (uint32_t fd = 0; fd < n; fd++)
        {
            uint32_t wanted = 0;

            for (int k = 0; k < 2; k++)
            {
                if (sets[k] != 0 && (original[k][fd / 8] >> (fd % 8)) & 1)
                    wanted |= k == 0 ? VFS_POLLIN : VFS_POLLOUT;
            }

            if (wanted == 0)
                continue;

            vfs_file_t *file = user_fd_get((int)fd);

            if (file == NULL)
                return -K_EBADF;

            uint32_t events = vfs_poll(file, wanted);

            if ((events & VFS_POLLIN) && sets[0] != 0)
            {
                result[0][fd / 8] |= (uint8_t)(1u << (fd % 8));
                ready++;
            }

            if ((events & VFS_POLLOUT) && sets[1] != 0)
            {
                result[1][fd / 8] |= (uint8_t)(1u << (fd % 8));
                ready++;
            }
        }

        if (ready > 0 || timeout_ms == 0 || (end != 0 && hal_timer_ns() >= end))
        {
            for (int k = 0; k < 3; k++)
            {
                if (sets[k] != 0)
                    k_memcpy((void *)(uintptr_t)sets[k], result[k], bytes);
            }

            return ready;
        }

        wait_a_little();
    }
}

/* Timeouts as the different select/poll flavours pass them: -1 = no limit. */
static int64_t timeval_ms(uint32_t address)
{
    int32_t tv[2];

    if (address == 0)
        return -1;

    if (copy_in(tv, address, 8) != 0)
        return -1;

    return (int64_t)tv[0] * 1000 + tv[1] / 1000;
}

static int64_t timespec_ms(uint32_t address, int wide)
{
    if (address == 0)
        return -1;

    if (wide)
    {
        int64_t ts[2];

        return copy_in(ts, address, 16) != 0 ? -1 : ts[0] * 1000 + ts[1] / 1000000;
    }

    int32_t ts[2];

    return copy_in(ts, address, 8) != 0 ? -1 : (int64_t)ts[0] * 1000 + ts[1] / 1000000;
}

/* --- misc --- */

static int32_t sys_uname(uint32_t address)
{
    /* struct utsname: six fields of 65 bytes. */
    char buffer[6 * 65];

    k_memset(buffer, 0, sizeof(buffer));
    k_strcpy(buffer + 0 * 65, "68k_HAL");
    k_strcpy(buffer + 1 * 65, "hal");
    k_strcpy(buffer + 2 * 65, "0.1");
    k_strcpy(buffer + 3 * 65, "68k_HAL");
    k_strcpy(buffer + 4 * 65, "i686");
    k_strcpy(buffer + 5 * 65, "(none)");

    return copy_out(address, buffer, sizeof(buffer));
}

/* struct user_desc: { int entry_number; uint base_addr; uint limit; uint flags; } */
static int32_t sys_set_thread_area(uint32_t address)
{
    uint32_t desc[4];
    int32_t status = copy_in(desc, address, sizeof(desc));

    if (status != 0)
        return status;

    uint32_t selector = hal_user_set_tls(desc[1]);

    user_process.tls_base = desc[1];

    /* The program loads (entry_number << 3) | 3 into its segment register. */
    desc[0] = selector >> 3;

    return copy_out(address, desc, 4);
}

static int32_t sys_getrandom(uint32_t address, uint32_t count)
{
    if (!user_range_writable(address, count))
        return -K_EFAULT;

    vfs_file_t *random;
    int32_t status = vfs_open("/dev/urandom", VFS_O_RDONLY, 0, &random);

    if (status != 0)
        return status;

    status = vfs_read(random, (void *)(uintptr_t)address, count);
    vfs_file_close(random);

    return status;
}

static int32_t sys_getrlimit(uint32_t resource, uint32_t address)
{
    /* struct rlimit { rlim_t cur; rlim_t max; } with 32-bit rlim_t (RLIM_INFINITY = ~0). */
    uint32_t limit[2] = {0xFFFFFFFFu, 0xFFFFFFFFu};

    if (resource == 3) /* RLIMIT_STACK */
        limit[0] = limit[1] = USER_STACK_SIZE;

    return copy_out(address, limit, 8);
}

static int32_t sys_kill_self(uint32_t signal)
{
    if (signal == 0)
        return 0;

    kernel_printf("%s: killed by signal %u\n", user_process.name, signal);
    hal_user_exit(128 + (int)signal);
}

static int32_t sys_openat(int dirfd, uint32_t path, uint32_t flags, uint32_t mode)
{
    if (dirfd != AT_FDCWD)
    {
        /* Absolute paths do not need the directory; relative ones would. */
        char first;

        if (copy_in(&first, path, 1) != 0 || first != '/')
            return -K_ENOSYS;
    }

    return sys_open(path, flags, mode);
}

int32_t user_syscall(const hal_user_syscall_t *call)
{
    const uint32_t *a = call->arg;

    /* System calls may take a while (waiting for a key): let interrupts in. */
    hal_cpu_enable_interrupts();

    switch (call->number)
    {
    case SYS_exit:
    case SYS_exit_group:
        hal_user_exit((int)a[0]);

    case SYS_read:
        return sys_read((int)a[0], a[1], a[2]);

    case SYS_write:
        return sys_write((int)a[0], a[1], a[2]);

    case SYS_readv:
        return sys_vector_io((int)a[0], a[1], a[2], 0);

    case SYS_writev:
        return sys_vector_io((int)a[0], a[1], a[2], 1);

    case SYS_open:
        return sys_open(a[0], a[1], a[2]);

    case SYS_openat:
        return sys_openat((int)a[0], a[1], a[2], a[3]);

    case SYS_close:
        return user_fd_close((int)a[0]);

    case SYS_lseek:
        return sys_lseek((int)a[0], (int32_t)a[1], (int)a[2], 0);

    case SYS__llseek:
        /* (fd, offset_high, offset_low, result*, whence) */
        return sys_lseek((int)a[0], ((int64_t)a[1] << 32) | a[2], (int)a[4], a[3]);

    case SYS_pread64:
    {
        /* (fd, buf, count, pos_low, pos_high) */
        vfs_file_t *file = user_fd_get((int)a[0]);

        if (file == NULL)
            return -K_EBADF;

        uint64_t saved = file->pos;
        file->pos = ((uint64_t)a[4] << 32) | a[3];

        int32_t n = sys_read((int)a[0], a[1], a[2]);

        file->pos = saved;

        return n;
    }

    case SYS_stat64:
    case SYS_lstat64:
        return sys_stat(a[0], a[1]);

    case SYS_fstat64:
        return sys_fstat((int)a[0], a[1]);

    case SYS_fstatat64:
        /* (dirfd, path, stat*, flags) */
        return sys_stat(a[1], a[2]);

    case SYS_getdents64:
        return sys_getdents64((int)a[0], a[1], a[2]);

    case SYS_fcntl64:
        return sys_fcntl((int)a[0], a[1], a[2]);

    case SYS_dup:
    {
        vfs_file_t *file = user_fd_get((int)a[0]);

        if (file == NULL)
            return -K_EBADF;

        int32_t n = user_fd_install(file, 0);

        if (n >= 0)
            vfs_file_ref(file);

        return n;
    }

    case SYS_dup2:
        return sys_dup2((int)a[0], (int)a[1]);

    case SYS_ioctl:
        return sys_ioctl((int)a[0], a[1], a[2]);

    case SYS_mkdir:
        return sys_mkdir(a[0], a[1]);

    case SYS_mkdirat:
        return sys_mkdir(a[1], a[2]);

    case SYS_unlink:
        return sys_unlink(a[0]);

    case SYS_unlinkat:
        /* (dirfd, path, flags): AT_REMOVEDIR = 0x200 */
        return (a[2] & 0x200) ? sys_rmdir(a[1]) : sys_unlink(a[1]);

    case SYS_rmdir:
        return sys_rmdir(a[0]);

    case SYS_rename:
        return sys_rename(a[0], a[1]);

    case SYS_renameat:
        return sys_rename(a[1], a[3]);

    case SYS_access:
        return sys_access(a[0]);

    case SYS_faccessat:
        return sys_access(a[1]);

    case SYS_getcwd:
        return sys_getcwd(a[0], a[1]);

    case SYS_ftruncate:
    {
        vfs_file_t *file = user_fd_get((int)a[0]);

        return file == NULL ? -K_EBADF : vfs_ftruncate(file, a[1]);
    }

    case SYS_fsync:
    case SYS_fdatasync:
        return user_fd_get((int)a[0]) == NULL ? -K_EBADF : 0;

    /* Permissions and owners do not exist on these file systems: accept and ignore. */
    case SYS_chmod:
    case SYS_chown32:
        return sys_access(a[0]);

    case SYS_fchmod:
    case SYS_fchown32:
        return user_fd_get((int)a[0]) == NULL ? -K_EBADF : 0;

    case SYS_pwrite64:
    {
        /* (fd, buf, count, pos_low, pos_high) */
        vfs_file_t *file = user_fd_get((int)a[0]);

        if (file == NULL)
            return -K_EBADF;

        uint64_t saved = file->pos;
        file->pos = ((uint64_t)a[4] << 32) | a[3];

        int32_t n = sys_write((int)a[0], a[1], a[2]);

        file->pos = saved;

        return n;
    }

    case SYS_readlink:
        return -K_EINVAL; /* nothing is a symbolic link */

    case SYS_brk:
        return sys_brk(a[0]);

    case SYS_mmap2:
        return sys_mmap2(a[0], a[1], a[2], a[3], (int)a[4], a[5]);

    case SYS_munmap:
        return sys_munmap(a[0], a[1]);

    case SYS_mremap:
        return sys_mremap(a[0], a[1], a[2]);

    case SYS_statx:
        return sys_statx((int)a[0], a[1], a[2], a[4]);

    case SYS_mprotect:
        return protect_pages(a[0], a[1], a[2]);

    case SYS_madvise:
        return 0; /* advice only */

    case SYS_getpid:
    case SYS_gettid:
        return 1;

    case SYS_getppid:
        return 0;

    case SYS_getpgid:
    case SYS_getsid:
        return 1;

    case SYS_getuid32:
    case SYS_getgid32:
    case SYS_geteuid32:
    case SYS_getegid32:
        return 0;

    case SYS_umask:
        return 022;

    case SYS_set_tid_address:
        return 1;

    case SYS_set_thread_area:
        return sys_set_thread_area(a[0]);

    case SYS_uname:
        return sys_uname(a[0]);

    case SYS_rt_sigaction:
    case SYS_rt_sigprocmask:
        /* No signals are ever delivered: accept and forget the settings. */
        if (call->number == SYS_rt_sigprocmask && a[2] != 0)
        {
            uint8_t zero[8] = {0};

            return copy_out(a[2], zero, a[3] < 8 ? a[3] : 8);
        }

        return 0;

    case SYS_kill:
    case SYS_tkill:
        return sys_kill_self(a[1]);

    case SYS_tgkill:
        return sys_kill_self(a[2]);

    case SYS_gettimeofday:
        return sys_gettimeofday(a[0]);

    case SYS_time:
    {
        int64_t sec;
        int64_t nsec;

        clock_ns(0, &sec, &nsec);

        if (a[0] != 0 && copy_out(a[0], &sec, 4) != 0)
            return -K_EFAULT;

        return (int32_t)sec;
    }

    case SYS_clock_gettime:
        return sys_clock_gettime(a[0], a[1], 0);

    case SYS_clock_gettime64:
        return sys_clock_gettime(a[0], a[1], 1);

    case SYS_clock_getres:
    case SYS_clock_getres_time64:
    {
        if (a[1] == 0)
            return 0;

        if (call->number == SYS_clock_getres_time64)
        {
            int64_t out[2] = {0, 1};

            return copy_out(a[1], out, 16);
        }

        int32_t out[2] = {0, 1};

        return copy_out(a[1], out, 8);
    }

    case SYS_nanosleep:
        return sys_nanosleep(a[0], 0);

    case SYS_clock_nanosleep_time64:
        /* (clock, flags, request*, remain*) : relative sleeps only */
        return (a[1] & 1) ? -K_EINVAL : sys_nanosleep(a[2], 1);

    case SYS_clock_nanosleep:
        return (a[1] & 1) ? -K_EINVAL : sys_nanosleep(a[2], 0);

    case SYS_getrlimit:
    case SYS_ugetrlimit:
        return sys_getrlimit(a[0], a[1]);

    case SYS_socketcall:
        return user_socketcall(a[0], a[1]);

    case SYS_poll:
        return sys_poll(a[0], a[1], (int32_t)a[2]);

    case SYS_ppoll:
    case SYS_ppoll_time64:
    {
        int64_t ms = timespec_ms(a[2], call->number == SYS_ppoll_time64);

        return sys_poll(a[0], a[1], ms < 0 ? -1 : ms > 0x7FFFFFFF ? 0x7FFFFFFF : (int32_t)ms);
    }

    case SYS__newselect:
        return sys_select(a[0], a[1], a[2], a[3], timeval_ms(a[4]));

    case SYS_pselect6:
    case SYS_pselect6_time64:
        return sys_select(a[0], a[1], a[2], a[3], timespec_ms(a[4], call->number == SYS_pselect6_time64));

    case SYS_getrandom:
        return sys_getrandom(a[0], a[1]);

    case SYS_futex:
        return -K_ENOSYS; /* one thread: nothing to wait for */

    default:
        return enosys(call->number);
    }
}
