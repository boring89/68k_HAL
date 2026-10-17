/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_INPUT_EVENT_BUFFER_H
#define KERNEL_INPUT_EVENT_BUFFER_H

#include "event.h"

#include <stdbool.h>


void input_event_buffer_init(void);


int input_event_push(
    input_event_t *event
);


int input_event_pop(
    input_event_t *event
);


bool input_event_available(void);


/*
 * Call hook for every event pushed, in the context of the pusher (an interrupt handler: be
 * short, no waiting). The event is still queued as usual. NULL removes the hook.
 */
void input_event_set_hook(void (*hook)(const input_event_t *event));


#endif