/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "scancode.h"

void scancode_init(void)
{
}

bool scancode_process(
    uint8_t scancode,
    keyboard_key_event_t *event
)
{
    event->code = scancode & 0x7F;

    if(scancode & 0x80)
    {
        event->pressed = false;
    }
    else
    {
        event->pressed = true;
    }

    return true;
}