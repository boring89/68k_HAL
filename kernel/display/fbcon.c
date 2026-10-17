/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "fbcon.h"

#include "kernel/memory/heap.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct
{
    char c;
    uint8_t fg;
    uint8_t bg;
} cell_t;

static text_display_t display; /* width and height are 0 until fbcon_init */
static hal_framebuffer_t fb;
static cell_t *cells;
static uint32_t origin_x; /* pixel position of cell (0, 0) */
static uint32_t origin_y;
static int cursor_x = -1;
static int cursor_y = -1;
static uint32_t scale = 1; /* every glyph pixel is scale x scale screen pixels */

/* The standard VGA palette (0x00RRGGBB), indexed by color_t. */
static const uint32_t palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF};

static uint32_t *pixel(uint32_t px, uint32_t py)
{
    return (uint32_t *)(fb.base + (size_t)py * fb.pitch + (size_t)px * 4);
}

static void draw_cell(int x, int y)
{
    const cell_t *cell = &cells[y * display.width + x];
    uint32_t fg = palette[cell->fg & 15];
    uint32_t bg = palette[cell->bg & 15];
    const uint8_t *glyph = (cell->c >= 0) ? fbcon_font[(int)cell->c] : fbcon_font[0];
    bool underline = x == cursor_x && y == cursor_y;

    uint32_t cell_width = FBCON_CELL_WIDTH * scale;
    uint32_t cell_height = FBCON_CELL_HEIGHT * scale;

    for (uint32_t row = 0; row < cell_height; row++)
    {
        uint32_t *out = pixel(origin_x + (uint32_t)x * cell_width, origin_y + (uint32_t)y * cell_height + row);
        uint8_t bits = glyph[row / (2 * scale)];

        /* The cursor: the bottom two glyph pixel rows in the foreground colour. */
        bool full = underline && row >= cell_height - 2 * scale;

        for (uint32_t col = 0; col < cell_width; col++)
            out[col] = (full || (bits & (1u << (col / scale)))) ? fg : bg;
    }
}

static bool inside(int x, int y)
{
    return cells != NULL && x >= 0 && x < display.width && y >= 0 && y < display.height;
}

static void put_cell(int x, int y, char c, color_t fg, color_t bg)
{
    if (!inside(x, y))
        return;

    cell_t *cell = &cells[y * display.width + x];

    cell->c = c;
    cell->fg = (uint8_t)fg;
    cell->bg = (uint8_t)bg;
    draw_cell(x, y);
}

static void copy_cell(int src_x, int src_y, int dst_x, int dst_y)
{
    if (!inside(src_x, src_y) || !inside(dst_x, dst_y))
        return;

    cells[dst_y * display.width + dst_x] = cells[src_y * display.width + src_x];
    draw_cell(dst_x, dst_y);
}

static void set_cursor(int x, int y)
{
    int old_x = cursor_x;
    int old_y = cursor_y;

    cursor_x = x;
    cursor_y = y;

    if (inside(old_x, old_y))
        draw_cell(old_x, old_y);

    if (inside(x, y))
        draw_cell(x, y);
}

static void clear(void)
{
    if (cells == NULL)
        return;

    for (int i = 0; i < display.width * display.height; i++)
    {
        cells[i].c = ' ';
        cells[i].fg = 7; /* light grey */
        cells[i].bg = 0; /* black */
    }

    fbcon_redraw();
}

void fbcon_redraw(void)
{
    if (cells == NULL)
        return;

    /* Everything outside the grid is black too. */
    for (uint32_t y = 0; y < fb.height; y++)
    {
        uint32_t *row = pixel(0, y);

        for (uint32_t x = 0; x < fb.width; x++)
            row[x] = 0;
    }

    for (int y = 0; y < display.height; y++)
    {
        for (int x = 0; x < display.width; x++)
            draw_cell(x, y);
    }
}

uint32_t fbcon_scale_for(uint32_t width, uint32_t height)
{
    /* About 960 pixels of width per step, as long as the height keeps up (1080p: 2, 4K: 4). */
    uint32_t wanted = width / 960;
    uint32_t possible = height / 540;

    if (wanted > possible)
        wanted = possible;

    return wanted < 1 ? 1 : wanted > 4 ? 4 : wanted;
}

int fbcon_init(const hal_framebuffer_t *framebuffer, uint32_t top_margin)
{
    if (framebuffer == NULL || framebuffer->base == NULL || framebuffer->bpp != 32 ||
        top_margin >= framebuffer->height)
        return -1;

    uint32_t new_scale = fbcon_scale_for(framebuffer->width, framebuffer->height);
    uint32_t cell_width = FBCON_CELL_WIDTH * new_scale;
    uint32_t cell_height = FBCON_CELL_HEIGHT * new_scale;
    int columns = (int)(framebuffer->width / cell_width);
    int rows = (int)((framebuffer->height - top_margin) / cell_height);

    if (columns < 1 || rows < 1)
        return -1;

    cell_t *grid = kmalloc((size_t)columns * (size_t)rows * sizeof(cell_t));

    if (grid == NULL)
        return -1;

    if (cells != NULL)
        kfree(cells);

    cells = grid;
    fb = *framebuffer;
    display.width = columns;
    display.height = rows;
    display.put_cell = put_cell;
    display.copy_cell = copy_cell;
    display.set_cursor = set_cursor;
    display.clear = clear;
    scale = new_scale;
    origin_x = (framebuffer->width - (uint32_t)columns * cell_width) / 2;
    origin_y = top_margin + (framebuffer->height - top_margin - (uint32_t)rows * cell_height) / 2;
    cursor_x = -1;
    cursor_y = -1;

    for (int i = 0; i < columns * rows; i++)
    {
        cells[i].c = ' ';
        cells[i].fg = 7;
        cells[i].bg = 0;
    }

    return 0;
}

const text_display_t *fbcon_display(void)
{
    return cells != NULL ? &display : NULL;
}
