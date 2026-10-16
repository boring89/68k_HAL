/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DRIVERS_E1000_E1000_H
#define DRIVERS_E1000_E1000_H

#include "hal/pci.h"

/*
 * Intel 8254x "e1000" gigabit Ethernet cards: the 82540EM that QEMU
 * (and VirtualBox, VMware) emulate as the default network card, and a
 * few relatives with the same register layout. Registers and
 * descriptor formats are from the Intel 8254x Family of Gigabit
 * Ethernet Controllers Software Developer's Manual (317453). The
 * driver polls the receive ring from net_poll; the interrupt only
 * acknowledges the card.
 *
 * Handles PCI 8086:100E (82540EM), 100F (82545EM copper), 1004
 * (82543GC), 10D3 (82574L, "e1000e"-style cards are not supported).
 */
extern const hal_pci_driver_t e1000_pci_driver;

/* Device IDs by the card they name (Intel vendor ID 0x8086). */
#define E1000_DEVICE_82540EM 0x100E

#endif
