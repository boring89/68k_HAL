/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PLATFORM_PC_INTERRUPT_IOAPIC_H
#define PLATFORM_PC_INTERRUPT_IOAPIC_H

#include <stdint.h>

/*
 * The I/O APIC (Intel 82093AA datasheet; SDM Vol.3A 10.1): the chip that takes the interrupt wires
 * of the motherboard (the timer, the keyboard, PCI cards' INTx) and delivers each, as a
 * message with a vector number, to a local APIC. Each wire ("input", numbered by its global
 * system interrupt number, GSI) has a redirection entry that says which vector and whether it is
 * masked.
 */

#define IOAPIC_POLARITY_LOW 0x1u /* active low (default: active high) */
#define IOAPIC_TRIGGER_LEVEL 0x2u /* level triggered (default: edge) */

/*
 * Map the I/O APICs listed in the ACPI tables and mask all their inputs. Returns how many
 * were mapped (0 = none: keep using the 8259).
 */
int ioapic_init(void);

/*
 * Route input gsi to vector on the processor with APIC ID destination (physical mode, fixed
 * delivery) and leave it masked. flags are IOAPIC_*. Returns 0, or -1 if no I/O APIC has that input.
 */
int ioapic_route(uint32_t gsi, uint8_t vector, uint32_t destination, uint32_t flags);

/* Let input gsi through / stop it. Returns 0, or -1 if no I/O APIC has that input. */
int ioapic_unmask(uint32_t gsi);
int ioapic_mask(uint32_t gsi);

/* Number of inputs of all I/O APICs together (for diagnostics). */
uint32_t ioapic_input_count(void);

#endif
