/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef HAL_IRQ_H
#define HAL_IRQ_H

/*
 * Interrupt lines of the platform's interrupt controller. Used by
 * drivers, which know their own line number. Implemented once per
 * platform (platform/<platform>/interrupt/hal_irq.c).
 *
 * All lines start masked.
 */

/*
 * Runs with interrupts disabled. The end of interrupt is sent to the
 * controller after it returns.
 */
typedef void (*hal_irq_handler_t)(void);

/*
 * Call handler for every interrupt on line. Register before unmasking.
 * Returns 0, or -1 if the platform has no such line, handler is NULL
 * or the line already has a handler.
 */
int hal_irq_register(unsigned int line, hal_irq_handler_t handler);

/*
 * Let interrupts on line reach the CPU. Returns 0, or -1 if the
 * platform has no such line.
 */
int hal_irq_unmask(unsigned int line);

/*
 * How many interrupts have arrived since boot (wraps around). The scheduler uses it to see
 * whether anything happened while every process was waiting.
 */
unsigned int hal_irq_count(void);

#endif
