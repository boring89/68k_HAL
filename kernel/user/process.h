/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_USER_PROCESS_H
#define KERNEL_USER_PROCESS_H

#include "region.h"

#include "hal/usermode.h"

#include "kernel/vfs/vfs.h"

#include <stddef.h>
#include <stdint.h>

/*
 * The running user program. There is only one at a time (no
 * multitasking yet), so its state is a single global. Programs are
 * statically linked Linux i386 ELF files (see docs/userland.md).
 *
 * Address space of a program (all page-granular, user-accessible):
 *
 *   0x08048000 ...  program image (what the ELF file says), then the brk heap
 *   0x50000000 ...  anonymous mmap areas, handed out upward
 *   0x7FE00000 ...  stack, ends at 0x80000000
 *
 * Everything else, the kernel included, is not reachable from user mode.
 */

#define USER_IMAGE_MIN 0x08000000u
#define USER_BRK_LIMIT 0x0F000000u
#define USER_MMAP_BASE 0x50000000u
#define USER_MMAP_LIMIT 0x70000000u
#define USER_STACK_TOP 0x80000000u
#define USER_STACK_SIZE (2u * 1024 * 1024)

#define USER_MAX_FDS 64

typedef struct
{
    int running;
    user_regions_t regions;
    uint32_t brk_start;
    uint32_t brk;
    uint32_t mmap_next;
    vfs_file_t *fd[USER_MAX_FDS];
    uint32_t tls_base;
    char name[64];
} user_process_t;

extern user_process_t user_process;

/* --- memory --- */

/* Map zeroed, user-accessible pages over [start, end) (page aligned) and record the region. Returns 0 or -errno. */
int32_t user_map(uint32_t start, uint32_t end);

/* Undo user_map for [start, end): unmap and free the frames, forget the regions. */
void user_unmap(uint32_t start, uint32_t end);

/* True if the whole range may be touched by the kernel on behalf of the program. */
int user_range_ok(uint32_t address, uint32_t length);

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

/*
 * Load the program at path (a VFS path), run it with the given
 * arguments and wait for it to end. argv and envp are NULL-terminated
 * string arrays. Returns the exit status (0..255; 128+n if it was
 * killed by a fault or signal n), or -errno if it could not be started.
 */
int32_t user_run(const char *path, const char *const *argv, const char *const *envp);

/* socket.c: the i386 socketcall(call, args) multiplexer (BSD sockets, AF_INET). */
int32_t user_socketcall(uint32_t call, uint32_t args_address);

/* fcntl(F_SETFL, O_NONBLOCK) on a socket (does nothing for other files). */
void user_socket_set_nonblock(vfs_file_t *file, int enabled);

/* syscall.c */
int32_t user_syscall(const hal_user_syscall_t *call);

#endif
