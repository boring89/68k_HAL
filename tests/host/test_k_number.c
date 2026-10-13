/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "kernel/lib/k_number.h"

static void test_utoa(void)
{
    char buffer[32];

    k_utoa(0, buffer);
    TEST_ASSERT_EQ_STR("0", buffer);

    k_utoa(12345, buffer);
    TEST_ASSERT_EQ_STR("12345", buffer);

    k_utoa(4294967295u, buffer);
    TEST_ASSERT_EQ_STR("4294967295", buffer);
}

static void test_itoa(void)
{
    char buffer[32];

    k_itoa(0, buffer);
    TEST_ASSERT_EQ_STR("0", buffer);

    k_itoa(-42, buffer);
    TEST_ASSERT_EQ_STR("-42", buffer);

    k_itoa(-2147483647 - 1, buffer);
    TEST_ASSERT_EQ_STR("-2147483648", buffer);
}

static void test_utoa_hex(void)
{
    char buffer[32];

    k_utoa_hex(0, buffer);
    TEST_ASSERT_EQ_STR("0", buffer);

    k_utoa_hex(0xBEEF, buffer);
    TEST_ASSERT_EQ_STR("BEEF", buffer);

    k_utoa_ptr(0x100000, buffer);
    TEST_ASSERT_EQ_STR("0x100000", buffer);
}

static void test_atoi(void)
{
    int value;

    k_atoi("123", &value);
    TEST_ASSERT_EQ_INT(123, value);

    k_atoi("-7", &value);
    TEST_ASSERT_EQ_INT(-7, value);

    k_atoi("42abc", &value);
    TEST_ASSERT_EQ_INT(42, value);

    k_atoi("abc", &value);
    TEST_ASSERT_EQ_INT(0, value);
}

static void test_atoh(void)
{
    TEST_ASSERT_EQ_INT(0x1F, k_atoh("1f"));
    TEST_ASSERT_EQ_INT(0xABCD, k_atoh("0xABCD"));
    TEST_ASSERT_EQ_INT(0x10, k_atoh("0X10"));
    TEST_ASSERT_EQ_INT(0x12, k_atoh("12zz"));
}

int main(void)
{
    TEST_RUN(test_utoa);
    TEST_RUN(test_itoa);
    TEST_RUN(test_utoa_hex);
    TEST_RUN(test_atoi);
    TEST_RUN(test_atoh);

    return TEST_REPORT("k_number");
}
