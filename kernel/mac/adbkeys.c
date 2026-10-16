/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "adbkeys.h"

static const signed char letter_codes[26] = {
    /* a   b     c     d     e     f     g     h     i     j     k     l     m */
    0x00, 0x0B, 0x08, 0x02, 0x0E, 0x03, 0x05, 0x04, 0x22, 0x26, 0x28, 0x25, 0x2E,
    /* n   o     p     q     r     s     t     u     v     w     x     y     z */
    0x2D, 0x1F, 0x23, 0x0C, 0x0F, 0x01, 0x11, 0x20, 0x09, 0x0D, 0x07, 0x10, 0x06,
};

/* KEY_0 .. KEY_9 */
static const signed char digit_codes[10] = {
    0x1D, 0x12, 0x13, 0x14, 0x15, 0x17, 0x16, 0x1A, 0x1C, 0x19,
};

int mac_adb_key_code(keycode_t key)
{
    if (key >= KEY_A && key <= KEY_Z)
        return letter_codes[key - KEY_A];

    if (key >= KEY_0 && key <= KEY_9)
        return digit_codes[key - KEY_0];

    switch (key)
    {
        case KEY_ENTER:
            return 0x24;
        case KEY_ESC:
            return 0x35;
        case KEY_BACKSPACE:
            return 0x33;        /* the Mac's "delete" */
        case KEY_TAB:
            return 0x30;
        case KEY_SPACE:
            return 0x31;
        case KEY_SEMICOLON:
            return 0x29;
        case KEY_GRAVE:
            return 0x32;
        case KEY_MINUS:
            return 0x1B;
        case KEY_EQUAL:
            return 0x18;
        case KEY_LEFT_BRACKET:
            return 0x21;
        case KEY_RIGHT_BRACKET:
            return 0x1E;
        case KEY_BACKSLASH:
            return 0x2A;
        case KEY_APOSTROPHE:
            return 0x27;
        case KEY_COMMA:
            return 0x2B;
        case KEY_PERIOD:
            return 0x2F;
        case KEY_SLASH:
            return 0x2C;
        case KEY_LEFT_SHIFT:
        case KEY_RIGHT_SHIFT:
            return 0x38;
        default:
            return -1;
    }
}
