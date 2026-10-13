/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ARCH_X86_INTERRUPT_IRQ_STUB_H
#define ARCH_X86_INTERRUPT_IRQ_STUB_H

#include <stdint.h>

#define IRQ_STUB_COUNT 16

/*
 * First IDT vector of the IRQ stubs: line n uses vector
 * IRQ_STUB_VECTOR_BASE + n. The interrupt controller must deliver
 * IRQs on these vectors (vectors 0-31 are CPU exceptions).
 */
#define IRQ_STUB_VECTOR_BASE 0x20

/* Addresses of the entry stubs for lines 0-15 (irq_stub.asm). */
extern const uint32_t irq_stub_table[IRQ_STUB_COUNT];

/*
 * Called by the stubs with interrupts disabled and the IRQ line number.
 * Not implemented here: the interrupt controller code provides it.
 */
void irq_handler(uint32_t irq);

#endif