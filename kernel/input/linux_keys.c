/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "linux_keys.h"

/* linux/input-event-codes.h */
static const unsigned char letters[26] = {
    30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50, /* A-M */
    49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44  /* N-Z */
};

int input_linux_key_code(keycode_t key)
{
    if (key >= KEY_A && key <= KEY_Z)
        return letters[key - KEY_A];

    /* KEY_1 .. KEY_9 are 2..10, KEY_0 is 11. */
    if (key >= KEY_1 && key <= KEY_9)
        return 2 + (key - KEY_1);

    if (key == KEY_0)
        return 11;

    if (key >= KEY_F1 && key <= KEY_F10)
        return 59 + (key - KEY_F1);

    switch (key)
    {
    case KEY_ESC:
        return 1;
    case KEY_MINUS:
        return 12;
    case KEY_EQUAL:
        return 13;
    case KEY_BACKSPACE:
        return 14;
    case KEY_TAB:
        return 15;
    case KEY_LEFT_BRACKET:
        return 26;
    case KEY_RIGHT_BRACKET:
        return 27;
    case KEY_ENTER:
        return 28;
    case KEY_LEFT_CTRL:
        return 29;
    case KEY_SEMICOLON:
        return 39;
    case KEY_APOSTROPHE:
        return 40;
    case KEY_GRAVE:
        return 41;
    case KEY_LEFT_SHIFT:
        return 42;
    case KEY_BACKSLASH:
        return 43;
    case KEY_COMMA:
        return 51;
    case KEY_PERIOD:
        return 52;
    case KEY_SLASH:
        return 53;
    case KEY_RIGHT_SHIFT:
        return 54;
    case KEY_LEFT_ALT:
        return 56;
    case KEY_SPACE:
        return 57;
    case KEY_CAPS_LOCK:
        return 58;
    case KEY_F11:
        return 87;
    case KEY_F12:
        return 88;
    case KEY_RIGHT_CTRL:
        return 97;
    case KEY_RIGHT_ALT:
        return 100;
    case KEY_HOME:
        return 102;
    case KEY_UP:
        return 103;
    case KEY_PAGE_UP:
        return 104;
    case KEY_LEFT:
        return 105;
    case KEY_RIGHT:
        return 106;
    case KEY_END:
        return 107;
    case KEY_DOWN:
        return 108;
    case KEY_PAGE_DOWN:
        return 109;
    case KEY_INSERT:
        return 110;
    case KEY_DELETE:
        return 111;
    case KEY_LEFT_GUI:
        return 125;
    case KEY_RIGHT_GUI:
        return 126;
    default:
        return 0;
    }
}
