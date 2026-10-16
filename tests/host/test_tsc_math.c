/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "platform/pc/timer/tsc_math.h"

static void test_khz_from_sample(void)
{
    /* 20 ms at 3 GHz = 60,000,000 cycles; PIT counted 23864 (19.999 ms). */
    uint32_t khz = tsc_khz_from_sample(60000000u, 23864);

    TEST_ASSERT(khz > 2999000u && khz < 3001000u);

    /* 20 ms at 1 GHz */
    khz = tsc_khz_from_sample(20000000u, 23864);
    TEST_ASSERT(khz > 999000u && khz < 1001000u);
}

static void test_khz_rejects(void)
{
    TEST_ASSERT_EQ_INT(0, tsc_khz_from_sample(0, 23864));
    TEST_ASSERT_EQ_INT(0, tsc_khz_from_sample(1000, 0));
    /* 1 MHz is too slow to be believed. */
    TEST_ASSERT_EQ_INT(0, tsc_khz_from_sample(20000, 23864));
}

static void test_ns_conversion(void)
{
    uint32_t mult = tsc_ns_multiplier(3000000u);

    TEST_ASSERT(mult != 0);

    /* 3 GHz: 3e9 cycles = 1 s, within 0.01%. */
    uint64_t ns = tsc_cycles_to_ns(3000000000u, mult);

    TEST_ASSERT(ns > 999900000u && ns < 1000100000u);

    /* A day of cycles needs the high word: 86400 s * 3e9. */
    ns = tsc_cycles_to_ns(86400ull * 3000000000ull, mult);
    TEST_ASSERT(ns > 86399ull * 1000000000ull && ns < 86401ull * 1000000000ull);

    TEST_ASSERT_EQ_INT(0, (int)tsc_cycles_to_ns(0, mult));
}

static void test_multiplier_rejects(void)
{
    TEST_ASSERT_EQ_INT(0, tsc_ns_multiplier(0));
    TEST_ASSERT_EQ_INT(0, tsc_ns_multiplier(100));
}

int main(void)
{
    TEST_RUN(test_khz_from_sample);
    TEST_RUN(test_khz_rejects);
    TEST_RUN(test_ns_conversion);
    TEST_RUN(test_multiplier_rejects);

    return TEST_REPORT("tsc_math");
}
