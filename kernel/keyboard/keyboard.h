/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_KEYBOARD_KEYBOARD_H
#define KERNEL_KEYBOARD_KEYBOARD_H

#include "../input/event.h"

void keyboard_init(void);

void keyboard_handler(void);

bool keyboard_has_event(void);

int keyboard_read(input_event_t *event);


#endif