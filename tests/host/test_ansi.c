/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include <stddef.h>
#include <string.h>

#include "kernel/display/text_display.h"
#include "kernel/terminal/ansi.h"
#include "kernel/terminal/terminal.h"

#define W 10
#define H 4

typedef struct
{
    char c;
    color_t fg;
    color_t bg;
} cell_t;

static cell_t cells[H][W];
static int cursor_x;
static int cursor_y;

static void fake_put_cell(int x, int y, char c, color_t fg, color_t bg)
{
    if (x < 0 || x >= W || y < 0 || y >= H)
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
    cursor_x = x;
    cursor_y = y;
}

static void fake_clear(void)
{
    for (int y = 0; y < H; y++)
    {
        for (int x = 0; x < W; x++)
            fake_put_cell(x, y, ' ', COLOR_LIGHT_GREY, COLOR_BLACK);
    }
}

static const text_display_t fake_display = {
    W, H, fake_put_cell, fake_copy_cell, fake_set_cursor, fake_clear};

static char mirrored[256];
static unsigned int mirrored_length;

static void mirror(char c)
{
    if (mirrored_length < sizeof(mirrored) - 1)
        mirrored[mirrored_length++] = c;

    mirrored[mirrored_length] = '\0';
}

static const char *row(int y)
{
    static char buf[W + 1];

    for (int x = 0; x < W; x++)
        buf[x] = cells[y][x].c;

    buf[W] = 0;

    return buf;
}

static void start(void)
{
    text_display_reset();
    text_display_register(&fake_display);
    terminal_init();
    ansi_reset();
    mirrored_length = 0;
    mirrored[0] = '\0';
    terminal_set_mirror(mirror);
}

static void put(const char *text)
{
    while (*text)
        ansi_put_char(*text++);
}

static void test_plain_text(void)
{
    start();
    put("ab\ncd");

    TEST_ASSERT_EQ_STR("ab        ", row(0));
    TEST_ASSERT_EQ_STR("cd        ", row(1));
    TEST_ASSERT_EQ_STR("ab\ncd", mirrored);
}

static void test_cursor_moves(void)
{
    start();
    put("\x1b[3;5Hx");
    TEST_ASSERT_EQ_INT('x', cells[2][4].c);
    TEST_ASSERT_EQ_INT(5, cursor_x);
    TEST_ASSERT_EQ_INT(2, cursor_y);

    put("\x1b[2Ay\x1b[B\x1b[3Dz");
    TEST_ASSERT_EQ_INT('y', cells[0][5].c);
    TEST_ASSERT_EQ_INT('z', cells[1][3].c);

    put("\x1b[H");
    TEST_ASSERT_EQ_INT(0, cursor_x);
    TEST_ASSERT_EQ_INT(0, cursor_y);

    /* Limited to the screen. */
    put("\x1b[99;99H");
    TEST_ASSERT_EQ_INT(W - 1, cursor_x);
    TEST_ASSERT_EQ_INT(H - 1, cursor_y);
    put("\x1b[99A\x1b[99D");
    TEST_ASSERT_EQ_INT(0, cursor_x);
    TEST_ASSERT_EQ_INT(0, cursor_y);

    put("\x1b[3G\x1b[2d");
    TEST_ASSERT_EQ_INT(2, cursor_x);
    TEST_ASSERT_EQ_INT(1, cursor_y);

    put("\x1b[2E");
    TEST_ASSERT_EQ_INT(0, cursor_x);
    TEST_ASSERT_EQ_INT(3, cursor_y);

    put("\x1b[1F");
    TEST_ASSERT_EQ_INT(2, cursor_y);
}

static void test_save_restore(void)
{
    start();
    put("\x1b[2;3H\x1b[s\x1b[4;9H\x1b[u");
    TEST_ASSERT_EQ_INT(2, cursor_x);
    TEST_ASSERT_EQ_INT(1, cursor_y);
}

static void test_erase(void)
{
    start();
    put("aaaaaaaaa\nbbbbbbbbb\nccccccccc");

    put("\x1b[2;4H\x1b[K");
    TEST_ASSERT_EQ_STR("aaaaaaaaa ", row(0));
    TEST_ASSERT_EQ_STR("bbb       ", row(1));

    put("\x1b[1K");
    TEST_ASSERT_EQ_STR("          ", row(1));

    put("\x1b[3;5H\x1b[J");
    TEST_ASSERT_EQ_STR("aaaaaaaaa ", row(0));
    TEST_ASSERT_EQ_STR("cccc      ", row(2));

    put("\x1b[1J");
    TEST_ASSERT_EQ_STR("          ", row(0));
    TEST_ASSERT_EQ_INT(' ', cells[2][4].c);

    put("zzz\x1b[2J");
    TEST_ASSERT_EQ_STR("          ", row(2));
    /* The cursor stays where it was. */
    TEST_ASSERT_EQ_INT(7, cursor_x);
    TEST_ASSERT_EQ_INT(2, cursor_y);
}

