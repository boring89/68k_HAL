/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_TIMER_PIT_H
#define KERNEL_TIMER_PIT_H

#include <stdint.h>

void pit_init(uint32_t frequency);
void timer_tick(void);

uint32_t pit_get_ticks(void);

#endif