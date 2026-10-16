/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PLATFORM_PC_SMP_SMP_H
#define PLATFORM_PC_SMP_SMP_H

/*
 * Wake the other processors listed in the ACPI MADT with
 * INIT-SIPI-SIPI (Intel SDM Vol.3A 8.4.4). Each one reports and then
 * halts with interrupts off: interrupts stay with the 8259 on the boot
 * CPU. Call after acpi_init and tsc_init (it needs both), with
 * interrupts disabled. Returns the number of processors that came up,
 * the boot CPU included.
 */
unsigned int smp_start(void);

#endif
