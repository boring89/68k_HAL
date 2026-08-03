/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "vga.h"
#include "../io/io.h"

#define VGA_MEM 0xB8000

static unsigned short *video = (unsigned short *)VGA_MEM;
static unsigned char color = VGA_DEFAULT_COLOR;

static int cursor_x = 0;
static int cursor_y = 0;

static void vga_update_cursor(void)
{
    uint16_t pos = cursor_y * VGA_WIDTH + cursor_x;

    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));

    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static inline int vga_index(void)
{
    return cursor_y * VGA_WIDTH + cursor_x;
}

void vga_scroll(void)
{
    for (int y = 1; y < VGA_HEIGHT; y++)
    {
        for (int x = 0; x < VGA_WIDTH; x++)
        {
            video[(y - 1) * VGA_WIDTH + x] =
                video[y * VGA_WIDTH + x];
        }
    }

    unsigned short blank =
        ((unsigned short)color << 8) | ' ';

    for (int x = 0; x < VGA_WIDTH; x++)
    {
        video[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = blank;
    }

    cursor_y = VGA_HEIGHT - 1;
    cursor_x = 0;
    vga_update_cursor();
}

void vga_set_color(vga_color_t foreground, vga_color_t background)
{
    color =
        (background << 4) | foreground;
}

void vga_set_cursor(int x, int y)
{
    if (x < VGA_WIDTH &&
        x >= 0 &&
        y < VGA_HEIGHT &&
        y >= 0)
    {
        cursor_x = x;
        cursor_y = y;

        vga_update_cursor();
    }
}

void vga_newline(void)
{
    cursor_x = 0;

    if (cursor_y == VGA_HEIGHT - 1)
    {
        vga_scroll();
        return;
    }

    cursor_y++;
    vga_update_cursor();
}
void vga_clear(void)
{
    unsigned short blank =
        ((unsigned short)color << 8) | ' ';

    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
    {
        video[i] = blank;
    }

    cursor_x = 0;
    cursor_y = 0;
    vga_update_cursor();
}

void vga_init(void)
{
    color = VGA_DEFAULT_COLOR;
    vga_clear();
}

void vga_put_char(char c)
{
    if (c == '\n')
    {
        vga_newline();
        return;
    }

    video[vga_index()] =
        ((unsigned short)color << 8) | c;

    cursor_x++;

    if (cursor_x >= VGA_WIDTH)
    {
        vga_newline();
    }
    else
    {
        vga_update_cursor();
    }
}

void vga_write(const char *str)
{
    if (!str)
        str = "NULL";

    while (*str)
    {
        vga_put_char(*str);
        str++;
    }
    vga_update_cursor();
}

void vga_write_line(const char *str)
{
    vga_write(str);
    vga_newline();
}

void vga_write_color(const char *str, vga_color_t fg, vga_color_t bg)
{
    unsigned char old_color = color;

    vga_set_color(fg, bg);
    vga_write(str);

    color = old_color;
}

void vga_write_color_line(
    const char *str,
    vga_color_t fg,
    vga_color_t bg)
{
    vga_write_color(str, fg, bg);
    vga_newline();
}