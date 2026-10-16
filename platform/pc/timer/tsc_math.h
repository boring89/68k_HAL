/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PLATFORM_PC_TIMER_TSC_MATH_H
#define PLATFORM_PC_TIMER_TSC_MATH_H

#include <stdint.h>

/* The PIT's input clock, from OSDev Wiki "Programmable Interval Timer". */
#define TSC_PIT_HZ 1193182u

/* A TSC is slower than this (kHz) only on CPUs this kernel never meets. */
#define TSC_MIN_KHZ 4000u

#define TSC_NS_SHIFT 24

/*
 * TSC frequency in kHz, given that the TSC advanced by cycles while the
 * PIT counted pit_count of its own cycles. Returns 0 if the result is
 * below TSC_MIN_KHZ or the inputs are zero.
 */
uint32_t tsc_khz_from_sample(uint64_t cycles, uint32_t pit_count);

/*
 * Fixed-point factor so that ns = cycles * mult / 2^TSC_NS_SHIFT.
 * Returns 0 if khz < TSC_MIN_KHZ.
 */
uint32_t tsc_ns_multiplier(uint32_t khz);

/* Convert TSC cycles to nanoseconds without a 64-bit division. */
uint64_t tsc_cycles_to_ns(uint64_t cycles, uint32_t mult);

#endif
