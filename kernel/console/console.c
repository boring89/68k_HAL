/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "../terminal/terminal.h"
#include "../../arch/x86/serial/serial.h"
#include "console.h"

void console_init(void)
{
    terminal_init();
    serial_init();
}

void console_write(const char *str)
{
    terminal_write(str);
    serial_write(str);
}

void console_write_char(char c)
{
    terminal_put_char(c);
    serial_write_char(c);
}

void console_write_line(const char *str)
{
    terminal_write_line(str);
    serial_write_line(str);
}

void console_write_color(const char *str, color_t fg, color_t bg)
{
    terminal_write_color(str, fg, bg);
    serial_write(str);
}

void console_write_color_line(const char *str, color_t fg, color_t bg)
{
    terminal_write_color_line(str, fg, bg);
    serial_write_line(str);
}

void console_clear(void)
{
    terminal_clear();
}