/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PLATFORM_PC_PCI_PCI_BAR_H
#define PLATFORM_PC_PCI_PCI_BAR_H

#include "hal/pci.h"

#include <stdint.h>

/*
 * Decoding a base address register (PCI Local Bus Specification 3.0,
 * 6.2.5.1). Pure functions: the caller does the config space accesses.
 *
 *   bit 0 = 1: I/O BAR   bits 31:2 = port base (bit 1 reserved)
 *   bit 0 = 0: memory    bits 2:1 = type (0 = 32-bit, 2 = 64-bit),
 *                        bit 3 = prefetchable, bits 31:4 = address
 *
 * The size is found by writing all ones and reading back: the bits that
 * stay zero are the address bits the device decodes, so
 * size = ~(readback & mask) + 1.
 */

/* Number of 32-bit registers this BAR takes (2 for a 64-bit memory BAR). */
unsigned int pci_bar_registers(uint32_t original);

/*
 * Fill *bar from the original value of the register (and of the next
 * one for a 64-bit BAR: original_high) and the values read back after
 * writing all ones (sized, sized_high). An unused BAR (original and
 * sized both 0) gives HAL_PCI_BAR_NONE.
 */
void pci_bar_decode(uint32_t original, uint32_t original_high,
                    uint32_t sized, uint32_t sized_high, hal_pci_bar_t *bar);

#endif
