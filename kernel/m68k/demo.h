/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_M68K_DEMO_H
#define KERNEL_M68K_DEMO_H

/*
 * Run a small built-in 68k program (sums 1..10, then makes an A-line trap)
 * in the guest VM and print the registers and the trap log on the terminal.
 */
void m68k_demo_run(void);

#endif
