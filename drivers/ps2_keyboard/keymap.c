/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "keymap.h"

keycode_t keyboard_scancode_to_keycode(
    uint8_t scancode)
{
    switch (scancode)
    {
        /* Set 1 scancode of Escape (OSDev Wiki "PS/2 Keyboard"). */
    case 0x01:
        return KEY_ESC;

        /* Numbers */
    case 0x02:
        return KEY_1;

    case 0x03:
        return KEY_2;

    case 0x04:
        return KEY_3;

    case 0x05:
        return KEY_4;

    case 0x06:
        return KEY_5;

    case 0x07:
        return KEY_6;

    case 0x08:
        return KEY_7;

    case 0x09:
        return KEY_8;

    case 0x0A:
        return KEY_9;

    case 0x0B:
        return KEY_0;

        /* Letters */

    case 0x1E:
        return KEY_A;

    case 0x30:
        return KEY_B;

    case 0x2E:
        return KEY_C;

    case 0x20:
        return KEY_D;

    case 0x12:
        return KEY_E;

    case 0x21:
        return KEY_F;

    case 0x22:
        return KEY_G;

    case 0x23:
        return KEY_H;

    case 0x17:
        return KEY_I;

    case 0x24:
        return KEY_J;

    case 0x25:
        return KEY_K;

    case 0x26:
        return KEY_L;

    case 0x32:
        return KEY_M;

    case 0x31:
        return KEY_N;

    case 0x18:
        return KEY_O;

    case 0x19:
        return KEY_P;

    case 0x10:
        return KEY_Q;

    case 0x13:
        return KEY_R;

    case 0x1F:
        return KEY_S;

    case 0x14:
        return KEY_T;

    case 0x16:
        return KEY_U;

    case 0x2F:
        return KEY_V;

    case 0x11:
        return KEY_W;

    case 0x2D:
        return KEY_X;

    case 0x15:
        return KEY_Y;

    case 0x2C:
        return KEY_Z;

        /* Control */

    case 0x1C:
        return KEY_ENTER;

    case 0x39:
        return KEY_SPACE;

        /* Set 1 scancode of the ';' key (OSDev Wiki "PS/2 Keyboard"). */
    case 0x27:
        return KEY_SEMICOLON;

        /* Punctuation: set 1 make codes (OSDev Wiki "PS/2 Keyboard"). */
    case 0x29:
        return KEY_GRAVE;

    case 0x0C:
        return KEY_MINUS;

    case 0x0D:
        return KEY_EQUAL;

    case 0x1A:
        return KEY_LEFT_BRACKET;

    case 0x1B:
        return KEY_RIGHT_BRACKET;

    case 0x2B:
        return KEY_BACKSLASH;

    case 0x28:
        return KEY_APOSTROPHE;

    case 0x33:
        return KEY_COMMA;

    case 0x34:
        return KEY_PERIOD;

    case 0x35:
        return KEY_SLASH;

    case 0x0E:
        return KEY_BACKSPACE;

    case 0x0F:
        return KEY_TAB;

        /* Shift */

    case 0x2A:
        return KEY_LEFT_SHIFT;

    case 0x36:
        return KEY_RIGHT_SHIFT;

        /* Modifiers and function keys, scan code set 1 */

    case 0x1D:
        return KEY_LEFT_CTRL;

    case 0x38:
        return KEY_LEFT_ALT;

    case 0x3A:
        return KEY_CAPS_LOCK;

    case 0x3B:
        return KEY_F1;

    case 0x3C:
        return KEY_F2;

    case 0x3D:
        return KEY_F3;

    case 0x3E:
        return KEY_F4;

    case 0x3F:
        return KEY_F5;

    case 0x40:
        return KEY_F6;

    case 0x41:
        return KEY_F7;

    case 0x42:
        return KEY_F8;

    case 0x43:
        return KEY_F9;

    case 0x44:
        return KEY_F10;

    case 0x57:
        return KEY_F11;

    case 0x58:
        return KEY_F12;

    default:
        return KEY_NONE;
    }
}

keycode_t keyboard_extended_scancode_to_keycode(uint8_t scancode)
{
    switch (scancode)
    {
    case 0x1D:
        return KEY_RIGHT_CTRL;

    case 0x38:
        return KEY_RIGHT_ALT; /* AltGr */

    case 0x5B:
        return KEY_LEFT_GUI;

    case 0x5C:
        return KEY_RIGHT_GUI;

    case 0x48:
        return KEY_UP;

    case 0x50:
        return KEY_DOWN;

    case 0x4B:
        return KEY_LEFT;

    case 0x4D:
        return KEY_RIGHT;

    case 0x47:
        return KEY_HOME;

    case 0x4F:
        return KEY_END;

    case 0x49:
        return KEY_PAGE_UP;

    case 0x51:
        return KEY_PAGE_DOWN;

    case 0x52:
        return KEY_INSERT;

    case 0x53:
        return KEY_DELETE;

    /* The keypad's Enter and "/" are the main keys' twins. */
    case 0x1C:
        return KEY_ENTER;

    case 0x35:
        return KEY_SLASH;

    default:
        return KEY_NONE;
    }
}