/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sink.h"

#include <stddef.h>

static const console_sink_t *sinks[CONSOLE_MAX_SINKS];
static int sink_count;

int console_register_sink(const console_sink_t *sink)
{
    if (sink == NULL || sink->write == NULL || sink->write_char == NULL)
        return -1;

    if (sink_count == CONSOLE_MAX_SINKS)
        return -1;

    for (int i = 0; i < sink_count; i++)
    {
        if (sinks[i] == sink)
            return -1;
    }

    sinks[sink_count++] = sink;

    return 0;
}

void console_sinks_reset(void)
{
    sink_count = 0;
}

void console_sinks_write(const char *str)
{
    for (int i = 0; i < sink_count; i++)
        sinks[i]->write(str);
}

void console_sinks_write_char(char c)
{
    for (int i = 0; i < sink_count; i++)
        sinks[i]->write_char(c);
}
