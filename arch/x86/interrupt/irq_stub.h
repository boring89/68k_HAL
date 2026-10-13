/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ARCH_X86_INTERRUPT_IRQ_STUB_H
#define ARCH_X86_INTERRUPT_IRQ_STUB_H

#include <stdint.h>

/* Entry stubs for IRQ 0 and 1 (irq_stub.asm), installed by idt_init. */
void irq0(void);

void irq1(void);

/*
 * Called by the stubs with interrupts disabled and the IRQ line number.
 * Not implemented here: the interrupt controller code provides it.
 */
void irq_handler(uint32_t irq);

#endif