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

/* Extended keys send 0xE0 first; the prefix itself is not a key. */
static void test_scancode_extended(void)
{
    keyboard_key_event_t key;

    scancode_init();
    TEST_ASSERT(!scancode_process(0xE0, &key));
    TEST_ASSERT(scancode_process(0x48, &key)); /* Up arrow */
    TEST_ASSERT_EQ_INT(0x48, key.code);
    TEST_ASSERT(key.pressed);
    TEST_ASSERT(key.extended);
    TEST_ASSERT_EQ_INT(KEY_UP, keyboard_extended_scancode_to_keycode(key.code));

    TEST_ASSERT(!scancode_process(0xE0, &key));
    TEST_ASSERT(scancode_process(0xC8, &key)); /* its release */
    TEST_ASSERT(!key.pressed);
    TEST_ASSERT(key.extended);

    /* The prefix applies to one code only. */
    TEST_ASSERT(scancode_process(0x48, &key));
    TEST_ASSERT(!key.extended);
    TEST_ASSERT_EQ_INT(KEY_NONE, keyboard_scancode_to_keycode(0x48));
}

static void test_scancode_pause_and_replies(void)
{
    keyboard_key_event_t key;

    scancode_init();

    /* Pause: E1 1D 45 E1 9D C5, none of it is a key. */
    static const uint8_t pause[] = {0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5};

    for (unsigned int i = 0; i < sizeof(pause); i++)
        TEST_ASSERT(!scancode_process(pause[i], &key));

    /* Acknowledge and error bytes; but 0xAA is the left Shift release. */
    TEST_ASSERT(!scancode_process(0xFA, &key));
    TEST_ASSERT(!scancode_process(0xFF, &key));
    TEST_ASSERT(scancode_process(0xAA, &key));
    TEST_ASSERT_EQ_INT(KEY_LEFT_SHIFT, keyboard_scancode_to_keycode(key.code));
    TEST_ASSERT(!key.pressed);
    TEST_ASSERT(scancode_process(0x1E, &key)); /* normal again */
}

static void test_keymap_new_keys(void)
{
    TEST_ASSERT_EQ_INT(KEY_LEFT_CTRL, keyboard_scancode_to_keycode(0x1D));
    TEST_ASSERT_EQ_INT(KEY_RIGHT_CTRL, keyboard_extended_scancode_to_keycode(0x1D));
    TEST_ASSERT_EQ_INT(KEY_LEFT_ALT, keyboard_scancode_to_keycode(0x38));
    TEST_ASSERT_EQ_INT(KEY_RIGHT_ALT, keyboard_extended_scancode_to_keycode(0x38));
    TEST_ASSERT_EQ_INT(KEY_CAPS_LOCK, keyboard_scancode_to_keycode(0x3A));
    TEST_ASSERT_EQ_INT(KEY_F1, keyboard_scancode_to_keycode(0x3B));
    TEST_ASSERT_EQ_INT(KEY_F10, keyboard_scancode_to_keycode(0x44));
    TEST_ASSERT_EQ_INT(KEY_F12, keyboard_scancode_to_keycode(0x58));
    TEST_ASSERT_EQ_INT(KEY_DELETE, keyboard_extended_scancode_to_keycode(0x53));
    TEST_ASSERT_EQ_INT(KEY_PAGE_DOWN, keyboard_extended_scancode_to_keycode(0x51));
    TEST_ASSERT_EQ_INT(KEY_ENTER, keyboard_extended_scancode_to_keycode(0x1C)); /* keypad Enter */
}

static void test_keymap(void)
{
    TEST_ASSERT_EQ_INT(KEY_A, keyboard_scancode_to_keycode(0x1E));
    TEST_ASSERT_EQ_INT(KEY_1, keyboard_scancode_to_keycode(0x02));
    TEST_ASSERT_EQ_INT(KEY_ENTER, keyboard_scancode_to_keycode(0x1C));
    TEST_ASSERT_EQ_INT(KEY_SPACE, keyboard_scancode_to_keycode(0x39));
    TEST_ASSERT_EQ_INT(KEY_SEMICOLON, keyboard_scancode_to_keycode(0x27));
    TEST_ASSERT_EQ_INT(KEY_ESC, keyboard_scancode_to_keycode(0x01));
}

static void test_keymap_unknown(void)
{
    /* 0x00 is not a key in set 1. */
    TEST_ASSERT_EQ_INT(KEY_NONE, keyboard_scancode_to_keycode(0x00));
}

int main(void)
{
    TEST_RUN(test_scancode_press_and_release);
    TEST_RUN(test_scancode_extended);
    TEST_RUN(test_scancode_pause_and_replies);
    TEST_RUN(test_keymap_new_keys);
    TEST_RUN(test_keymap);
    TEST_RUN(test_keymap_unknown);

    return TEST_REPORT("ps2_keyboard");
}
