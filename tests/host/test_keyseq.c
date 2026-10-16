/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include <string.h>

#include "kernel/keyboard/keyseq.h"

static char out[KEYBOARD_SEQUENCE_MAX + 1];

/* The bytes of a key as a string, or "" when it sends nothing. */
static const char *bytes(keycode_t key, keyboard_state_t state)
{
    int n = keyboard_key_sequence(key, &state, out);

    out[n] = '\0';

    return out;
}

static void test_plain_keys(void)
{
    keyboard_state_t none = {0};
    keyboard_state_t shift = {.left_shift = true};
    keyboard_state_t ctrl = {.left_ctrl = true};

    TEST_ASSERT_EQ_STR("a", bytes(KEY_A, none));
    TEST_ASSERT_EQ_STR("A", bytes(KEY_A, shift));
    TEST_ASSERT_EQ_STR("\x03", bytes(KEY_C, ctrl));
    TEST_ASSERT_EQ_STR("\r", bytes(KEY_ENTER, none));
    TEST_ASSERT_EQ_STR("\x7f", bytes(KEY_BACKSPACE, none));
    TEST_ASSERT_EQ_STR("\x1b", bytes(KEY_ESC, none));
    TEST_ASSERT_EQ_STR("\t", bytes(KEY_TAB, none));
    TEST_ASSERT_EQ_STR(" ", bytes(KEY_SPACE, none));
    TEST_ASSERT_EQ_STR("\x1b", bytes(KEY_LEFT_BRACKET, ctrl));
    TEST_ASSERT_EQ_STR("\x1c", bytes(KEY_BACKSLASH, ctrl));
    TEST_ASSERT_EQ_INT(1, keyboard_key_sequence(KEY_SPACE, &ctrl, out));
    TEST_ASSERT_EQ_INT(0, out[0]);
}

static void test_navigation_and_function_keys(void)
{
    keyboard_state_t none = {0};

    TEST_ASSERT_EQ_STR("\x1b[A", bytes(KEY_UP, none));
    TEST_ASSERT_EQ_STR("\x1b[B", bytes(KEY_DOWN, none));
    TEST_ASSERT_EQ_STR("\x1b[C", bytes(KEY_RIGHT, none));
    TEST_ASSERT_EQ_STR("\x1b[D", bytes(KEY_LEFT, none));
    TEST_ASSERT_EQ_STR("\x1b[H", bytes(KEY_HOME, none));
    TEST_ASSERT_EQ_STR("\x1b[F", bytes(KEY_END, none));
    TEST_ASSERT_EQ_STR("\x1b[3~", bytes(KEY_DELETE, none));
    TEST_ASSERT_EQ_STR("\x1b[5~", bytes(KEY_PAGE_UP, none));
    TEST_ASSERT_EQ_STR("\x1bOP", bytes(KEY_F1, none));
    TEST_ASSERT_EQ_STR("\x1b[15~", bytes(KEY_F5, none));
    TEST_ASSERT_EQ_STR("\x1b[24~", bytes(KEY_F12, none));
}

static void test_alt_and_silent_keys(void)
{
    keyboard_state_t alt = {.right_alt = true};
    keyboard_state_t none = {0};

    TEST_ASSERT_EQ_STR("\x1b" "x", bytes(KEY_X, alt));
    TEST_ASSERT_EQ_STR("\x1b\x1b[A", bytes(KEY_UP, alt));
    /* Alt-Escape is just Escape. */
    TEST_ASSERT_EQ_STR("\x1b", bytes(KEY_ESC, alt));

    TEST_ASSERT_EQ_STR("", bytes(KEY_LEFT_SHIFT, none));
    TEST_ASSERT_EQ_STR("", bytes(KEY_CAPS_LOCK, none));
    TEST_ASSERT_EQ_STR("", bytes(KEY_LEFT_CTRL, none));
    TEST_ASSERT_EQ_STR("", bytes(KEY_NONE, none));
}

int main(void)
{
    TEST_RUN(test_plain_keys);
    TEST_RUN(test_navigation_and_function_keys);
    TEST_RUN(test_alt_and_silent_keys);

    return TEST_REPORT("keyseq");
}
