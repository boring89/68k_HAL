/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DRIVERS_AHCI_AHCI_H
#define DRIVERS_AHCI_AHCI_H

#include "hal/pci.h"

/*
 * SATA disks through an AHCI controller (PCI class 1, subclass 6,
 * programming interface 1): what nearly every PC from the last 15
 * years has, and QEMU's `-device ich9-ahci`. Reference: Serial ATA
 * Advanced Host Controller Interface (AHCI) 1.3.1 and OSDev Wiki "AHCI".
 * Every SATA disk found becomes a block device "sata0", "sata1", ... that can
 * be read and written (48-bit LBA, DMA, polled). No ATAPI, no port
 * multipliers, no hot plug, no interrupts.
 */
extern const hal_pci_driver_t ahci_pci_driver;

#endif
