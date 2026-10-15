/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef HAL_TIMER_H
#define HAL_TIMER_H

#include <stdint.h>

/*
 * The platform's periodic system timer. The platform starts it and
 * picks the frequency. Implemented once per platform
 * (platform/<platform>/timer/hal_timer.c).
 */

/*
 * Ticks since the timer started. Wraps around at 2^32; compute
 * intervals as (now - start), which stays correct across one wrap.
 */
uint32_t hal_timer_ticks(void);

/* Nominal ticks per second. */
uint32_t hal_timer_frequency(void);

#endif
