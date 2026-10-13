/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal/cpu.h"

/*
 * STI / CLI set and clear EFLAGS.IF (bit 9), which masks maskable
 * external interrupts (Intel SDM Vol.2B "STI", Vol.2A "CLI"; Vol.3A 6.8.1).
 * The "memory" clobber makes each one a compiler barrier.
 */

void hal_cpu_enable_interrupts(void)
{
    asm volatile("sti" : : : "memory");
}

void hal_cpu_disable_interrupts(void)
{
    asm volatile("cli" : : : "memory");
}

/*
 * HLT stops the CPU until an interrupt, NMI or reset arrives; the
 * handler runs, then execution continues after HLT (Intel SDM Vol.2A
 * "HLT"). The "memory" clobber makes callers re-read variables written
 * by that handler.
 */

void hal_cpu_idle(void)
{
    asm volatile("hlt" : : : "memory");
}

void hal_cpu_halt(void)
{
    hal_cpu_disable_interrupts();

    /* An NMI still wakes HLT even with IF clear, so halt again. */
    for (;;)
        asm volatile("hlt");
}
