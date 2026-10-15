/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "pit.h"

#include "arch/x86/io/io.h"

#include "kernel/printf/printf.h"

#include "hal/irq.h"

/*
 * Ports, input clock and the 0x36 command (channel 0, lobyte/hibyte,
 * mode 3 square wave, binary) from OSDev Wiki "Programmable Interval
 * Timer".
 */
#define PIT_CHANNEL0 0x40
#define PIT_COMMAND 0x43

/* Written by IRQ0, read by the main loop. */
static volatile uint32_t ticks = 0;

static uint32_t pit_frequency;

static void timer_tick(void)
{
    ticks++;
}

void pit_init(uint32_t frequency)
{
    pit_frequency = frequency;

    uint32_t divisor = 1193182 / frequency;

    outb(PIT_COMMAND, 0x36);

    outb(PIT_CHANNEL0, divisor & 0xff);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xff);
    
    hal_irq_register(0, timer_tick);
    hal_irq_unmask(0);
}

uint32_t pit_get_ticks(void)
{
    return ticks;
}

uint32_t pit_get_frequency(void)
{
    return pit_frequency;
}