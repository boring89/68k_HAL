/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal/timer.h"

#include "pit.h"

uint32_t hal_timer_ticks(void)
{
    return pit_get_ticks();
}

uint32_t hal_timer_frequency(void)
{
    return pit_get_frequency();
}
