/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal/task.h"
#include "hal/usermode.h"

#include "hal/cpu.h"

#include "arch/x86/cpu/cpu.h"
#include "arch/x86_64/gdt/gdt.h"
#include "arch/x86_64/interrupt/interrupt_frame.h"

#include "kernel/lib/k_memory.h"

#include <stdbool.h>
#include <stddef.h>

extern void task_entry_stub(void);
extern void fork_return_stub(void);

/* The registers hal_context_switch saves, below the return address: rflags, r15, r14, r13, r12, rbx, rbp. */
#define SAVED_REGISTERS 7

static uint64_t *push_start_state(uint64_t *sp, void (*resume)(void))
{
    *--sp = (uint64_t)(uintptr_t)resume;

    for (int i = 0; i < SAVED_REGISTERS - 1; i++)
        *--sp = 0;

    *--sp = 0x2; /* rflags: reserved bit 1 set, interrupts off */

    return sp;
}

void hal_context_init(hal_context_t *ctx, void *stack, uint32_t stack_size,
                      void (*entry)(void *), void *arg)
{
    /* The top is 16-byte aligned: task_entry_stub pops entry and arg and then calls with rsp = top. */
    uint64_t *sp = (uint64_t *)(((uintptr_t)stack + stack_size) & ~(uintptr_t)15);

    *--sp = (uint64_t)(uintptr_t)arg;
    *--sp = (uint64_t)(uintptr_t)entry;

    ctx->sp = (uintptr_t)push_start_state(sp, task_entry_stub);
}

void hal_task_set_kernel_stack(void *top)
{
    /* The CPU aligns the stack to 16 bytes before it pushes an interrupt frame. */
    gdt_set_kernel_stack((uintptr_t)top & ~(uintptr_t)15);
}

void hal_task_set_tls(uint32_t base)
{
    uint32_t selector = 0x23; /* no TLS: the plain user data segment */

    if (base != 0)
    {
        gdt_set_tls(base);
        selector = GDT_TLS;
    }

    /* Reloading %gs makes the CPU read the (changed) descriptor again; it loads the segment base too. */
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
    return (struct interrupt_frame *)(gdt_get_kernel_stack() - sizeof(struct interrupt_frame));
}

void hal_user_regs_get(hal_user_regs_t *regs)
{
    const struct interrupt_frame *frame = syscall_frame();

    regs->eax = (uint32_t)frame->rax;
    regs->ecx = (uint32_t)frame->rcx;
    regs->edx = (uint32_t)frame->rdx;
    regs->ebx = (uint32_t)frame->rbx;
    regs->esp = (uint32_t)frame->rsp;
    regs->ebp = (uint32_t)frame->rbp;
    regs->esi = (uint32_t)frame->rsi;
    regs->edi = (uint32_t)frame->rdi;
    regs->eip = (uint32_t)frame->rip;
    regs->eflags = (uint32_t)frame->rflags;
}

void hal_user_regs_set(const hal_user_regs_t *regs)
{
    struct interrupt_frame *frame = syscall_frame();

    frame->rax = regs->eax;
    frame->rcx = regs->ecx;
    frame->rdx = regs->edx;
    frame->rbx = regs->ebx;
    frame->rsp = regs->esp;
    frame->rbp = regs->ebp;
    frame->rsi = regs->esi;
    frame->rdi = regs->edi;
    frame->rip = regs->eip;
    /* The program may not turn off interrupts or change the privilege bits: keep IF, take the arithmetic flags. */
    frame->rflags = 0x202 | (regs->eflags & 0xCD5);
}

int hal_user_fork(hal_context_t *child, void *stack, uint32_t stack_size, uint32_t user_sp)
{
    if (stack_size < 2 * sizeof(struct interrupt_frame))
        return -1;

    uintptr_t top = ((uintptr_t)stack + stack_size) & ~(uintptr_t)15;
    struct interrupt_frame *copy = (struct interrupt_frame *)(top - sizeof(*copy));

    k_memcpy(copy, syscall_frame(), sizeof(*copy));
    copy->rax = 0;

    if (user_sp != 0)
        copy->rsp = user_sp;

    child->sp = (uintptr_t)push_start_state((uint64_t *)copy, fork_return_stub);

    return 0;
}
