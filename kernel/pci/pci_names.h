/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_PCI_PCI_NAMES_H
#define KERNEL_PCI_PCI_NAMES_H

#include <stdint.h>

/*
 * Human readable names for lspci. The class names are from the PCI
 * Code and ID Assignment Specification; the vendor names are the few
 * that show up in a PC and in QEMU. Unknown values give "Unknown".
 */

const char *pci_class_name(uint8_t class_code, uint8_t subclass);

const char *pci_vendor_name(uint16_t vendor_id);

#endif
