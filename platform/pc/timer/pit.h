/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PLATFORM_PC_TIMER_PIT_H
#define PLATFORM_PC_TIMER_PIT_H

#include <stdint.h>

/* Program channel 0 to raise IRQ 0 frequency times per second. */
void pit_init(uint32_t frequency);

uint32_t pit_get_ticks(void);

/* The frequency passed to pit_init. */
uint32_t pit_get_frequency(void);

#endif