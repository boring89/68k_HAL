/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "state.h"

static keyboard_state_t state;

void keyboard_state_init(void)
{
    state.left_shift = false;
    state.right_shift = false;

    state.left_ctrl = false;
    state.right_ctrl = false;

    state.left_alt = false;
    state.right_alt = false;

    state.caps_lock = false;
}

void keyboard_state_update(
    keycode_t key,
    bool pressed)
{
    switch (key)
    {
        case KEY_LEFT_SHIFT:
            state.left_shift = pressed;
            break;

        case KEY_RIGHT_SHIFT:
            state.right_shift = pressed;
            break;

        case KEY_LEFT_CTRL:
            state.left_ctrl = pressed;
            break;

        case KEY_RIGHT_CTRL:
            state.right_ctrl = pressed;
            break;

        case KEY_LEFT_ALT:
            state.left_alt = pressed;
            break;

        case KEY_RIGHT_ALT:
            state.right_alt = pressed;
            break;

        case KEY_CAPS_LOCK:
            /* A locking key: each press flips it. */
            if (pressed)
                state.caps_lock = !state.caps_lock;
            break;

        default:
            break;
    }
}

const keyboard_state_t *keyboard_state_get(void)
{
    return &state;
}