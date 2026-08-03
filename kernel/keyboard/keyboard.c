/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "keyboard.h"

#include "../input/event.h"
#include "../input/ring_buffer.h"
#include "../../hal/io/io.h"


static input_event_t keyboard_events[128];

static ring_buffer_t keyboard_buffer;


void keyboard_init(void)
{
    ring_buffer_init(
        &keyboard_buffer,
        keyboard_events,
        sizeof(input_event_t),
        128
    );
}


void keyboard_handler(void)
{
    uint8_t scancode = inb(0x60);


    input_event_t event =
        input_event_keyboard(
            scancode & 0x7F,
            !(scancode & 0x80)
        );

    ring_buffer_push(
        &keyboard_buffer,
        &event
    );
}


bool keyboard_has_key(void)
{
    return ring_buffer_has_data(
        &keyboard_buffer
    );
}


int keyboard_read(input_event_t *event)
{
    return ring_buffer_pop(
        &keyboard_buffer,
        event
    );
}