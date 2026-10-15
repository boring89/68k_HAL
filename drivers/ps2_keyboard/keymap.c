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

    case 0x0E:
        return KEY_BACKSPACE;

    case 0x0F:
        return KEY_TAB;

        /* Shift */

    case 0x2A:
        return KEY_LEFT_SHIFT;

    case 0x36:
        return KEY_RIGHT_SHIFT;

    default:
        return KEY_NONE;
    }
}