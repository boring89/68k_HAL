/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "kernel/lib/k_snprintf.h"

#include <string.h>

static void test_formats(void)
{
    char out[64];

    TEST_ASSERT_EQ_INT(9, (int)k_snprintf(out, sizeof(out), "%s %d%%", "hello", 42));
    TEST_ASSERT_EQ_STR("hello 42%", out);

    k_snprintf(out, sizeof(out), "%d %d %u", -5, 0, 4000000000u);
    TEST_ASSERT_EQ_STR("-5 0 4000000000", out);

    k_snprintf(out, sizeof(out), "%x %08x %c", 0xBEEFu, 0xABCu, 'z');
    TEST_ASSERT_EQ_STR("beef 00000abc z", out);

    k_snprintf(out, sizeof(out), "[%5u] [%05d] [%6s]", 42u, -42, "ab");
    TEST_ASSERT_EQ_STR("[   42] [-0042] [    ab]", out);

    k_snprintf(out, sizeof(out), "%llu %lu %llx", 12345678901234ull, 7ul, 0x1122334455ull);
    TEST_ASSERT_EQ_STR("12345678901234 7 1122334455", out);

    k_snprintf(out, sizeof(out), "%d", -2147483647 - 1);
    TEST_ASSERT_EQ_STR("-2147483648", out);

    k_snprintf(out, sizeof(out), "%s|%q|100%", (const char *)NULL);
    TEST_ASSERT_EQ_STR("(null)|?|100", out);
}

static void test_truncation(void)
{
    char out[8];

    /* The return value is the length the whole text has; the buffer holds a cut, terminated copy. */
    TEST_ASSERT_EQ_INT(12, (int)k_snprintf(out, sizeof(out), "%s", "hello world!"));
    TEST_ASSERT_EQ_STR("hello w", out);

    TEST_ASSERT_EQ_INT(3, (int)k_snprintf(out, 1, "abc"));
    TEST_ASSERT_EQ_STR("", out);

    /* Size 0 writes nothing. */
    out[0] = 'x';
    TEST_ASSERT_EQ_INT(3, (int)k_snprintf(out, 0, "abc"));
    TEST_ASSERT_EQ_INT('x', out[0]);
}

int main(void)
{
    TEST_RUN(test_formats);
    TEST_RUN(test_truncation);

    return TEST_REPORT("k_snprintf");
}
