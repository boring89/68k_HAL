/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_KEYBOARD_SCANCODE_H
#define KERNEL_KEYBOARD_SCANCODE_H

#include <stdint.h>
#include <stdbool.h>

typedef struct 
{
    uint8_t code;
    bool pressed;
} keyboard_key_event_t;

void scancode_init(void);

bool scancode_process(
    uint8_t scancode,
    keyboard_key_event_t *event
);

#endif
