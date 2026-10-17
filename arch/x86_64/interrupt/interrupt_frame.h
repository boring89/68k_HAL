/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ARCH_X86_64_INTERRUPT_INTERRUPT_FRAME_H
#define ARCH_X86_64_INTERRUPT_INTERRUPT_FRAME_H

#include <stdint.h>

/*
 * Stack layout built by the interrupt stubs (isr.asm, irq_stub.asm, user.asm), lowest address first.
 *
 * In 64-bit mode the CPU always pushes the stack pointer and segment, whatever the privilege
 * level of the interrupted code (Intel SDM Vol.3A 6.14.2, Figure 6-8), and it aligns the stack
 * to 16 bytes first. int_no and err_code are pushed by the stub (err_code is a dummy 0 if the
 * CPU has none), then the stub pushes the general registers.
 *
 * A program in compatibility mode (an i386 program) has its eax in the low half of rax; the
 * upper halves are zero when it enters and ignored when it returns.
 */
struct interrupt_frame
{
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;

    uint64_t int_no;
    uint64_t err_code;

    /* Pushed by the CPU. */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

#endif
