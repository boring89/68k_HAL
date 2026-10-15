/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_MAIN_H
#define KERNEL_MAIN_H

/*
 * The kernel proper. Called once by the platform's entry code with
 * interrupts disabled; never returns.
 */
void kernel_main(void);

#endif
