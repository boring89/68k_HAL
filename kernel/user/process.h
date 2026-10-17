/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_USER_PROCESS_H
#define KERNEL_USER_PROCESS_H

#include "region.h"

#include "hal/mmu.h"
#include "hal/task.h"
#include "hal/usermode.h"

#include "kernel/vfs/vfs.h"

#include <stddef.h>
#include <stdint.h>

/*
 * User programs (processes). Several can exist at the same time; they take turns when one
 * of them waits (see sched.h and docs/process.md). Programs are statically linked Linux i386
 * ELF files (see docs/userland.md).
 *
 * Address space of a program (all page-granular, user-accessible):
 *
 *   0x08048000 ...  program image (what the ELF file says), then the brk heap
 *   0x50000000 ...  anonymous mmap areas, handed out upward
 *   0x7FE00000 ...  stack, ends at 0x80000000
 *
 * Everything else, the kernel included, is not reachable from user mode. Each process has its
 * own address space (hal_space_*): the same addresses mean different memory in each.
 */

#define USER_IMAGE_MIN 0x08000000u
#define USER_BRK_LIMIT 0x0F000000u
#define USER_MMAP_BASE 0x50000000u
#define USER_MMAP_LIMIT 0x70000000u
#define USER_STACK_TOP 0x80000000u
#define USER_STACK_SIZE (2u * 1024 * 1024)

#define USER_MAX_FDS 64

/* Most processes alive at once (zombies included); slot 0 is the kernel itself and not counted. */
#define USER_MAX_PROCS 64

/* Size of the kernel stack each process has for its system calls. */
#define USER_KERNEL_STACK_SIZE (16u * 1024)

#define USER_SIGNAL_COUNT 32

typedef enum
{
    PROC_FREE = 0,
    PROC_RUNNABLE,
    PROC_WAITING, /* in sched_wait: runs again when something happened */
    PROC_ZOMBIE   /* ended, waiting for its parent to collect the status */
} proc_state_t;

/* What a process does on a signal (Linux struct sigaction fields, kept as given). */
typedef struct
{
    uint32_t handler; /* 0 = SIG_DFL, 1 = SIG_IGN, else the address of the handler */
    uint32_t flags;
    uint32_t restorer;
    uint32_t mask;    /* signals blocked while the handler runs (the first 32) */
} user_sigaction_t;

typedef struct user_process
{
    proc_state_t state;
    int pid;
    int ppid;
    int pgid;
    int is_kernel;      /* slot 0: the boot context, not a user program */

    /* How it ended (valid in PROC_ZOMBIE): exit status, or the signal that killed it. */
    int exit_code;
    int term_signal;

    uint32_t seen;      /* sched_stamp() when it went to PROC_WAITING */

    hal_context_t context;
    void *kernel_stack;
    hal_space_t space;
    uint8_t fpu_raw[HAL_FPU_AREA_SIZE + 16];

    user_regions_t regions;
    uint32_t brk_start;
    uint32_t brk;
    uint32_t mmap_next;
    struct
    {
        uint32_t start;
        uint32_t end;
    } device[4]; /* ranges mapped onto device memory (user_map_device) */
    unsigned int device_count;
    vfs_file_t *fd[USER_MAX_FDS];
    uint32_t fd_cloexec[(USER_MAX_FDS + 31) / 32];
    uint32_t tls_base;
    char name[64];
    char cwd[VFS_PATH_MAX];

    /* Where a new process starts (sched_start). */
    uint32_t start_entry;
    uint32_t start_sp;

    /* signal.c */
    uint32_t sig_pending;
    uint32_t sig_blocked;
    user_sigaction_t sig_action[USER_SIGNAL_COUNT];
} user_process_t;

/* The process that is running (the kernel's own entry while no program runs). */
extern user_process_t *user_current;

/* Existing code says user_process.x; it means the running process. */
#define user_process (*user_current)

/* --- memory --- */

/* Map zeroed, user-accessible pages over [start, end) (page aligned) and record the region. Returns 0 or -errno. */
int32_t user_map(uint32_t start, uint32_t end);

/*
 * Map size bytes (page aligned) of device memory at physical address phys at start, writable,
 * and record the region. The frames are not RAM: unmapping never gives them to the allocator.
 */
int32_t user_map_device(uint32_t start, uint32_t size, uintptr_t phys);

/* Undo user_map for [start, end): unmap and free the frames, forget the regions. */
void user_unmap(uint32_t start, uint32_t end);

