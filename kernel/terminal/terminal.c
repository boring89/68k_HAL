/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "terminal.h"

#include "kernel/display/text_display.h"

#include <stddef.h>

/* NULL on a machine without a display: then every call does nothing. */
static const text_display_t *display;

static int cursor_x = 0;
static int cursor_y = 0;

static color_t terminal_fg = COLOR_LIGHT_GREY;
static color_t terminal_bg = COLOR_BLACK;

static void terminal_update_cursor(void)
{
    display->set_cursor(cursor_x, cursor_y);
}

void terminal_init(void)
{
    display = text_display_get();

    cursor_x = 0;
    cursor_y = 0;
    terminal_fg = COLOR_LIGHT_GREY;
    terminal_bg = COLOR_BLACK;

    if (display == NULL)
        return;

    display->clear();

    terminal_update_cursor();
}

void terminal_backspace(void)
{
    if (display == NULL || cursor_x == 0)
    {
        return;
    }

    cursor_x--;

    display->put_cell(
        cursor_x,
        cursor_y,
        ' ',
        terminal_fg,
        terminal_bg);

    terminal_update_cursor();
}

void terminal_tab(void)
{
    // for (int i = 0; i < 4; i++)
    // {
    //     terminal_put_char(' ');
    // }
}

void terminal_clear(void)
{
    terminal_fg = COLOR_LIGHT_GREY;
    terminal_bg = COLOR_BLACK;

    cursor_x = 0;
    cursor_y = 0;

    if (display == NULL)
        return;

    display->clear();

    terminal_update_cursor();
}

void terminal_scroll(void)
{
    if (display == NULL)
        return;

    for (int y = 1; y < display->height; y++)
    {
        for (int x = 0; x < display->width; x++)
        {
            display->copy_cell(x, y, x, y - 1);
        }
    }

    for (int x = 0; x < display->width; x++)
    {
        display->put_cell(
            x,
            display->height - 1,
            ' ',
            terminal_fg,
            terminal_bg);
    }

    cursor_y = display->height - 1;
    cursor_x = 0;

    terminal_update_cursor();
}

void terminal_newline(void)
{
    if (display == NULL)
        return;

    cursor_x = 0;

    cursor_y++;

    if (cursor_y >= display->height)
    {
        terminal_scroll();
        return;
    }

    terminal_update_cursor();
}

void terminal_set_color(color_t foreground, color_t background)
{
    terminal_fg = foreground;
    terminal_bg = background;
}

void terminal_put_char(char c)
{
    if (display == NULL)
        return;

    switch (c)
    {
    case '\n':
        terminal_newline();
        return;

    case '\b':
        terminal_backspace();
        return;

    case '\t':
        terminal_tab();
        return;

    default:
        break;
    }

    display->put_cell(
        cursor_x,
        cursor_y,
        c,
        terminal_fg,
        terminal_bg);

    cursor_x++;

    if (cursor_x >= display->width)
    {
        terminal_newline();
        return;
    }

    terminal_update_cursor();
}

void terminal_write(const char *str)
{
    if (!str)
        str = "NULL";

    while (*str)
    {
        terminal_put_char(*str);
        str++;
    }
}

void terminal_write_line(const char *str)
{
    terminal_write(str);
    terminal_newline();
}

void terminal_write_color(const char *str, color_t fg, color_t bg)
{
    color_t old_fg = terminal_fg;
    color_t old_bg = terminal_bg;

    terminal_set_color(fg, bg);
    terminal_write(str);

    terminal_fg = old_fg;
    terminal_bg = old_bg;
}

void terminal_write_color_line(
    const char *str,
    color_t fg,
    color_t bg)
{
    terminal_write_color(str, fg, bg);
    terminal_newline();
}
