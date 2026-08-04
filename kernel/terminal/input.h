/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_TERMINAL_INPUT_H
#define KERNEL_TERMINAL_INPUT_H

typedef void (*terminal_submit_callback_t)(
    const char *
);

void terminal_input_init(void);

void terminal_input_set_callback(terminal_submit_callback_t callback);

void terminal_input_submit(void);

void terminal_input_char(char c);

const char *terminal_input_get(void);

void terminal_input_clear(void);

#endif