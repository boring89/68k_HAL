/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include <stdint.h>

#include "kernel/interrupt/exception.h"
#include "kernel/interrupt/page_fault.h"

static char buf[PAGE_FAULT_DESCRIBE_MAX];

static void test_describe_basic_bits(void)
{
    /* Null pointer write from the kernel: error code 0x2. */
    TEST_ASSERT_EQ_INT(0, page_fault_describe(0x0, buf, sizeof(buf)));
    TEST_ASSERT_EQ_STR("not present, read, kernel", buf);

    TEST_ASSERT_EQ_INT(0, page_fault_describe(EXCEPTION_PF_WRITE, buf, sizeof(buf)));
    TEST_ASSERT_EQ_STR("not present, write, kernel", buf);

    TEST_ASSERT_EQ_INT(0, page_fault_describe(EXCEPTION_PF_PRESENT, buf, sizeof(buf)));
    TEST_ASSERT_EQ_STR("protection violation, read, kernel", buf);

    TEST_ASSERT_EQ_INT(0, page_fault_describe(EXCEPTION_PF_USER, buf, sizeof(buf)));
    TEST_ASSERT_EQ_STR("not present, read, user", buf);
}

static void test_describe_optional_bits(void)
{
    TEST_ASSERT_EQ_INT(0, page_fault_describe(EXCEPTION_PF_PRESENT | EXCEPTION_PF_RESERVED,
                                              buf, sizeof(buf)));
    TEST_ASSERT_EQ_STR("protection violation, read, kernel, reserved bit", buf);

    TEST_ASSERT_EQ_INT(0, page_fault_describe(EXCEPTION_PF_FETCH, buf, sizeof(buf)));
    TEST_ASSERT_EQ_STR("not present, read, kernel, instruction fetch", buf);

    /* Every defined bit at once: the longest possible output. */
    TEST_ASSERT_EQ_INT(0, page_fault_describe(0x1F, buf, sizeof(buf)));
    TEST_ASSERT_EQ_STR("protection violation, write, user, reserved bit, instruction fetch",
                       buf);
}

static void test_describe_max_is_exact(void)
{
    /* PAGE_FAULT_DESCRIBE_MAX must fit the longest output with no slack. */
    TEST_ASSERT_EQ_INT(PAGE_FAULT_DESCRIBE_MAX, (long long)strlen(buf) + 1);
    TEST_ASSERT_EQ_INT(0, page_fault_describe(0x1F, buf, PAGE_FAULT_DESCRIBE_MAX));
    TEST_ASSERT_EQ_INT(-1, page_fault_describe(0x1F, buf, PAGE_FAULT_DESCRIBE_MAX - 1));
}

static void test_describe_ignores_undefined_bits(void)
{
    TEST_ASSERT_EQ_INT(0, page_fault_describe(0xFFFFFF00 | EXCEPTION_PF_WRITE,
                                              buf, sizeof(buf)));
    TEST_ASSERT_EQ_STR("not present, write, kernel", buf);
}

static void test_describe_buffer_size(void)
{
    /* "not present, read, kernel" is 25 chars: 26 bytes fit exactly. */
    char exact[26];
    char small[25];

    TEST_ASSERT_EQ_INT(0, page_fault_describe(0, exact, sizeof(exact)));
    TEST_ASSERT_EQ_STR("not present, read, kernel", exact);

    TEST_ASSERT_EQ_INT(-1, page_fault_describe(0, small, sizeof(small)));
    TEST_ASSERT_EQ_STR("", small);

    TEST_ASSERT_EQ_INT(-1, page_fault_describe(0, buf, 0));
    TEST_ASSERT_EQ_INT(-1, page_fault_describe(0, NULL, sizeof(buf)));
}

int main(void)
{
    TEST_RUN(test_describe_basic_bits);
    TEST_RUN(test_describe_optional_bits);
    TEST_RUN(test_describe_max_is_exact);
    TEST_RUN(test_describe_ignores_undefined_bits);
    TEST_RUN(test_describe_buffer_size);

    return TEST_REPORT("page_fault");
}
