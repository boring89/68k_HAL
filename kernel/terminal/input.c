/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "input.h"
#include "terminal.h"

#define TERMINAL_INPUT_SIZE 128

static char input_buffer[TERMINAL_INPUT_SIZE];

static int input_length = 0;

static terminal_submit_callback_t submit_callback = 0;

void terminal_input_init(void)
{
    input_length = 0;

    for (int i = 0; i < TERMINAL_INPUT_SIZE; i++)
    {
        input_buffer[i] = 0;
    }
}

void terminal_input_set_callback(
    terminal_submit_callback_t callback)
{
    submit_callback = callback;
}

void terminal_input_submit(void)
{
    if (submit_callback)
    {
        submit_callback(input_buffer);
    }

    terminal_input_clear();
}

void terminal_input_char(char c)
{
    if (c == '\b')
    {
        if (input_length > 0)
        {
            input_length--;
            input_buffer[input_length] = 0;

            terminal_backspace();
        }

        return;
    }
    if (c == '\n')
    {
        terminal_newline();
        terminal_input_submit();
        return;
    }

    if (c == '\t')
    {
        for (int i = 0; i < 4; i++)
        {
            terminal_input_char(' ');
        }
        return;
    }

    if (input_length >= TERMINAL_INPUT_SIZE - 1)
        return;

    input_buffer[input_length++] = c;
    input_buffer[input_length] = 0;

    terminal_put_char(c);
}

const char *terminal_input_get(void)
{
    return input_buffer;
}

void terminal_input_clear(void)
{
    input_length = 0;
    input_buffer[0] = 0;
}