/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PLATFORM_PC_TIMER_TSC_H
#define PLATFORM_PC_TIMER_TSC_H

#include <stdint.h>

/*
 * Measure the TSC frequency against PIT channel 2 and start the
 * nanosecond clock. Needs the CPU's TSC; call after hal_cpu_init.
 * Returns 0, or -1 if there is no TSC or the measurement is unusable
 * (tsc_now_ns then returns 0).
 */
int tsc_init(void);

/* The measured frequency in kHz; 0 if tsc_init failed. */
uint32_t tsc_khz(void);

/* Nanoseconds since tsc_init. */
uint64_t tsc_now_ns(void);

#endif
