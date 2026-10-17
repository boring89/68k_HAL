/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal/task.h"
#include "hal/usermode.h"

#include "hal/cpu.h"

#include "arch/x86/cpu/cpu.h"
#include "arch/x86/gdt/gdt.h"
#include "arch/x86/interrupt/interrupt_frame.h"

#include "kernel/lib/k_memory.h"

#include <stddef.h>

extern void task_entry_stub(void);
extern void fork_return_stub(void);

/* The registers hal_context_switch saves, above the return address: edi, esi, ebx, ebp, eflags. */
#define SAVED_REGISTERS 5

static uint32_t *push_start_state(uint32_t *sp, void (*resume)(void))
{
    *--sp = (uint32_t)(uintptr_t)resume;

    for (int i = 0; i < SAVED_REGISTERS - 1; i++)
        *--sp = 0;

    *--sp = 0x2; /* eflags: reserved bit 1 set, interrupts off */

    return sp;
}

void hal_context_init(hal_context_t *ctx, void *stack, uint32_t stack_size,
                      void (*entry)(void *), void *arg)
{
    uint32_t *sp = (uint32_t *)((uintptr_t)stack + stack_size);

    *--sp = (uint32_t)(uintptr_t)arg;
    *--sp = (uint32_t)(uintptr_t)entry;

    ctx->sp = (uintptr_t)push_start_state(sp, task_entry_stub);
}

void hal_task_set_kernel_stack(void *top)
{
    gdt_set_kernel_stack((uint32_t)(uintptr_t)top);
}

void hal_task_set_tls(uint32_t base)
{
    uint32_t selector = 0x23; /* no TLS: the plain user data segment */

    if (base != 0)
    {
        gdt_set_tls(base);
        selector = GDT_TLS;
    }

    /* Reloading %gs makes the CPU read the (changed) descriptor again. */
    asm volatile("mov %0, %%gs" : : "r"((uint16_t)selector));
}

/* --- floating point --- */

static bool use_fxsave(void)
{
    const hal_cpu_info_t *cpu = hal_cpu_info();

    return (cpu->features & HAL_CPU_FEATURE_SSE) && (cpu_read_cr4() & CPU_CR4_OSFXSR);
}

void hal_fpu_save(void *area)
{
    if (use_fxsave())
        asm volatile("fxsave (%0)" : : "r"(area) : "memory");
    else if (hal_cpu_info()->features & HAL_CPU_FEATURE_FPU)
        asm volatile("fnsave (%0); fwait" : : "r"(area) : "memory");
}

void hal_fpu_restore(const void *area)
{
    if (use_fxsave())
        asm volatile("fxrstor (%0)" : : "r"(area) : "memory");
    else if (hal_cpu_info()->features & HAL_CPU_FEATURE_FPU)
        asm volatile("frstor (%0)" : : "r"(area) : "memory");
}

void hal_fpu_reset_area(void *area)
{
    /* The state of a CPU after power-up, as hal_user_run sets it. */
    if (hal_cpu_info()->features & HAL_CPU_FEATURE_FPU)
        asm volatile("fninit");

    if (use_fxsave())
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

    hal_fpu_save(area);
}

/* --- user registers and fork --- */

static struct interrupt_frame *syscall_frame(void)
{
    return (struct interrupt_frame *)(uintptr_t)(gdt_get_kernel_stack() - sizeof(struct interrupt_frame));
}

void hal_user_regs_get(hal_user_regs_t *regs)
{
    const struct interrupt_frame *frame = syscall_frame();

    regs->eax = frame->eax;
    regs->ecx = frame->ecx;
    regs->edx = frame->edx;
    regs->ebx = frame->ebx;
    regs->esp = frame->user_esp;
    regs->ebp = frame->ebp;
    regs->esi = frame->esi;
    regs->edi = frame->edi;
    regs->eip = frame->eip;
    regs->eflags = frame->eflags;
}

void hal_user_regs_set(const hal_user_regs_t *regs)
{
    struct interrupt_frame *frame = syscall_frame();

    frame->eax = regs->eax;
    frame->ecx = regs->ecx;
    frame->edx = regs->edx;
    frame->ebx = regs->ebx;
    frame->user_esp = regs->esp;
    frame->ebp = regs->ebp;
    frame->esi = regs->esi;
    frame->edi = regs->edi;
    frame->eip = regs->eip;
    /* The program may not turn off interrupts or change the privilege bits: keep IF, take the arithmetic flags. */
    frame->eflags = 0x202 | (regs->eflags & 0xCD5);
}

int hal_user_fork(hal_context_t *child, void *stack, uint32_t stack_size, uint32_t user_sp)
{
    if (stack_size < 2 * sizeof(struct interrupt_frame))
        return -1;

    uintptr_t top = (uintptr_t)stack + stack_size;
    struct interrupt_frame *copy = (struct interrupt_frame *)(top - sizeof(*copy));

    k_memcpy(copy, syscall_frame(), sizeof(*copy));
    copy->eax = 0;

    if (user_sp != 0)
        copy->user_esp = user_sp;

    child->sp = (uintptr_t)push_start_state((uint32_t *)copy, fork_return_stub);

    return 0;
}
