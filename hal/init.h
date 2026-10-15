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
 * Register the console devices: the text display and the console sinks.
 * Call once, first thing in kernel_main, before console_init. Must not
 * log.
 */
void hal_init_console(void);

/*
 * Check and record what the boot loader passed in. Call once, right
 * after console_init (it logs, and halts on a bad boot). Afterwards
 * hal/boot.h may be used.
 */
void hal_init_early(void);

/*
 * Bring up the CPU tables, the MMU, the interrupt controller, the
 * system timer and the input devices. Call once, after pmm_init and
 * heap_init, with interrupts disabled; they stay disabled. Afterwards
 * hal/irq.h, hal/mmu.h and hal/timer.h may be used. Devices only start
 * delivering events once interrupts are enabled, so the kernel services
 * they feed (the input event buffer) must be ready by then.
 */
void hal_init(void);

#endif
