/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "console.h"
#include "sink.h"

#include "kernel/terminal/terminal.h"

void console_init(void)
{
    terminal_init();
}

void console_write(const char *str)
{
    terminal_write(str);
    console_sinks_write(str);
}

void console_write_char(char c)
{
    terminal_put_char(c);
    console_sinks_write_char(c);
}

void console_write_line(const char *str)
{
    terminal_write_line(str);
    console_sinks_write(str);
    console_sinks_write("\n");
}

void console_write_color(const char *str, color_t fg, color_t bg)
{
    terminal_write_color(str, fg, bg);
    console_sinks_write(str);
}

void console_write_color_line(const char *str, color_t fg, color_t bg)
{
    terminal_write_color_line(str, fg, bg);
    console_sinks_write(str);
    console_sinks_write("\n");
}

void console_clear(void)
{
    terminal_clear();
}