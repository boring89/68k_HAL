/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tsc_math.h"

uint32_t tsc_khz_from_sample(uint64_t cycles, uint32_t pit_count)
{
    if (cycles == 0 || pit_count == 0)
        return 0;

    /* time = pit_count / TSC_PIT_HZ s;  khz = cycles / time / 1000 */
    uint64_t khz = cycles * TSC_PIT_HZ / ((uint64_t)pit_count * 1000u);

    if (khz < TSC_MIN_KHZ || khz > 0xFFFFFFFFu)
        return 0;

    return (uint32_t)khz;
}

uint32_t tsc_ns_multiplier(uint32_t khz)
{
    if (khz < TSC_MIN_KHZ)
        return 0;

    /* ns per cycle = 10^6 / khz; scaled by 2^TSC_NS_SHIFT. */
    return (uint32_t)(((uint64_t)1000000u << TSC_NS_SHIFT) / khz);
}

uint64_t tsc_cycles_to_ns(uint64_t cycles, uint32_t mult)
{
    uint64_t hi = cycles >> 32;
    uint64_t lo = cycles & 0xFFFFFFFFu;

    /* (hi * 2^32 + lo) * mult >> 24, split so nothing exceeds 64 bits. */
    return ((hi * mult) << (32 - TSC_NS_SHIFT)) + ((lo * mult) >> TSC_NS_SHIFT);
}
