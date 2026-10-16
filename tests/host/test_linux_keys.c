/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "kernel/input/linux_keys.h"

static void test_known_codes(void)
{
    /* Values from linux/input-event-codes.h */
    TEST_ASSERT_EQ_INT(1, input_linux_key_code(KEY_ESC));
    TEST_ASSERT_EQ_INT(2, input_linux_key_code(KEY_1));
    TEST_ASSERT_EQ_INT(11, input_linux_key_code(KEY_0));
    TEST_ASSERT_EQ_INT(16, input_linux_key_code(KEY_Q));
    TEST_ASSERT_EQ_INT(17, input_linux_key_code(KEY_W));
    TEST_ASSERT_EQ_INT(28, input_linux_key_code(KEY_ENTER));
    TEST_ASSERT_EQ_INT(30, input_linux_key_code(KEY_A));
    TEST_ASSERT_EQ_INT(44, input_linux_key_code(KEY_Z));
    TEST_ASSERT_EQ_INT(50, input_linux_key_code(KEY_M));
    TEST_ASSERT_EQ_INT(57, input_linux_key_code(KEY_SPACE));
    TEST_ASSERT_EQ_INT(59, input_linux_key_code(KEY_F1));
    TEST_ASSERT_EQ_INT(68, input_linux_key_code(KEY_F10));
    TEST_ASSERT_EQ_INT(87, input_linux_key_code(KEY_F11));
    TEST_ASSERT_EQ_INT(103, input_linux_key_code(KEY_UP));
    TEST_ASSERT_EQ_INT(108, input_linux_key_code(KEY_DOWN));
    TEST_ASSERT_EQ_INT(0, input_linux_key_code(KEY_NONE));
}

/* Every key we have maps to a different non-zero code. */
static void test_codes_are_unique(void)
{
    int seen[256] = {0};

    for (int key = KEY_A; key <= KEY_F12; key++)
    {
        int code = input_linux_key_code((keycode_t)key);

        TEST_ASSERT(code > 0 && code < 256);

        if (code > 0 && code < 256)
        {
            TEST_ASSERT_EQ_INT(0, seen[code]);
            seen[code] = 1;
        }
    }
}

int main(void)
{
    TEST_RUN(test_known_codes);
    TEST_RUN(test_codes_are_unique);

    return TEST_REPORT("linux_keys");
}
