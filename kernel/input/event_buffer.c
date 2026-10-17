/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "event_buffer.h"

#include "kernel/lib/ring_buffer.h"


#define INPUT_EVENT_BUFFER_SIZE 128


static input_event_t storage[INPUT_EVENT_BUFFER_SIZE];

static ring_buffer_t buffer;

static void (*push_hook)(const input_event_t *event);

void input_event_set_hook(void (*hook)(const input_event_t *event))
{
    push_hook = hook;
}


void input_event_buffer_init(void)
{
    ring_buffer_init(
        &buffer,
        storage,
        sizeof(input_event_t),
        INPUT_EVENT_BUFFER_SIZE
    );
}


int input_event_push(
    input_event_t *event
)
{
    if (push_hook != 0)
        push_hook(event);

    return ring_buffer_push(
        &buffer,
        event
    );
}


int input_event_pop(
    input_event_t *event
)
{
    return ring_buffer_pop(
        &buffer,
        event
    );
}


bool input_event_available(void)
{
    return ring_buffer_has_data(
        &buffer
    );
}