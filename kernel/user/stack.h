/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_USER_STACK_H
#define KERNEL_USER_STACK_H

#include <stddef.h>
#include <stdint.h>

/*
 * The initial stack of a Linux i386 program (System V i386 ABI, 3.4.1
 * "Initial Stack and Register State", plus Linux's auxiliary vector,
 * getauxval(3)). From the stack pointer upward:
 *
 *   argc
 *   argv[0] ... argv[argc-1], NULL
 *   envp[0] ... envp[n-1], NULL
 *   auxv: (type, value) pairs, ending with (AT_NULL, 0)
 *   ... padding ...
 *   the strings argv and envp point to, 16 random bytes, "i686"
 *
 * The builder works on a buffer that stands for the top of the user
 * stack, so it can be tested without any hardware.
 */

#define AT_NULL 0
#define AT_PHDR 3
#define AT_PHENT 4
#define AT_PHNUM 5
#define AT_PAGESZ 6
#define AT_ENTRY 9
#define AT_UID 11
#define AT_EUID 12
#define AT_GID 13
#define AT_EGID 14
#define AT_PLATFORM 15
#define AT_HWCAP 16
#define AT_SECURE 23
#define AT_RANDOM 25

typedef struct
{
    uint32_t phdr;
    uint32_t phent;
    uint32_t phnum;
    uint32_t entry;
    uint8_t random[16]; /* AT_RANDOM: seeds musl's stack protector and hash salts */
} user_stack_params_t;

/*
 * Fill buffer (capacity bytes, standing for user addresses
 * [top - capacity, top)) with the initial stack and store the initial
 * stack pointer, a user address that is a multiple of 16, in *sp.
 * argv and envp are NULL-terminated string arrays. Returns the number
 * of bytes used at the top of the buffer, or 0 if everything does not
 * fit.
 */
size_t user_stack_build(uint8_t *buffer, size_t capacity, uint32_t top,
                        const char *const *argv, const char *const *envp,
                        const user_stack_params_t *params, uint32_t *sp);

#endif
