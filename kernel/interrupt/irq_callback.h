/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_INTERRUPT_IRQ_CALLBACK_H
#define KERNEL_INTERRUPT_IRQ_CALLBACK_H

#include <stdint.h>

#define IRQ_MAX 16

typedef void (*irq_handler_t)(void);

void irq_callback_init(void);

int irq_register(
    uint8_t irq,
    irq_handler_t handler
);

void irq_dispatch(uint8_t irq);

#endif