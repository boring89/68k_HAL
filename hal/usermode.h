/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef HAL_USERMODE_H
#define HAL_USERMODE_H

#include <stdint.h>

/*
 * Running code in the CPU's unprivileged mode (ring 3 on x86). A user
 * program runs until it calls hal_user_exit() (from a system call) or
 * faults; only one program runs at a time. System calls arrive through
 * the software interrupt 0x80 with the Linux i386 register convention:
 * number in EAX, arguments in EBX, ECX, EDX, ESI, EDI, EBP, result in
 * EAX. Implemented once per architecture (arch/<arch>/user/hal_user.c).
 *
 * Memory: user code may only touch pages mapped with HAL_MMU_USER
 * (hal/mmu.h). The kernel is in the same address space but its pages
 * are not user-accessible.
 */

/* A system call as the program issued it. */
typedef struct
{
    uint32_t number;
    uint32_t arg[6];
} hal_user_syscall_t;

/* Why the program was stopped by the CPU. */
typedef struct
{
    uint32_t vector;  /* exception number (x86: 0 = divide, 13 = protection, 14 = page fault) */
    uint32_t error;   /* the CPU's error code, 0 if none */
    uint32_t address; /* faulting address for a page fault, else the instruction pointer */
    uint32_t eip;     /* instruction that faulted */
} hal_user_fault_t;

/*
 * Handlers, called in kernel mode on the kernel stack with interrupts
 * disabled (the syscall handler may enable them to wait). The syscall
 * handler returns the value for the program's EAX (a negative errno on
 * failure). The fault handler must not return: it ends the program
 * with hal_user_exit().
 */
typedef int32_t (*hal_user_syscall_handler_t)(const hal_user_syscall_t *call);
typedef void (*hal_user_fault_handler_t)(const hal_user_fault_t *fault);

/* Set up the mode switch machinery. Call once, before hal_user_run. */
void hal_user_init(hal_user_syscall_handler_t syscall,
                   hal_user_fault_handler_t fault);

/*
 * Start the program at entry with the stack pointer at stack and wait
 * for it to end. Returns the status given to hal_user_exit. The CPU
 * registers are zero at entry. Interrupt state is kept: this must be
 * called with interrupts enabled.
 */
int hal_user_run(uint32_t entry, uint32_t stack);

/* End the running program; hal_user_run returns status. Never returns. */
void hal_user_exit(int status) __attribute__((noreturn));

/*
 * Thread-local storage: make base the base of the program's TLS
 * segment and return the segment selector the program must load into
 * its segment register (%gs on x86) to use it.
 */
uint32_t hal_user_set_tls(uint32_t base);

#endif
