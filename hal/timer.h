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

/*
 * Nanoseconds since the timer started, from the finest clock the
 * platform has (the PC uses the CPU's time stamp counter, measured at
 * boot). Falls back to ticks if there is none, then the resolution is
 * 1 / hal_timer_frequency() seconds. Does not wrap in practice (2^64 ns
 * is 584 years).
 */
uint64_t hal_timer_ns(void);

/* Resolution hint: 1 if hal_timer_ns is fine grained, else ns per step. */
uint32_t hal_timer_ns_resolution(void);

#endif
