/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PLATFORM_PC_PCI_PCI_H
#define PLATFORM_PC_PCI_PCI_H

#include "hal/pci.h"

/*
 * Walk the PCI buses and fill the device list (hal_pci_device_*).
 * Call once, after paging_init and before pic_init is not required.
 * Returns the number of devices found, 0 if the machine has no PCI.
 */
unsigned int pci_init(void);

#endif
