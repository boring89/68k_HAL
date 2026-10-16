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
 * When the interrupted code was in ring 0 the CPU does not push the
 * stack pointer and segment (Intel SDM Vol.3A 6.12.1, Figure 6-4: no
 * privilege-level change), so user_esp/user_ss exist only for ring 3.
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

    /*
     * Pushed by the CPU only when it came from ring 3 (cs & 3 != 0):
     * the user stack to return to (Intel SDM Vol.3A 6.12.1, Figure 6-4).
     */
    uint32_t user_esp;
    uint32_t user_ss;
};

#endif