static void test_colors(void)
{
    start();
    put("\x1b[31mr\x1b[1;32mg\x1b[0mn\x1b[44;93mb\x1b[7mi\x1b[m!");

    TEST_ASSERT_EQ_INT(COLOR_RED, cells[0][0].fg);
    TEST_ASSERT_EQ_INT(COLOR_BLACK, cells[0][0].bg);
    TEST_ASSERT_EQ_INT(COLOR_LIGHT_GREEN, cells[0][1].fg); /* bold green */
    TEST_ASSERT_EQ_INT(COLOR_LIGHT_GREY, cells[0][2].fg);
    TEST_ASSERT_EQ_INT(COLOR_YELLOW, cells[0][3].fg);      /* 93 = bright yellow */
    TEST_ASSERT_EQ_INT(COLOR_BLUE, cells[0][3].bg);
    TEST_ASSERT_EQ_INT(COLOR_BLUE, cells[0][4].fg);        /* reverse swaps */
    TEST_ASSERT_EQ_INT(COLOR_YELLOW, cells[0][4].bg);
    TEST_ASSERT_EQ_INT(COLOR_LIGHT_GREY, cells[0][5].fg);
    TEST_ASSERT_EQ_INT(COLOR_BLACK, cells[0][5].bg);

    /* Brown is yellow without the bright bit. */
    put("\x1b[33mY");
    TEST_ASSERT_EQ_INT(COLOR_BROWN, cells[0][6].fg);

    put("\x1b[39;49mD");
    TEST_ASSERT_EQ_INT(COLOR_LIGHT_GREY, cells[0][7].fg);
}

static void test_control_characters(void)
{
    start();
    put("abc\rX");
    TEST_ASSERT_EQ_STR("Xbc       ", row(0));

    put("\t!");
    TEST_ASSERT_EQ_INT('!', cells[0][8].c);

    /* Bell and other controls are not shown. */
    put("\a\x01");
    TEST_ASSERT_EQ_INT(9, cursor_x);
}

static void test_mirror_gets_everything(void)
{
    start();
    put("a\x1b[31mb\x1b[2J");

    TEST_ASSERT_EQ_STR("a\x1b[31mb\x1b[2J", mirrored);
}

static void test_unknown_and_private(void)
{
    start();
    /* Unknown final bytes, private modes, a lone ESC letter: read and ignored. */
    put("a\x1b[?25l\x1b[5xb\x1bMc\x1b[99;99;99;99;99;99zd");

    TEST_ASSERT_EQ_STR("abcd      ", row(0));
}

static void test_split_sequence_and_limits(void)
{
    start();

    /* The parser keeps its state between bytes. */
    ansi_put_char('\x1b');
    ansi_put_char('[');
    ansi_put_char('2');
    ansi_put_char(';');
    ansi_put_char('3');
    ansi_put_char('H');
    ansi_put_char('q');
    TEST_ASSERT_EQ_INT('q', cells[1][2].c);

    /* Huge numbers saturate instead of overflowing. */
    put("\x1b[99999999999999999999;1Hr");
    TEST_ASSERT_EQ_INT('r', cells[H - 1][0].c);
}

static void test_reset(void)
{
    start();
    put("\x1b[31;42m\x1b");
    ansi_reset();
    put("a");

    /* The half-read ESC was dropped, so 'a' is text, in the default colours. */
    TEST_ASSERT_EQ_INT('a', cells[0][0].c);
    TEST_ASSERT_EQ_INT(COLOR_LIGHT_GREY, cells[0][0].fg);
    TEST_ASSERT_EQ_INT(COLOR_BLACK, cells[0][0].bg);

    put("\x1b[31m\x1b" "c" "b");
    TEST_ASSERT_EQ_INT('b', cells[0][0].c);
    TEST_ASSERT_EQ_INT(COLOR_LIGHT_GREY, cells[0][0].fg);
}

static void test_without_display(void)
{
    text_display_reset();
    terminal_init();
    ansi_reset();
    mirrored_length = 0;
    mirrored[0] = '\0';
    terminal_set_mirror(mirror);

    put("a\x1b[2J\x1b[5;5Hb\r\t");

    TEST_ASSERT_EQ_STR("a\x1b[2J\x1b[5;5Hb\r\t", mirrored);
    TEST_ASSERT_EQ_INT(80, terminal_width());
    TEST_ASSERT_EQ_INT(25, terminal_height());

    terminal_set_mirror(NULL);
}

int main(void)
{
    TEST_RUN(test_plain_text);
    TEST_RUN(test_cursor_moves);
    TEST_RUN(test_save_restore);
    TEST_RUN(test_erase);
    TEST_RUN(test_colors);
    TEST_RUN(test_control_characters);
    TEST_RUN(test_mirror_gets_everything);
    TEST_RUN(test_unknown_and_private);
    TEST_RUN(test_split_sequence_and_limits);
    TEST_RUN(test_reset);
    TEST_RUN(test_without_display);

    return TEST_REPORT("ansi");
}
