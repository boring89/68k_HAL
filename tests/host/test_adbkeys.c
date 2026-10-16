/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"
#include "kernel/mac/adbkeys.h"

static void test_known_codes(void)
{
    /* A few keys of the US layout (Inside Macintosh: Text). */
    TEST_ASSERT_EQ_INT(0x00, mac_adb_key_code(KEY_A));
    TEST_ASSERT_EQ_INT(0x11, mac_adb_key_code(KEY_T));
    TEST_ASSERT_EQ_INT(0x06, mac_adb_key_code(KEY_Z));
    TEST_ASSERT_EQ_INT(0x12, mac_adb_key_code(KEY_1));
    TEST_ASSERT_EQ_INT(0x1D, mac_adb_key_code(KEY_0));
    TEST_ASSERT_EQ_INT(0x24, mac_adb_key_code(KEY_ENTER));
    TEST_ASSERT_EQ_INT(0x31, mac_adb_key_code(KEY_SPACE));
    TEST_ASSERT_EQ_INT(0x38, mac_adb_key_code(KEY_LEFT_SHIFT));
    TEST_ASSERT_EQ_INT(-1, mac_adb_key_code(KEY_NONE));
}

static void test_codes_are_unique(void)
{
    int seen[128] = { 0 };

    for (int key = KEY_A; key <= KEY_SLASH; key++)
    {
        int code = mac_adb_key_code((keycode_t)key);

        if (code < 0)
            continue;
        TEST_ASSERT(code < 128);
        /* Two shift keys share a code; every other key has its own. */
        if (key != KEY_RIGHT_SHIFT)
            TEST_ASSERT_EQ_INT(0, seen[code]);
        seen[code] = 1;
    }
}

int main(void)
{
    TEST_RUN(test_known_codes);
    TEST_RUN(test_codes_are_unique);

    return TEST_REPORT("adbkeys");
}
