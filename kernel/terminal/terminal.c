/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "terminal.h"

#include "kernel/display/text_display.h"

#include <stdbool.h>
#include <stddef.h>

/* NULL on a machine without a display: then every call does nothing. */
static const text_display_t *display;
static void (*mirror)(char c);

void terminal_set_mirror(void (*function)(char c))
{
    mirror = function;
}

static bool mirror_suspended;

bool terminal_set_mirror_suspended(bool suspended)
{
    bool before = mirror_suspended;

    mirror_suspended = suspended;

    return before;
}

static void mirror_char(char c)
{
    if (mirror != NULL && !mirror_suspended)
        mirror(c);
}

static int cursor_x = 0;
static int cursor_y = 0;

/*
 * The last column was written: the cursor stays on it and the line breaks only when
 * the next character comes (as on xterm), so a line that fills the width exactly does
 * not get an empty line from its own newline.
 */
static bool wrap_pending;

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
    wrap_pending = false;
    terminal_fg = COLOR_LIGHT_GREY;
    terminal_bg = COLOR_BLACK;

    if (display == NULL)
        return;

    display->clear();

    terminal_update_cursor();
}

void terminal_backspace(void)
{
    mirror_char('\b');

    if (display == NULL)
        return;

    /* After a full line the cursor is on the last character: erase that one. */
    if (wrap_pending)
        wrap_pending = false;
    else if (cursor_x == 0)
        return;
    else
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

void terminal_mirror_raw(char c)
{
    mirror_char(c);
}

int terminal_width(void)
{
    return display != NULL ? display->width : 80;
}

int terminal_height(void)
{
    return display != NULL ? display->height : 25;
}

bool terminal_cursor_pending(void)
{
    return wrap_pending;
}

bool terminal_has_display(void)
{
    return display != NULL;
}

void terminal_get_cursor(int *x, int *y)
{
    *x = cursor_x;
    *y = cursor_y;
}

void terminal_move_cursor(int x, int y)
{
    if (display == NULL)
        return;

    if (x < 0)
        x = 0;

    if (x >= display->width)
        x = display->width - 1;

    if (y < 0)
        y = 0;

    if (y >= display->height)
        y = display->height - 1;

    cursor_x = x;
    cursor_y = y;
    wrap_pending = false;

    terminal_update_cursor();
}

void terminal_erase(int from_x, int from_y, int to_x, int to_y)
{
    if (display == NULL)
        return;

    /* Cells from (from_x, from_y) to (to_x, to_y), both included, in reading order. */
    for (int y = from_y; y <= to_y; y++)
    {
        int first = y == from_y ? from_x : 0;
        int last = y == to_y ? to_x : display->width - 1;

        for (int x = first; x <= last; x++)
            display->put_cell(x, y, ' ', terminal_fg, terminal_bg);
    }
}

void terminal_clear(void)
{
    terminal_fg = COLOR_LIGHT_GREY;
    terminal_bg = COLOR_BLACK;

    cursor_x = 0;
    cursor_y = 0;
    wrap_pending = false;

    if (display == NULL)
        return;

    display->clear();

    terminal_update_cursor();
}

static void terminal_scroll(void)
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

static void newline_no_mirror(void)
{
    if (display == NULL)
        return;

    cursor_x = 0;
    wrap_pending = false;

    cursor_y++;

    if (cursor_y >= display->height)
    {
        terminal_scroll();
        return;
    }

    terminal_update_cursor();
}

void terminal_newline(void)
{
    mirror_char('\n');

    newline_no_mirror();
}

void terminal_set_color(color_t foreground, color_t background)
{
    terminal_fg = foreground;
    terminal_bg = background;
}

void terminal_put_char(char c)
{
    /* Without a display only the mirror sees the text. */
    if (display == NULL)
    {
        if (c != '\t')
            mirror_char(c);

        return;
    }

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

    mirror_char(c);

    if (wrap_pending)
        newline_no_mirror();

    display->put_cell(
        cursor_x,
        cursor_y,
        c,
        terminal_fg,
        terminal_bg);

    if (cursor_x + 1 >= display->width)
        wrap_pending = true;
    else
        cursor_x++;

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
