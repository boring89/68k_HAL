/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal/usermode.h"

#include "user.h"

#include "arch/x86/cpu/cpu.h"
#include "arch/x86/gdt/gdt.h"
#include "arch/x86/interrupt/interrupt_frame.h"

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

/* Called by user_syscall_stub with the registers the program had. */
void user_syscall_c(struct interrupt_frame *frame)
{
    hal_user_syscall_t call;

    call.number = frame->eax;
    call.arg[0] = frame->ebx;
    call.arg[1] = frame->ecx;
    call.arg[2] = frame->edx;
    call.arg[3] = frame->esi;
    call.arg[4] = frame->edi;
    call.arg[5] = frame->ebp;

    frame->eax = (uint32_t)syscall_handler(&call);
}

/* Called by exception_handler when the CPU was in ring 3. */
void user_fault(struct interrupt_frame *frame)
{
    hal_user_fault_t fault;

    fault.vector = frame->int_no;
    fault.error = frame->err_code;
    fault.eip = frame->eip;
    fault.address = frame->int_no == 14 ? cpu_read_cr2() : frame->eip;

    if (fault_handler == 0)
        panic("fault in user mode (vector %u) at %x", frame->int_no, frame->eip);

    fault_handler(&fault);
    panic("user fault handler returned");
}
