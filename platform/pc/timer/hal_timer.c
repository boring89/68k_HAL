/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal/timer.h"

#include "pit.h"
#include "tsc.h"

uint32_t hal_timer_ticks(void)
{
    return pit_get_ticks();
}

uint32_t hal_timer_frequency(void)
{
    return pit_get_frequency();
}

uint64_t hal_timer_ns(void)
{
    if (tsc_khz() != 0)
        return tsc_now_ns();

    /* No TSC: count PIT ticks. */
    return (uint64_t)pit_get_ticks() * (1000000000u / pit_get_frequency());
}

uint32_t hal_timer_ns_resolution(void)
{
    return tsc_khz() != 0 ? 1 : 1000000000u / pit_get_frequency();
}
