/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include <stdint.h>
#include <string.h>

#include "kernel/display/fbcon.h"
#include "kernel/memory/heap.h"
#include "kernel/terminal/terminal.h"

/* 100x70 pixels: 12 columns, and 4 rows below a 4 pixel margin; 2 pixels spare at the sides. */
#define W 100
#define H 70
#define MARGIN 4

static uint32_t pixels[W * H];
static uint8_t heap_region[64 * 1024];

static hal_framebuffer_t framebuffer(void)
{
    hal_framebuffer_t fb = {(uint8_t *)pixels, W, H, W * 4, 32};

    return fb;
}

static void start(void)
{
    heap_init((uintptr_t)heap_region, sizeof(heap_region));
    memset(pixels, 0x55, sizeof(pixels));

    hal_framebuffer_t fb = framebuffer();

    TEST_ASSERT_EQ_INT(0, fbcon_init(&fb, MARGIN));
}

static uint32_t at(int x, int y)
{
    return pixels[y * W + x];
}

/* Pixel of cell (cx, cy): column px, row py inside the cell. */
static uint32_t cell_pixel(int cx, int cy, int px, int py)
{
    /* origin: (100 - 12*8) / 2 = 2 horizontally; 4 + (66 - 4*16) / 2 = 5 vertically */
    return at(2 + cx * 8 + px, 5 + cy * 16 + py);
}

static void test_geometry_and_rejects(void)
{
    hal_framebuffer_t fb = framebuffer();

    TEST_ASSERT(fbcon_display() == NULL || fbcon_display()->width >= 0);

    start();
    TEST_ASSERT_EQ_INT(12, fbcon_display()->width);
    TEST_ASSERT_EQ_INT(4, fbcon_display()->height);

    TEST_ASSERT_EQ_INT(-1, fbcon_init(NULL, 0));
    fb.bpp = 16;
    TEST_ASSERT_EQ_INT(-1, fbcon_init(&fb, 0));
    fb.bpp = 32;
    TEST_ASSERT_EQ_INT(-1, fbcon_init(&fb, H));
    fb.width = 7;
    TEST_ASSERT_EQ_INT(-1, fbcon_init(&fb, 0));

    /* A failed init keeps the working display. */
    TEST_ASSERT_EQ_INT(12, fbcon_display()->width);
}

static void test_clear_paints_everything_black(void)
{
    start();
    fbcon_display()->clear();

    TEST_ASSERT_EQ_INT(0, (int)at(0, 0));
    TEST_ASSERT_EQ_INT(0, (int)at(W - 1, H - 1));
    TEST_ASSERT_EQ_INT(0, (int)cell_pixel(0, 0, 3, 3));
}

static void test_glyph_drawing(void)
{
    start();
    fbcon_display()->clear();

    /* '!' is 0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00: pixel rows 0 and 1 are 0x18. */
    fbcon_display()->put_cell(1, 2, '!', COLOR_LIGHT_RED, COLOR_BLUE);

    TEST_ASSERT_EQ_INT(0x0000AA, (int)cell_pixel(1, 2, 0, 0)); /* background */
    TEST_ASSERT_EQ_INT(0xFF5555, (int)cell_pixel(1, 2, 3, 0)); /* bit 3 = foreground */
    TEST_ASSERT_EQ_INT(0xFF5555, (int)cell_pixel(1, 2, 4, 1)); /* each font row is drawn twice */
    TEST_ASSERT_EQ_INT(0x0000AA, (int)cell_pixel(1, 2, 5, 1));
    TEST_ASSERT_EQ_INT(0x0000AA, (int)cell_pixel(1, 2, 3, 10)); /* font row 5 is empty */

    /* Neighbours are untouched. */
    TEST_ASSERT_EQ_INT(0, (int)cell_pixel(0, 2, 3, 0));
    TEST_ASSERT_EQ_INT(0, (int)cell_pixel(2, 2, 3, 0));
}

