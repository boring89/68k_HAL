/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "terminal.h"

#include "../../arch/x86/vga/vga.h"

static int cursor_x = 0;
static int cursor_y = 0;

static unsigned char terminal_color = (COLOR_BLACK << 4) | COLOR_LIGHT_GREY;

void terminal_init(void)
{
    cursor_x = 0;
    cursor_y = 0;
    terminal_color = (COLOR_BLACK << 4) | COLOR_LIGHT_GREY;

    vga_clear();

    vga_set_cursor(
        cursor_x,
        cursor_y);
}

void terminal_backspace(void)
{
    if (cursor_x == 0)
    {
        return;
    }

    cursor_x--;

    vga_put_cell(
        cursor_x,
        cursor_y,
        ' ',
        terminal_color);

    vga_set_cursor(
        cursor_x,
        cursor_y);
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
    terminal_color = (COLOR_BLACK << 4) | COLOR_LIGHT_GREY;

    cursor_x = 0;
    cursor_y = 0;

    vga_clear();

    vga_set_cursor(
        cursor_x,
        cursor_y);
}

void terminal_scroll(void)
{
    for (int y = 1; y < VGA_HEIGHT; y++)
    {
        for (int x = 0; x < VGA_WIDTH; x++)
        {
            vga_copy_cell(x, y, x, y - 1);
        }
    }

    for (int x = 0; x < VGA_WIDTH; x++)
    {
        vga_put_cell(
            x,
            VGA_HEIGHT - 1,
            ' ',
            terminal_color);
    }

    cursor_y = VGA_HEIGHT - 1;
    cursor_x = 0;

    vga_set_cursor(cursor_x, cursor_y);
}

void terminal_newline(void)
{
    cursor_x = 0;

    cursor_y++;

    if (cursor_y >= VGA_HEIGHT)
    {
        terminal_scroll();
        return;
    }

    vga_set_cursor(
        cursor_x,
        cursor_y);
}

void terminal_set_color(color_t foreground, color_t background)
{
    terminal_color = (background << 4) | foreground;
}

void terminal_put_char(char c)
{
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

    vga_put_cell(
        cursor_x,
        cursor_y,
        c,
        terminal_color);

    cursor_x++;

    if (cursor_x >= VGA_WIDTH)
    {
        terminal_newline();
        return;
    }

    vga_set_cursor(
        cursor_x,
        cursor_y);
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
    unsigned char old_color = terminal_color;

    terminal_set_color(fg, bg);
    terminal_write(str);

    terminal_color = old_color;
}

void terminal_write_color_line(
    const char *str,
    color_t fg,
    color_t bg)
{
    terminal_write_color(str, fg, bg);
    terminal_newline();
}