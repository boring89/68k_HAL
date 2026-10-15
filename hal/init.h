/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef HAL_INIT_H
#define HAL_INIT_H

/*
 * Start-up of the hardware layer. Implemented once per platform
 * (platform/<platform>/hal_init.c), which decides what to bring up and
 * in which order.
 */

/*
 * Bring up the CPU tables, the MMU, the interrupt controller and the
 * system timer. Call once, after pmm_init and heap_init, with
 * interrupts disabled; they stay disabled. Afterwards hal/irq.h,
 * hal/mmu.h and hal/timer.h may be used.
 */
void hal_init(void);

#endif
