/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ARCH_X86_INTERRUPT_INTERRUPT_FRAME_H
#define ARCH_X86_INTERRUPT_INTERRUPT_FRAME_H

#include <stdint.h>

/*
 * Stack layout built by isr_common_stub (isr.asm), lowest address first.
 *
 * The kernel only runs in ring 0, so the CPU does not push useresp/ss
 * (Intel SDM Vol.3 6.12.1, Figure 6-4: no privilege-level change).
 */
struct interrupt_frame
{
    /* pusha */
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp; /* esp before pusha; ignored by popa */
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;

    /* Pushed by the stub (err_code is a dummy 0 if the CPU has none). */
    uint32_t int_no;
    uint32_t err_code;

    /* Pushed by the CPU. */
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
};

#endif
