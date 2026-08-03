/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "keychar.h"

char keyboard_keycode_to_ascii(
    keycode_t key,
    const keyboard_state_t *state
)
{
    bool shift =
        state->left_shift ||
        state->right_shift;


    if(key >= KEY_A &&
       key <= KEY_Z)
    {
        char c =
            'a' + (key - KEY_A);

        if(shift)
            c -= 32;

        return c;
    }


    if(key >= KEY_0 &&
       key <= KEY_9)
    {
        return '0' + (key - KEY_0);
    }


    switch(key)
    {
        case KEY_SPACE:
            return ' ';

        case KEY_ENTER:
            return '\n';

        case KEY_TAB:
            return '\t';

        case KEY_BACKSPACE:
            return '\b';

        default:
            return 0;
    }
}