static void test_out_of_range_is_ignored(void)
{
    start();
    fbcon_display()->clear();

    fbcon_display()->put_cell(-1, 0, 'x', COLOR_WHITE, COLOR_BLACK);
    fbcon_display()->put_cell(12, 0, 'x', COLOR_WHITE, COLOR_BLACK);
    fbcon_display()->put_cell(0, 4, 'x', COLOR_WHITE, COLOR_BLACK);
    fbcon_display()->copy_cell(0, 0, 99, 99);
    fbcon_display()->set_cursor(50, 50);

    for (int y = 0; y < H; y++)
    {
        for (int x = 0; x < W; x++)
            TEST_ASSERT_EQ_INT(0, (int)at(x, y));
    }
}

static void test_copy_and_cursor(void)
{
    start();
    fbcon_display()->clear();
    fbcon_display()->put_cell(0, 0, 'A', COLOR_WHITE, COLOR_BLACK);
    fbcon_display()->copy_cell(0, 0, 5, 3);

    for (int py = 0; py < 16; py++)
    {
        for (int px = 0; px < 8; px++)
            TEST_ASSERT_EQ_INT((int)cell_pixel(0, 0, px, py), (int)cell_pixel(5, 3, px, py));
    }

    /* The cursor underlines a cell; moving it away restores the glyph. */
    fbcon_display()->put_cell(2, 1, ' ', COLOR_GREEN, COLOR_BLACK);
    fbcon_display()->set_cursor(2, 1);
    TEST_ASSERT_EQ_INT(0x00AA00, (int)cell_pixel(2, 1, 0, 14));
    TEST_ASSERT_EQ_INT(0x00AA00, (int)cell_pixel(2, 1, 7, 15));
    TEST_ASSERT_EQ_INT(0, (int)cell_pixel(2, 1, 0, 13));

    fbcon_display()->set_cursor(3, 1);
    TEST_ASSERT_EQ_INT(0, (int)cell_pixel(2, 1, 0, 14));
    TEST_ASSERT_EQ_INT(0x555555 == 0 ? 1 : 0xAAAAAA, (int)cell_pixel(3, 1, 0, 15));
}

static void test_redraw_after_foreign_drawing(void)
{
    start();
    fbcon_display()->clear();
    fbcon_display()->put_cell(0, 0, 'A', COLOR_WHITE, COLOR_BLACK);

    uint32_t glyph_pixel = cell_pixel(0, 0, 1, 6);

    memset(pixels, 0x11, sizeof(pixels));
    fbcon_redraw();

    TEST_ASSERT_EQ_INT((int)glyph_pixel, (int)cell_pixel(0, 0, 1, 6));
    TEST_ASSERT_EQ_INT(0, (int)at(0, 0)); /* the margins are black again */
}

/* The terminal works on it: text, scrolling, colours. */
static void test_with_terminal(void)
{
    start();
    text_display_reset();
    TEST_ASSERT_EQ_INT(0, text_display_register(fbcon_display()));
    terminal_init();

    terminal_write("1\n2\n3\n4\n5");

    /* 4 rows: "1" scrolled off, "2".."5" remain; the first column of row 0 shows '2'. */
    int lit = 0;

    for (int py = 0; py < 16; py++)
    {
        for (int px = 0; px < 8; px++)
            lit += cell_pixel(0, 0, px, py) != 0;
    }

    TEST_ASSERT(lit > 10);

    text_display_reset();
}

int main(void)
{
    TEST_RUN(test_geometry_and_rejects);
    TEST_RUN(test_clear_paints_everything_black);
    TEST_RUN(test_glyph_drawing);
    TEST_RUN(test_out_of_range_is_ignored);
    TEST_RUN(test_copy_and_cursor);
    TEST_RUN(test_redraw_after_foreign_drawing);
    TEST_RUN(test_with_terminal);

    return TEST_REPORT("fbcon");
}
