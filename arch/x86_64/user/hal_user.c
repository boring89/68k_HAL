/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal/usermode.h"

#include "user.h"

#include "hal/cpu.h"

#include "arch/x86/cpu/cpu.h"
#include "arch/x86_64/gdt/gdt.h"
#include "arch/x86_64/interrupt/interrupt_frame.h"

#include "kernel/panic/panic.h"

static hal_user_syscall_handler_t syscall_handler;
static hal_user_fault_handler_t fault_handler;
static int initialized;

void hal_user_init(hal_user_syscall_handler_t syscall,
                   hal_user_fault_handler_t fault)
{
    syscall_handler = syscall;
    fault_handler = fault;

    if (!initialized)
    {
        gdt_init_tss();
        initialized = 1;
    }
}

int hal_user_run(uint32_t entry, uint32_t stack)
{
    KASSERT(initialized);

    /*
     * A fresh program starts with the FPU as after power-up: no state from the program before
     * (control word, stack, xmm registers). The kernel itself never uses them, so nothing needs
     * saving.
     */
    const hal_cpu_info_t *cpu = hal_cpu_info();

    if (cpu->features & HAL_CPU_FEATURE_FPU)
        asm volatile("fninit");

    if ((cpu->features & HAL_CPU_FEATURE_SSE) && (cpu_read_cr4() & CPU_CR4_OSFXSR))
    {
        static const uint32_t default_mxcsr = 0x1F80; /* all exceptions masked, round to nearest */

        asm volatile(
            "pxor %%xmm0, %%xmm0\n\t"
            "pxor %%xmm1, %%xmm1\n\t"
            "pxor %%xmm2, %%xmm2\n\t"
            "pxor %%xmm3, %%xmm3\n\t"
            "pxor %%xmm4, %%xmm4\n\t"
            "pxor %%xmm5, %%xmm5\n\t"
            "pxor %%xmm6, %%xmm6\n\t"
            "pxor %%xmm7, %%xmm7\n\t"
            "ldmxcsr %0"
            :
            : "m"(default_mxcsr));
    }

    int status = user_run_asm(entry, stack);

    /* The program ended in a gate handler, which left interrupts off. */
    asm volatile("sti" : : : "memory");

    return status;
}

void hal_user_exit(int status)
{
    user_leave_asm(status);

    for (;;)
        ;
}

uint32_t hal_user_set_tls(uint32_t base)
{
    gdt_set_tls(base);

    return GDT_TLS;
}

/* Called by user_syscall_stub with the registers the program had (an i386 program: the low halves). */
void user_syscall_c(struct interrupt_frame *frame)
{
    hal_user_syscall_t call;

    call.number = (uint32_t)frame->rax;
    call.arg[0] = (uint32_t)frame->rbx;
    call.arg[1] = (uint32_t)frame->rcx;
    call.arg[2] = (uint32_t)frame->rdx;
    call.arg[3] = (uint32_t)frame->rsi;
    call.arg[4] = (uint32_t)frame->rdi;
    call.arg[5] = (uint32_t)frame->rbp;

    frame->rax = (uint32_t)syscall_handler(&call);
}

/* Called by exception_handler when the CPU was in ring 3. */
void user_fault(struct interrupt_frame *frame)
{
    hal_user_fault_t fault;

    fault.vector = (uint32_t)frame->int_no;
    fault.error = (uint32_t)frame->err_code;
    fault.eip = (uint32_t)frame->rip;
    fault.address = frame->int_no == 14 ? (uint32_t)cpu_read_cr2() : (uint32_t)frame->rip;

    if (fault_handler == 0)
        panic("fault in user mode (vector %u) at %x", (unsigned int)frame->int_no, (unsigned int)frame->rip);

    /* 1 = fixed (a page was mapped): return and let the program try again. */
    if (fault_handler(&fault))
        return;

    panic("user fault handler returned");
}
