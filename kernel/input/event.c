/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "event.h"

input_event_t input_event_keyboard(
    keycode_t key,
    bool pressed
)
{
    input_event_t event = {0};

    event.type = INPUT_EVENT_KEYBOARD;

    event.data.keyboard.key = key;
    event.data.keyboard.pressed = pressed;

    return event;
}