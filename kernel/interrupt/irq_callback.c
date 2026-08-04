/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "irq_callback.h"

static irq_handler_t irq_table[IRQ_MAX];

void irq_callback_init(void)
{
    for (int i = 0; i < IRQ_MAX; i++)
    {
        irq_table[i] = 0;
    }
}

int irq_register(
    uint8_t irq,
    irq_handler_t handler
)
{
    if (irq >= IRQ_MAX)
        return -1;

    if (handler == 0)
        return -1;

    irq_table[irq] = handler;

    return 0;
}

void irq_dispatch(uint8_t irq)
{
    if (irq >= IRQ_MAX)
        return;

    if (irq_table[irq])
    {
        irq_table[irq]();
    }
}