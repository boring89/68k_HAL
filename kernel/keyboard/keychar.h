/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_KEYBOARD_KEYCHAR_H
#define KERNEL_KEYBOARD_KEYCHAR_H

#include "keycode.h"
#include "state.h"

char keyboard_keycode_to_ascii(
    keycode_t key,
    const keyboard_state_t *state
);

#endif