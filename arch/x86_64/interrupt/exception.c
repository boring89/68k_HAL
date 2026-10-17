/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "arch/x86/interrupt/exception.h"

#include "arch/x86/interrupt/page_fault.h"
#include "arch/x86_64/user/user.h"
#include "kernel/printf/printf.h"
#include "kernel/panic/panic.h"
#include "arch/x86/cpu/cpu.h"

#include <stdint.h>

#define EXCEPTION_COUNT 32

static const char *const exception_names[EXCEPTION_COUNT] =
{
    "Divide Error (#DE)",
    "Debug (#DB)",
    "NMI Interrupt",
    "Breakpoint (#BP)",
    "Overflow (#OF)",
    "BOUND Range Exceeded (#BR)",
    "Invalid Opcode (#UD)",
    "Device Not Available (#NM)",
    "Double Fault (#DF)",
    "Coprocessor Segment Overrun",
    "Invalid TSS (#TS)",
    "Segment Not Present (#NP)",
    "Stack-Segment Fault (#SS)",
    "General Protection (#GP)",
    "Page Fault (#PF)",
    "Reserved",
    "x87 FPU Floating-Point Error (#MF)",
    "Alignment Check (#AC)",
    "Machine Check (#MC)",
    "SIMD Floating-Point Exception (#XM)",
    "Virtualization Exception (#VE)",
    "Control Protection Exception (#CP)",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "VMM Communication (#VC)",
    "Reserved",
};

#ifdef KERNEL_SELFTEST
static volatile exception_hook_t test_hook;

void exception_set_test_hook(exception_hook_t hook)
{
    test_hook = hook;
}
#endif

static void print_hex64(const char *label, uint64_t value)
{
    kernel_printf("%s=%x%x ", label, (unsigned int)(value >> 32), (unsigned int)value);
}

void exception_handler(struct interrupt_frame *frame)
{
#ifdef KERNEL_SELFTEST
    if (test_hook && test_hook(frame) == 0)
        return;
#endif

    /* The program in ring 3 did it: stop the program, not the kernel. */
    if ((frame->cs & 3) == 3)
    {
        user_fault(frame);

        return;
    }

    const char *name = frame->int_no < EXCEPTION_COUNT
        ? exception_names[frame->int_no]
        : "Unknown";

    kernel_printf("\nCPU exception %u: %s\n", (unsigned int)frame->int_no, name);
    kernel_printf("err_code=%x cs=%x ", (unsigned int)frame->err_code, (unsigned int)frame->cs);
    print_hex64("rip", frame->rip);
    print_hex64("rflags", frame->rflags);
    kernel_printf("\n");
    print_hex64("rax", frame->rax);
    print_hex64("rbx", frame->rbx);
    print_hex64("rcx", frame->rcx);
    print_hex64("rdx", frame->rdx);
    kernel_printf("\n");
    print_hex64("rsi", frame->rsi);
    print_hex64("rdi", frame->rdi);
    print_hex64("rbp", frame->rbp);
    print_hex64("rsp", frame->rsp);
    kernel_printf("\n");

    if (frame->int_no == EXCEPTION_PAGE_FAULT)
    {
        /* CR2 holds the faulting linear address (Intel SDM Vol.3A 6.15). */
        char reason[PAGE_FAULT_DESCRIBE_MAX];
        uint64_t address = cpu_read_cr2();

        page_fault_describe((uint32_t)frame->err_code, reason, sizeof(reason));
        print_hex64("cr2", address);
        kernel_printf("(%s)\n", reason);
    }

    panic("unhandled CPU exception %u", (unsigned int)frame->int_no);
}
