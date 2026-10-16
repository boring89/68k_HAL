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

static void test_modifiers_and_navigation(void)
{
    TEST_ASSERT_EQ_INT(0x36, mac_adb_key_code(KEY_LEFT_CTRL)); /* Control */
    TEST_ASSERT_EQ_INT(0x37, mac_adb_key_code(KEY_LEFT_ALT));  /* Command */
    TEST_ASSERT_EQ_INT(0x3A, mac_adb_key_code(KEY_RIGHT_ALT)); /* Option */
    TEST_ASSERT_EQ_INT(0x39, mac_adb_key_code(KEY_CAPS_LOCK));
    TEST_ASSERT_EQ_INT(0x7B, mac_adb_key_code(KEY_LEFT));
    TEST_ASSERT_EQ_INT(0x7E, mac_adb_key_code(KEY_UP));
    TEST_ASSERT_EQ_INT(0x75, mac_adb_key_code(KEY_DELETE));
    TEST_ASSERT_EQ_INT(0x7A, mac_adb_key_code(KEY_F1));
    TEST_ASSERT_EQ_INT(0x6F, mac_adb_key_code(KEY_F12));
}

static void test_codes_are_unique(void)
{
    int seen[128] = { 0 };

    for (int key = KEY_A; key <= KEY_F12; key++)
    {
        int code = mac_adb_key_code((keycode_t)key);

        if (code < 0)
            continue;
        TEST_ASSERT(code < 128);
        /* ADB reports the right-hand modifiers with the left-hand codes, and Option has three
         * keys (AltGr and the two Windows keys); every other key has its own code. */
        int shares = key == KEY_RIGHT_SHIFT || key == KEY_RIGHT_CTRL || key == KEY_LEFT_GUI ||
                     key == KEY_RIGHT_GUI;

        if (!shares)
            TEST_ASSERT_EQ_INT(0, seen[code]);
        seen[code] = 1;
    }
}

int main(void)
{
    TEST_RUN(test_known_codes);
    TEST_RUN(test_modifiers_and_navigation);
    TEST_RUN(test_codes_are_unique);

    return TEST_REPORT("adbkeys");
}
