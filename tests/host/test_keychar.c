/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include <stdint.h>
#include "kernel/keyboard/keychar.h"
#include "drivers/ps2_keyboard/keymap.h"

static char ch(keycode_t key, bool shift)
{
    keyboard_state_t st = { .left_shift = shift };

    return keyboard_keycode_to_ascii(key, &st);
}

static void test_punctuation(void)
{
    /* Set 1 make code -> unshifted / shifted character (US layout). */
    static const struct { uint8_t sc; char plain; char shifted; } t[] = {
        { 0x29, '`', '~' }, { 0x0C, '-', '_' }, { 0x0D, '=', '+' },
        { 0x1A, '[', '{' }, { 0x1B, ']', '}' }, { 0x2B, '\\', '|' },
        { 0x27, ';', ':' }, { 0x28, '\'', '"' }, { 0x33, ',', '<' },
        { 0x34, '.', '>' }, { 0x35, '/', '?' },
    };

    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++)
    {
        keycode_t k = keyboard_scancode_to_keycode(t[i].sc);

        TEST_ASSERT(k != KEY_NONE);
        TEST_ASSERT_EQ_INT(t[i].plain, ch(k, false));
        TEST_ASSERT_EQ_INT(t[i].shifted, ch(k, true));
    }
}

static void test_digits_shift(void)
{
    TEST_ASSERT_EQ_INT('1', ch(KEY_1, false));
    TEST_ASSERT_EQ_INT('!', ch(KEY_1, true));
    TEST_ASSERT_EQ_INT('0', ch(KEY_0, false));
    TEST_ASSERT_EQ_INT(')', ch(KEY_0, true));
    TEST_ASSERT_EQ_INT('A', ch(KEY_A, true));
}

static void test_control_and_caps(void)
{
    keyboard_state_t ctrl = { .left_ctrl = true };
    keyboard_state_t caps = { .caps_lock = true };
    keyboard_state_t caps_shift = { .caps_lock = true, .right_shift = true };

    TEST_ASSERT_EQ_INT(1, keyboard_keycode_to_ascii(KEY_A, &ctrl));
    TEST_ASSERT_EQ_INT(4, keyboard_keycode_to_ascii(KEY_D, &ctrl));
    TEST_ASSERT_EQ_INT(26, keyboard_keycode_to_ascii(KEY_Z, &ctrl));

    TEST_ASSERT_EQ_INT('A', keyboard_keycode_to_ascii(KEY_A, &caps));
    TEST_ASSERT_EQ_INT('a', keyboard_keycode_to_ascii(KEY_A, &caps_shift));
    TEST_ASSERT_EQ_INT('1', keyboard_keycode_to_ascii(KEY_1, &caps)); /* Caps Lock leaves digits alone */

    /* Arrows, function keys and modifiers have no character. */
    keyboard_state_t none = {0};

    TEST_ASSERT_EQ_INT(0, keyboard_keycode_to_ascii(KEY_UP, &none));
    TEST_ASSERT_EQ_INT(0, keyboard_keycode_to_ascii(KEY_F5, &none));
    TEST_ASSERT_EQ_INT(0, keyboard_keycode_to_ascii(KEY_LEFT_CTRL, &none));
}

int main(void)
{
    TEST_RUN(test_control_and_caps);
    TEST_RUN(test_punctuation);
    TEST_RUN(test_digits_shift);
    return TEST_REPORT("keychar");
}