/* True if the program may read the whole range (it is the program's and not a guard page). */
int user_range_ok(uint32_t address, uint32_t length);

/* True if the program may also write it (not read-only). The kernel must check this before it writes into user memory. */
int user_range_writable(uint32_t address, uint32_t length);

/*
 * Copy a NUL-terminated string from the program into buffer (size
 * bytes, including the NUL). Returns its length, -EFAULT for a bad
 * pointer or -ENAMETOOLONG if it does not fit.
 */
int32_t user_copy_string(char *buffer, size_t size, uint32_t address);

/* --- file descriptors --- */

/* Lowest free descriptor at or above min holding file (reference taken over). Returns the number or -errno. */
int32_t user_fd_install(vfs_file_t *file, int min);

vfs_file_t *user_fd_get(int fd);

int32_t user_fd_close(int fd);

/* --- running --- */

/* Mount /dev and prepare the CPU for user mode. Call once at boot, after the heap. */
void user_init(void);

/* Close all descriptors and free all pages of the running process (it is about to end). */
void user_release_memory(void);

/* Free all pages of the running process, keep the descriptors (exec). */
void user_unmap_all(void);

/* Free what a process that is not running holds (a failed fork or spawn). */
void user_discard(user_process_t *p);

/* fork: copy the running process' pages, regions, break and mmap state into child's address space. Returns 0 or -errno. */
int32_t user_copy_memory(user_process_t *child);

/*
 * Start the program at path as a new process, child of the running one, and return at once
 * with its pid (or -errno). files are its descriptors 0, 1 and 2 (NULL entries or a NULL array:
 * the console); join_group > 0 puts it in that process group, else it starts its own.
 */
int32_t user_spawn(const char *path, const char *const *argv, const char *const *envp,
                   vfs_file_t *const files[3], int join_group);

/*
 * execve in the running process: replace its program by the one at path. The descriptors stay
 * (except close-on-exec ones), the handlers go back to default. Returns -errno if the new
 * program cannot be started and the old one is still intact; 0 when it worked (the
 * system call then returns into the new program).
 */
int32_t user_exec(const char *path, const char *const *argv, const char *const *envp);

/* One program of a job (a shell line). stdin_path / stdout_path override the pipe or console. */
typedef struct
{
    const char *path;
    const char *const *argv;
    const char *stdin_path;
    const char *stdout_path;
    int append;
} user_job_stage_t;

#define USER_JOB_MAX_STAGES 4

/*
 * Run a pipeline: all stages at the same time, the standard output of each one the standard
 * input of the next, and wait until all have ended. status[i] is the exit status of stage i
 * (128 + n if killed by signal n), or -errno if that stage could not start (the later ones then
 * did not run). Returns 0 or -errno for bad arguments. Whatever is left running afterwards is killed.
 */
int32_t user_run_job(const user_job_stage_t *stages, unsigned int count, const char *const *envp,
                     int32_t *status);

/*
 * Load the program at path (a VFS path), run it with the given
 * arguments and wait for it to end. argv and envp are NULL-terminated
 * string arrays. Returns the exit status (0..255; 128+n if it was
 * killed by a fault or signal n), or -errno if it could not be started.
 */
int32_t user_run(const char *path, const char *const *argv, const char *const *envp);

/*
 * Like user_run, with standard input read from the file stdin_path and standard output written to
 * stdout_path (created, then truncated or, with append, added to); NULL = the console. A file that
 * cannot be opened is the error returned (-errno) and the program does not run.
 */
int32_t user_run_redirect(const char *path, const char *const *argv, const char *const *envp,
                          const char *stdin_path, const char *stdout_path, int append);

/* pipe.c: an anonymous pipe: *read and *write are two open files. Returns 0 or -errno. */
int32_t pipe_create(vfs_file_t **read, vfs_file_t **write);

/* console.c: the process group that Ctrl-C and Ctrl-\ signal (0 = none). */
void user_console_set_foreground(int pgid);

/* console.c: read the keys that arrived (Ctrl-C reaches the foreground group even if nobody reads the console). */
void user_console_pump(void);

/* socket.c: the i386 socketcall(call, args) multiplexer (BSD sockets, AF_INET). */
int32_t user_socketcall(uint32_t call, uint32_t args_address);

/* fcntl(F_SETFL, O_NONBLOCK) on a socket (does nothing for other files). */
void user_socket_set_nonblock(vfs_file_t *file, int enabled);

/* syscall.c */
int32_t user_syscall(const hal_user_syscall_t *call);

#endif
