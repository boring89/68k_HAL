/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef HAL_IRQ_H
#define HAL_IRQ_H

#include <stdint.h>

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
 * Lines 0-15 are the classic ISA interrupts (timer 0, keyboard 1, mouse 12...); the platform
 * routes them to wherever its interrupt controller has them. Higher lines (up to
 * HAL_IRQ_LINE_COUNT - 1) exist only as targets of message signalled interrupts.
 */
#define HAL_IRQ_LINE_COUNT 64

/*
 * Message signalled interrupts (PCI MSI): a device interrupts by writing data to address, a
 * memory write the interrupt controller turns into a vector. Pick a free line, register handler
 * on it and store the address and data the device must be told. Returns the line, or -1 if
 * the platform does not support it (interrupts go through the 8259) or no line is free.
 */
int hal_irq_msi_alloc(hal_irq_handler_t handler, uint32_t *address, uint32_t *data);

/* "8259" or "IOAPIC": which controller delivers the interrupts. */
const char *hal_irq_controller(void);

/*
 * A hook the kernel gets whenever an interrupt is about to return to user mode (the interrupted code
 * was a user program), after the interrupt has been handled and acknowledged. It runs with interrupts
 * off on the kernel stack of that program and may switch to another process (the scheduler's preemption).
 * Never called when the interrupted code was kernel code, so the kernel stays single threaded.
 */
void hal_irq_set_user_return_hook(void (*hook)(void));

/* How many interrupts arrived on line (0 .. HAL_IRQ_LINE_COUNT - 1) since boot; 0 for other values. For diagnostics (`irqs`). */
unsigned int hal_irq_line_count(unsigned int line);

/*
 * How many interrupts have arrived since boot (wraps around). The scheduler uses it to see
 * whether anything happened while every process was waiting.
 */
unsigned int hal_irq_count(void);

#endif
