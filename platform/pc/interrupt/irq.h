/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PLATFORM_PC_INTERRUPT_IRQ_H
#define PLATFORM_PC_INTERRUPT_IRQ_H

/*
 * Switch the interrupts from the 8259 to the local APIC and I/O APIC, if this machine has
 * them (ACPI MADT with an I/O APIC, CPU with a local APIC) and the boot command line does
 * not say "noapic". Call after acpi_init and pic_init, before any driver unmasks a line.
 * Returns 0 if the APIC is in use now, -1 if the 8259 stays.
 */
int irq_apic_enable(void);

#endif
