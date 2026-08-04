/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "vga.h"
#include "../io/io.h"

#define VGA_MEM 0xB8000

static unsigned short *video =
    (unsigned short *)VGA_MEM;

static void vga_update_cursor(int x, int y)
{
    uint16_t pos = y * VGA_WIDTH + x;

    outb(0x3D4, 0x0F);
    outb(0x3D5, pos & 0xff);

    outb(0x3D4, 0x0E);
    outb(0x3D5, (pos >> 8) & 0xff);
}

void vga_put_cell(
    int x,
    int y,
    char c,
    unsigned char color)
{
    if (x < 0 || x >= VGA_WIDTH)
        return;

    if (y < 0 || y >= VGA_HEIGHT)
        return;

    video[y * VGA_WIDTH + x] =
        ((unsigned short)color << 8) | c;
}

void vga_get_cell(
    int x,
    int y,
    char *c,
    unsigned char *color)
{
    uint16_t cell = video[y * VGA_WIDTH + x];

    *c = (char)(cell & 0xFF);
    *color = (unsigned char)(cell >> 8);
}

void vga_copy_cell(
    int sx,
    int sy,
    int dx,
    int dy)
{
    video[dy * VGA_WIDTH + dx] = video[sy * VGA_WIDTH + sx];
}

void vga_set_cursor(int x, int y)
{
    if (x < 0 || x >= VGA_WIDTH)
        return;

    if (y < 0 || y >= VGA_HEIGHT)
        return;

    vga_update_cursor(x, y);
}

void vga_clear(void)
{
    unsigned short blank =
        ((unsigned short)0x07 << 8) | ' ';

    for (int i = 0;
         i < VGA_WIDTH * VGA_HEIGHT;
         i++)
    {
        video[i] = blank;
    }
}

void vga_init(void)
{
    vga_clear();
}