/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "exception.h"

#include "../printf/printf.h"
#include "../panic/panic.h"

#include <stdint.h>

#define EXCEPTION_COUNT 32
#define EXCEPTION_PAGE_FAULT 14

/* Vector names from Intel SDM Vol.3 Ch.6, Table 6-1. */
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
    "Reserved",
    "Reserved",
};

#ifdef KERNEL_SELFTEST
static volatile exception_hook_t test_hook;

void exception_set_test_hook(exception_hook_t hook)
{
    test_hook = hook;
}
#endif

void exception_handler(struct interrupt_frame *frame)
{
#ifdef KERNEL_SELFTEST
    if (test_hook && test_hook(frame) == 0)
        return;
#endif

    const char *name = frame->int_no < EXCEPTION_COUNT
        ? exception_names[frame->int_no]
        : "Unknown";

    kernel_printf("\nCPU exception %u: %s\n", frame->int_no, name);
    kernel_printf("err_code=%x eip=%x cs=%x eflags=%x\n",
                  frame->err_code, frame->eip, frame->cs, frame->eflags);
    kernel_printf("eax=%x ebx=%x ecx=%x edx=%x\n",
                  frame->eax, frame->ebx, frame->ecx, frame->edx);
    kernel_printf("esi=%x edi=%x ebp=%x esp=%x\n",
                  frame->esi, frame->edi, frame->ebp, frame->esp);

    if (frame->int_no == EXCEPTION_PAGE_FAULT)
    {
        /* CR2 holds the faulting linear address (Intel SDM Vol.3 6.15). */
        uint32_t cr2;
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
        kernel_printf("cr2=%x\n", cr2);
    }

    panic("unhandled CPU exception %u", frame->int_no);
}
