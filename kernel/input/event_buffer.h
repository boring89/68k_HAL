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


#endif