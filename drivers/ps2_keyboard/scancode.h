/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DRIVERS_PS2_KEYBOARD_SCANCODE_H
#define DRIVERS_PS2_KEYBOARD_SCANCODE_H

#include <stdint.h>
#include <stdbool.h>

typedef struct 
{
    uint8_t code;     /* scan code set 1 without the release bit */
    bool pressed;
    bool extended;    /* the code came after the 0xE0 prefix (arrows, right Ctrl/Alt, Delete...) */
} keyboard_key_event_t;

void scancode_init(void);

bool scancode_process(
    uint8_t scancode,
    keyboard_key_event_t *event
);

#endif
