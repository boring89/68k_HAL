/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "vga.h"
#include "hal/mmu.h"
#include "arch/x86/io/io.h"

#define VGA_MEM 0xB8000

/* The text buffer's physical address; a 64-bit kernel reaches it through the direct map. */
static unsigned short *video =
    (unsigned short *)HAL_PHYS_TO_VIRT(VGA_MEM);

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

/*
 * VGA attribute colour for each color_t: the default text mode palette
 * (OSDev Wiki "Printing To Screen"). Bits 3:0 are the foreground,
 * bits 7:4 the background.
 */
static const uint8_t vga_palette[16] = {
    [COLOR_BLACK] = 0x0,
    [COLOR_BLUE] = 0x1,
    [COLOR_GREEN] = 0x2,
    [COLOR_CYAN] = 0x3,
    [COLOR_RED] = 0x4,
    [COLOR_MAGENTA] = 0x5,
    [COLOR_BROWN] = 0x6,
    [COLOR_LIGHT_GREY] = 0x7,
    [COLOR_DARK_GREY] = 0x8,
    [COLOR_LIGHT_BLUE] = 0x9,
    [COLOR_LIGHT_GREEN] = 0xA,
    [COLOR_LIGHT_CYAN] = 0xB,
    [COLOR_LIGHT_RED] = 0xC,
    [COLOR_LIGHT_MAGENTA] = 0xD,
    [COLOR_YELLOW] = 0xE,
    [COLOR_WHITE] = 0xF,
};

static uint8_t vga_attribute(color_t fg, color_t bg)
{
    return (uint8_t)((vga_palette[bg & 0xF] << 4) | vga_palette[fg & 0xF]);
}

static void vga_display_put_cell(int x, int y, char c, color_t fg, color_t bg)
{
    vga_put_cell(x, y, c, vga_attribute(fg, bg));
}

const text_display_t vga_text_display = {
    .width = VGA_WIDTH,
    .height = VGA_HEIGHT,
    .put_cell = vga_display_put_cell,
    .copy_cell = vga_copy_cell,
    .set_cursor = vga_set_cursor,
    .clear = vga_clear,
};
