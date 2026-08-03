/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_KEYBOARD_STATE_H
#define KERNEL_KEYBOARD_STATE_H

#include <stdbool.h>
#include "keycode.h"

typedef struct
{
    bool left_shift;
    bool right_shift;

    bool left_ctrl;
    bool right_ctrl;

    bool left_alt;
    bool right_alt;

    bool caps_lock;

} keyboard_state_t;

void keyboard_state_init(void);

void keyboard_state_update(
    keycode_t key,
    bool pressed
);

const keyboard_state_t *keyboard_state_get(void);

#endif