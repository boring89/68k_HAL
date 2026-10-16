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

        /*
         * Modifiers: Ctrl is Control, Alt is Command (so Alt-N is Command-N, as in
         * most Mac emulators), the Windows/Super key and AltGr are Option. ADB reports
         * the right-hand modifiers with the left-hand codes.
         */
        case KEY_LEFT_CTRL:
        case KEY_RIGHT_CTRL:
            return 0x36;
        case KEY_LEFT_ALT:
            return 0x37;
        case KEY_RIGHT_ALT:
        case KEY_LEFT_GUI:
        case KEY_RIGHT_GUI:
            return 0x3A;
        case KEY_CAPS_LOCK:
            return 0x39;

        /* Arrows, navigation block and function keys (Inside Macintosh: Text, Figure 2-10). */
        case KEY_LEFT:
            return 0x7B;
        case KEY_RIGHT:
            return 0x7C;
        case KEY_DOWN:
            return 0x7D;
        case KEY_UP:
            return 0x7E;
        case KEY_HOME:
            return 0x73;
        case KEY_END:
            return 0x77;
        case KEY_PAGE_UP:
            return 0x74;
        case KEY_PAGE_DOWN:
            return 0x79;
        case KEY_INSERT:
            return 0x72;    /* the Help key */
        case KEY_DELETE:
            return 0x75;    /* forward delete */
        case KEY_F1:
            return 0x7A;
        case KEY_F2:
            return 0x78;
        case KEY_F3:
            return 0x63;
        case KEY_F4:
            return 0x76;
        case KEY_F5:
            return 0x60;
        case KEY_F6:
            return 0x61;
        case KEY_F7:
            return 0x62;
        case KEY_F8:
            return 0x64;
        case KEY_F9:
            return 0x65;
        case KEY_F10:
            return 0x6D;
        case KEY_F11:
            return 0x67;
        case KEY_F12:
            return 0x6F;
        default:
            return -1;
    }
}
