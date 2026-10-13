/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef HAL_CPU_H
#define HAL_CPU_H

/*
 * Control of the current CPU. Implemented once per architecture
 * (arch/<arch>/cpu/hal_cpu.c).
 */

/*
 * Let external interrupts (timer, keyboard, ...) reach the CPU.
 * Also a compiler barrier: memory accesses are not moved across it.
 */
void hal_cpu_enable_interrupts(void);

/*
 * Stop external interrupts from reaching the CPU until
 * hal_cpu_enable_interrupts(). CPU exceptions still happen.
 * Also a compiler barrier.
 */
void hal_cpu_disable_interrupts(void);

/*
 * Sleep until the next interrupt arrives and has been handled.
 * Interrupts must be enabled, or the CPU never wakes up.
 */
void hal_cpu_idle(void);

/* Disable interrupts and stop the CPU for good. */
void hal_cpu_halt(void) __attribute__((noreturn));

#endif
