/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "drivers/ps2_keyboard/keymap.h"
#include "drivers/ps2_keyboard/scancode.h"

/* Scancode set 1: bit 7 set means release (OSDev Wiki "PS/2 Keyboard"). */
static void test_scancode_press_and_release(void)
{
    keyboard_key_event_t key;

    TEST_ASSERT(scancode_process(0x1E, &key));
    TEST_ASSERT_EQ_INT(0x1E, key.code);
    TEST_ASSERT(key.pressed);

    TEST_ASSERT(scancode_process(0x9E, &key));
    TEST_ASSERT_EQ_INT(0x1E, key.code);
    TEST_ASSERT(!key.pressed);
}

static void test_keymap(void)
{
    TEST_ASSERT_EQ_INT(KEY_A, keyboard_scancode_to_keycode(0x1E));
    TEST_ASSERT_EQ_INT(KEY_1, keyboard_scancode_to_keycode(0x02));
    TEST_ASSERT_EQ_INT(KEY_ENTER, keyboard_scancode_to_keycode(0x1C));
    TEST_ASSERT_EQ_INT(KEY_SPACE, keyboard_scancode_to_keycode(0x39));
    TEST_ASSERT_EQ_INT(KEY_SEMICOLON, keyboard_scancode_to_keycode(0x27));
}

static void test_keymap_unknown(void)
{
    /* 0x00 is not a key in set 1. */
    TEST_ASSERT_EQ_INT(KEY_NONE, keyboard_scancode_to_keycode(0x00));
}

int main(void)
{
    TEST_RUN(test_scancode_press_and_release);
    TEST_RUN(test_keymap);
    TEST_RUN(test_keymap_unknown);

    return TEST_REPORT("ps2_keyboard");
}
