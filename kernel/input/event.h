/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_INPUT_EVENT_H
#define KERNEL_INPUT_EVENT_H

#include <stdint.h>
#include <stdbool.h>

#include "../keyboard/keycode.h"

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
 * Generic input event
 */
typedef struct 
{
    input_event_type_t type;

    union 
    {
        keyboard_event_t keyboard;
        
    } data;
    
} input_event_t;

/*
 * Event constructors
 */
input_event_t input_event_keyboard(
    keycode_t key,
    bool pressed
);

#endif