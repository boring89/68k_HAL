/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "history.h"

#include "sink.h"

static char ring[CONSOLE_HISTORY_SIZE];
static size_t head;  /* where the next byte goes */
static size_t count; /* bytes held */

static void add_char(char c)
{
    ring[head] = c;
    head = (head + 1) % CONSOLE_HISTORY_SIZE;

    if (count < CONSOLE_HISTORY_SIZE)
        count++;
}

void console_history_add(const char *text)
{
    for (; *text != '\0'; text++)
        add_char(*text);
}

static void sink_write(const char *str)
{
    console_history_add(str);
}

static const console_sink_t sink = {sink_write, add_char};

int console_history_start(void)
{
    return console_register_sink(&sink);
}

size_t console_history_size(void)
{
    return count;
}

void console_history_clear(void)
{
    head = 0;
    count = 0;
}

static char at(size_t index) /* 0 = the oldest byte held */
{
    return ring[(head + CONSOLE_HISTORY_SIZE - count + index) % CONSOLE_HISTORY_SIZE];
}

size_t console_history_last_lines(char *buffer, size_t size, unsigned int lines)
{
    if (size == 0)
        return 0;

    size_t start = 0;

    if (lines != 0)
    {
        /* Walk back from the end over `lines` line ends (a trailing newline does not start a line). */
        size_t seen = 0;
        size_t i = count;

        if (i > 0 && at(i - 1) == '\n')
            i--;

        while (i > 0)
        {
            if (at(i - 1) == '\n' && ++seen == lines)
                break;

            i--;
        }

        start = i;
    }

    size_t length = count - start;

    if (length > size - 1)
    {
        start += length - (size - 1);
        length = size - 1;
    }

    for (size_t i = 0; i < length; i++)
        buffer[i] = at(start + i);

    buffer[length] = '\0';

    return length;
}
