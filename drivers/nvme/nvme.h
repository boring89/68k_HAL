/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DRIVERS_NVME_NVME_H
#define DRIVERS_NVME_NVME_H

#include "hal/pci.h"

/*
 * NVMe solid state drives (PCI class 1, subclass 8, programming interface 2): what nearly every
 * new PC boots from, and QEMU's `-device nvme`. Reference: NVM Express Base Specification 1.4
 * and OSDev Wiki "NVMe". Namespace 1 of each controller becomes a block device "nvme0",
 * "nvme1", ...: 48-bit-free (64-bit) LBA, polled, one command at a time, one I/O queue, data
 * moved through a 128 KiB bounce buffer. No interrupts, no namespace management, no multiple
 * namespaces, no power management.
 */
extern const hal_pci_driver_t nvme_pci_driver;

#endif
