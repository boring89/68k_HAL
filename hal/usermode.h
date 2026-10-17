/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef HAL_USERMODE_H
#define HAL_USERMODE_H

#include "hal/task.h"

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
 * failure). The fault handler returns 1 if it fixed the cause (a page that was filled in on demand): the program
 * then runs the faulting instruction again. Otherwise it must not return: it ends the program.
 */
typedef int32_t (*hal_user_syscall_handler_t)(const hal_user_syscall_t *call);
typedef int (*hal_user_fault_handler_t)(const hal_user_fault_t *fault);

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

/*
 * --- Several programs (processes) ---
 *
 * With the scheduler in kernel/user each program has its own kernel stack (hal/task.h) and
 * address space (hal/mmu.h). These functions are for a program's own context.
 */

/* The registers of a program as a system call interrupted it (Linux i386 sigcontext order). */
typedef struct
{
    uint32_t eax, ecx, edx, ebx;
    uint32_t esp, ebp, esi, edi;
    uint32_t eip, eflags;
} hal_user_regs_t;

/*
 * The user registers of the system call being handled, and replace them. The new ones take effect
 * when the handler returns to user mode, except that the handler's return value always replaces eax.
 * Only valid inside a handler on a stack set with hal_task_set_kernel_stack.
 */
void hal_user_regs_get(hal_user_regs_t *regs);
void hal_user_regs_set(const hal_user_regs_t *regs);

/* Leave the kernel for good and start running user code at entry with the stack pointer sp (all other registers zero). */
void hal_user_enter(uint32_t entry, uint32_t sp) __attribute__((noreturn));

/*
 * fork: from inside a system call, prepare child (a context on the given kernel stack) so
 * that the first switch to it returns from the same system call in user mode with eax = 0,
 * all other registers as they are now, except the stack pointer if user_sp is not 0. Returns 0 or -1.
 */
int hal_user_fork(hal_context_t *child, void *stack, uint32_t stack_size, uint32_t user_sp);

#endif
