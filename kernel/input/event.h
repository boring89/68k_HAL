/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_INPUT_EVENT_H
#define KERNEL_INPUT_EVENT_H

#include <stdint.h>
#include <stdbool.h>

#include "kernel/keyboard/keycode.h"

/*
 * Event types
 */
typedef enum
{
    INPUT_EVENT_NONE = 0,

    INPUT_EVENT_KEYBOARD,

    INPUT_EVENT_MOUSE,

    INPUT_EVENT_SERIAL

} input_event_type_t;

/*
 * Keyboard event data
 */
typedef struct 
{
    keycode_t key;
    bool pressed;
} keyboard_event_t;

/*
 * Mouse event data: movement since the last event (+x right, +y down) and
 * the buttons held now (bit 0 left, bit 1 right, bit 2 middle).
 */
typedef struct
{
    int16_t dx;
    int16_t dy;
    uint8_t buttons;
} mouse_event_t;

/*
 * Generic input event
 */
typedef struct 
{
    input_event_type_t type;

    union 
    {
        keyboard_event_t keyboard;
        mouse_event_t mouse;
        
    } data;
    
} input_event_t;

/*
 * Event constructors
 */
input_event_t input_event_keyboard(
    keycode_t key,
    bool pressed
);

input_event_t input_event_mouse(
    int dx,
    int dy,
    uint8_t buttons
);

#endif