/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include <stddef.h>

#include "kernel/display/text_display.h"
#include "kernel/terminal/terminal.h"

/* A 4x3 fake display that records every cell and the cursor. */
#define FAKE_WIDTH 4
#define FAKE_HEIGHT 3

typedef struct
{
    char c;
    color_t fg;
    color_t bg;
} fake_cell_t;

static fake_cell_t cells[FAKE_HEIGHT][FAKE_WIDTH];
static int fake_cursor_x;
static int fake_cursor_y;

static void fake_put_cell(int x, int y, char c, color_t fg, color_t bg)
{
    if (x < 0 || x >= FAKE_WIDTH || y < 0 || y >= FAKE_HEIGHT)
        return;

    cells[y][x].c = c;
    cells[y][x].fg = fg;
    cells[y][x].bg = bg;
}

static void fake_copy_cell(int src_x, int src_y, int dst_x, int dst_y)
{
    cells[dst_y][dst_x] = cells[src_y][src_x];
}

static void fake_set_cursor(int x, int y)
{
    fake_cursor_x = x;
    fake_cursor_y = y;
}

static void fake_clear(void)
{
    for (int y = 0; y < FAKE_HEIGHT; y++)
    {
        for (int x = 0; x < FAKE_WIDTH; x++)
            fake_put_cell(x, y, ' ', COLOR_LIGHT_GREY, COLOR_BLACK);
    }
}

static const text_display_t fake_display = {
    FAKE_WIDTH, FAKE_HEIGHT,
    fake_put_cell, fake_copy_cell, fake_set_cursor, fake_clear,
};

/* Row y as a string, for compact assertions. */
static const char *row(int y)
{
    static char buf[FAKE_WIDTH + 1];

    for (int x = 0; x < FAKE_WIDTH; x++)
        buf[x] = cells[y][x].c;

    buf[FAKE_WIDTH] = 0;

    return buf;
}

static void start(void)
{
    text_display_reset();
    TEST_ASSERT_EQ_INT(0, text_display_register(&fake_display));
    terminal_init();
}

static void test_register_rejects(void)
{
    static const text_display_t no_clear = {
        FAKE_WIDTH, FAKE_HEIGHT,
        fake_put_cell, fake_copy_cell, fake_set_cursor, NULL,
    };
    static const text_display_t no_size = {
        0, FAKE_HEIGHT,
        fake_put_cell, fake_copy_cell, fake_set_cursor, fake_clear,
    };

    text_display_reset();

    TEST_ASSERT_EQ_INT(-1, text_display_register(NULL));
    TEST_ASSERT_EQ_INT(-1, text_display_register(&no_clear));
    TEST_ASSERT_EQ_INT(-1, text_display_register(&no_size));
    TEST_ASSERT(text_display_get() == NULL);

    TEST_ASSERT_EQ_INT(0, text_display_register(&fake_display));
    TEST_ASSERT_EQ_INT(-1, text_display_register(&fake_display));
    TEST_ASSERT(text_display_get() == &fake_display);
}

static void test_write_and_cursor(void)
{
    start();

    terminal_write("ab");

    TEST_ASSERT_EQ_STR("ab  ", row(0));
    TEST_ASSERT_EQ_INT(2, fake_cursor_x);
    TEST_ASSERT_EQ_INT(0, fake_cursor_y);
}

static void test_wraps_at_width(void)
{
    start();

    terminal_write("abcde");

    TEST_ASSERT_EQ_STR("abcd", row(0));
    TEST_ASSERT_EQ_STR("e   ", row(1));
    TEST_ASSERT_EQ_INT(1, fake_cursor_x);
    TEST_ASSERT_EQ_INT(1, fake_cursor_y);
}

static void test_scrolls_at_height(void)
{
    start();

    terminal_write("1\n2\n3\n4");

    TEST_ASSERT_EQ_STR("2   ", row(0));
    TEST_ASSERT_EQ_STR("3   ", row(1));
    TEST_ASSERT_EQ_STR("4   ", row(2));
    TEST_ASSERT_EQ_INT(2, fake_cursor_y);
}

static void test_backspace(void)
{
    start();

    terminal_write("ab\b");
    TEST_ASSERT_EQ_STR("a   ", row(0));
    TEST_ASSERT_EQ_INT(1, fake_cursor_x);

    /* At the start of a line it does nothing. */
    terminal_write("\b\b");
    TEST_ASSERT_EQ_INT(0, fake_cursor_x);
    TEST_ASSERT_EQ_INT(0, fake_cursor_y);
}

static void test_write_color_restores(void)
{
    start();

    terminal_write_color("r", COLOR_RED, COLOR_BLUE);
    terminal_write("g");

    TEST_ASSERT_EQ_INT(COLOR_RED, cells[0][0].fg);
    TEST_ASSERT_EQ_INT(COLOR_BLUE, cells[0][0].bg);
    TEST_ASSERT_EQ_INT(COLOR_LIGHT_GREY, cells[0][1].fg);
    TEST_ASSERT_EQ_INT(COLOR_BLACK, cells[0][1].bg);
}

/* The new bottom row takes the current colours. */
static void test_scroll_uses_current_color(void)
{
    start();

    terminal_set_color(COLOR_WHITE, COLOR_GREEN);
    terminal_write("\n\n\n");

    TEST_ASSERT_EQ_INT(COLOR_GREEN, cells[2][0].bg);
}

static void test_clear(void)
{
    start();

    terminal_set_color(COLOR_WHITE, COLOR_GREEN);
    terminal_write("ab\ncd");
    terminal_clear();
    terminal_write("x");

    TEST_ASSERT_EQ_STR("x   ", row(0));
    TEST_ASSERT_EQ_STR("    ", row(1));
    TEST_ASSERT_EQ_INT(COLOR_LIGHT_GREY, cells[0][0].fg);
}

static void test_null_string(void)
{
    start();

    terminal_write(NULL);

    TEST_ASSERT_EQ_STR("NULL", row(0));
}

/* Headless: nothing registered, every call is a no-op. */
static void test_no_display(void)
{
    text_display_reset();
    terminal_init();

    terminal_write("abc\n\b");
    terminal_clear();

    TEST_ASSERT(text_display_get() == NULL);
}

int main(void)
{
    TEST_RUN(test_register_rejects);
    TEST_RUN(test_write_and_cursor);
    TEST_RUN(test_wraps_at_width);
    TEST_RUN(test_scrolls_at_height);
    TEST_RUN(test_backspace);
    TEST_RUN(test_write_color_restores);
    TEST_RUN(test_scroll_uses_current_color);
    TEST_RUN(test_clear);
    TEST_RUN(test_null_string);
    TEST_RUN(test_no_display);

    return TEST_REPORT("terminal");
}
