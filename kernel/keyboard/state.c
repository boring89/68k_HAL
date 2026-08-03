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

        default:
            break;
    }
}

const keyboard_state_t *keyboard_state_get(void)
{
    return &state;
}