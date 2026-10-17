/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DRIVERS_E1000E_E1000E_H
#define DRIVERS_E1000E_E1000E_H

#include "hal/pci.h"

/*
 * Intel "e1000e" family gigabit Ethernet: the 82574L (QEMU's `-device e1000e`) and the I219
 * LAN controllers built into the Intel PCH (the Lenovo ThinkStation P520 has an I219-LM,
 * 8086:15B7). Same legacy descriptor rings as drivers/e1000, but a different reset, a few
 * workarounds for the PCH parts, and PHY link handled by the chip. Polled; no interrupts.
 * Written from Intel's public documentation and the BSD-licensed FreeBSD em(4) driver
 * (sys/dev/e1000); see docs/net.md. The I219 part is NOT verified on real hardware.
 */
#define E1000E_ID_COUNT 9
extern const hal_pci_driver_t e1000e_pci_drivers[E1000E_ID_COUNT];

#endif
