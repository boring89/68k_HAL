/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PLATFORM_PC_INTERRUPT_LAPIC_H
#define PLATFORM_PC_INTERRUPT_LAPIC_H

#include <stdint.h>

/*
 * The local APIC of the boot processor (Intel SDM Vol.3A chapter 10): the interrupt controller
 * that sits in each CPU. It receives the interrupts of the I/O APIC and the messages
 * of PCI devices (MSI), and it is how one CPU starts another (platform/pc/smp).
 */

/* Local APIC registers (SDM Vol.3A 10.4.1, Table 10-1): offsets from the base address. */
#define LAPIC_ID 0x20
#define LAPIC_TPR 0x80
#define LAPIC_EOI 0xB0
#define LAPIC_SVR 0xF0
#define LAPIC_ICR_LOW 0x300
#define LAPIC_ICR_HIGH 0x310

#define LAPIC_SVR_ENABLE 0x100u

/* The vector a spurious APIC interrupt arrives on (it needs no end of interrupt). */
#define LAPIC_SPURIOUS_VECTOR 0xFF

/*
 * Map the registers at physical address base, enable the local APIC and accept every interrupt
 * priority. Returns 0, or -1 if the registers cannot be mapped.
 */
int lapic_init(uint32_t base);

/* True after a successful lapic_init. */
int lapic_ready(void);

uint32_t lapic_read(uint32_t reg);
void lapic_write(uint32_t reg, uint32_t value);

/* The APIC ID of the processor that calls this (the boot processor, in practice). */
uint32_t lapic_id(void);

/* Tell the local APIC the interrupt being handled is done. */
void lapic_eoi(void);

#endif
