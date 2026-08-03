/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "pit.h"

#include "../../hal/io/io.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43

static uint32_t ticks = 0;

void pit_init(uint32_t frequency)
{
    uint32_t divisor = 1193182 / frequency;

    outb(PIT_CHANNEL0, divisor & 0xff);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xff);
}

void timer_tick(void)
{
    ticks++;
}

uint32_t pit_get_ticks(void)
{
    return ticks;
}