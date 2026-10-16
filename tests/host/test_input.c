/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include <stddef.h>
#include <string.h>

#include "kernel/display/text_display.h"
#include "kernel/terminal/input.h"
#include "kernel/terminal/terminal.h"

/* A 12x4 display; the prompt "> " takes two columns. */
#define W 12
#define H 4

typedef struct
{
    char c;
} cell_t;

static cell_t cells[H][W];
static int cursor_x;
static int cursor_y;

static void fake_put_cell(int x, int y, char c, color_t fg, color_t bg)
{
    (void)fg;
    (void)bg;

    if (x >= 0 && x < W && y >= 0 && y < H)
        cells[y][x].c = c;
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
            cells[y][x].c = ' ';
    }
}

static const text_display_t fake_display = {
    W, H, fake_put_cell, fake_copy_cell, fake_set_cursor, fake_clear};

static const char *row(int y)
{
    static char buf[W + 1];

    for (int x = 0; x < W; x++)
        buf[x] = cells[y][x].c;

    buf[W] = 0;

    return buf;
}

/* A row the way the tests write it: shorter texts are blank-padded to the width. */
static const char *padded(const char *text)
{
    static char buf[W + 1];

    memset(buf, ' ', W);
    buf[W] = 0;
    memcpy(buf, text, strlen(text) < W ? strlen(text) : W);

    return buf;
}

#define ASSERT_ROW(y, text) TEST_ASSERT_EQ_STR(padded(text), row(y))

static char submitted[8][TERMINAL_INPUT_SIZE];
static int submitted_count;

static void on_submit(const char *line)
{
    if (submitted_count < 8)
    {
        strcpy(submitted[submitted_count++], line);
    }

    /* The shell prints its prompt after every command. */
    terminal_write("> ");
}

static char mirrored[512];
static size_t mirrored_length;

static void mirror(char c)
{
    if (mirrored_length < sizeof(mirrored) - 1)
        mirrored[mirrored_length++] = c;

    mirrored[mirrored_length] = 0;
}

/* A completer with a few "files". */
static const char *const words[] = {"help", "heap", "hello", "clear", "dir/"};

static void completer(const char *line, size_t cursor, terminal_completion_t *completion)
{
    size_t start = cursor;

    while (start > 0 && line[start - 1] != ' ')
        start--;

    completion->start = start;
    completion->count = 0;

    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++)
    {
        size_t typed = cursor - start;

        if (strncmp(words[i], line + start, typed) == 0 && completion->count < TERMINAL_COMPLETION_MAX)
            strcpy(completion->item[completion->count++], words[i]);
    }
}

static void start(void)
{
    text_display_reset();
    text_display_register(&fake_display);
    terminal_set_mirror(mirror);
    terminal_init();
    terminal_input_init();
    terminal_input_set_callback(on_submit);
    terminal_input_set_completer(completer);
    terminal_input_set_prompt("> ");
    submitted_count = 0;
    mirrored_length = 0;
    mirrored[0] = 0;
    terminal_write("> ");
}

static void type(const char *keys)
{
    while (*keys)
        terminal_input_char(*keys++);
}

#define LEFT "\x1b[D"
#define RIGHT "\x1b[C"
#define UP "\x1b[A"
#define DOWN "\x1b[B"
#define HOME "\x1b[H"
#define END "\x1b[F"
#define DELETE "\x1b[3~"

static void test_typing_and_submit(void)
{
    start();
    type("abc");

    ASSERT_ROW(0, "> abc");
    TEST_ASSERT_EQ_STR("abc", terminal_input_get());

    type("\r");
    TEST_ASSERT_EQ_INT(1, submitted_count);
    TEST_ASSERT_EQ_STR("abc", submitted[0]);
    TEST_ASSERT_EQ_STR("", terminal_input_get());

    /* The mirror saw the typed text once. */
    TEST_ASSERT_EQ_STR("> abc\n> ", mirrored);
}

static void test_cursor_editing(void)
{
    start();
    type("abc" LEFT LEFT "X");

    ASSERT_ROW(0, "> aXbc");
    TEST_ASSERT_EQ_INT(4, cursor_x);
    TEST_ASSERT_EQ_STR("aXbc", terminal_input_get());

    type(HOME "Y");
    ASSERT_ROW(0, "> YaXbc");
    TEST_ASSERT_EQ_INT(3, cursor_x);

    type(END "Z");
    ASSERT_ROW(0, "> YaXbcZ");

    /* Backspace in the middle and Delete. */
    type(HOME RIGHT RIGHT "\x7f");
    ASSERT_ROW(0, "> YXbcZ");
    TEST_ASSERT_EQ_INT(3, cursor_x);

    type(DELETE);
    ASSERT_ROW(0, "> YbcZ");
    TEST_ASSERT_EQ_STR("YbcZ", terminal_input_get());

    /* The limits: Left at the start, Right at the end, Delete at the end do nothing. */
    type(HOME LEFT "\x7f");
    TEST_ASSERT_EQ_STR("YbcZ", terminal_input_get());
    type(END RIGHT DELETE);
    TEST_ASSERT_EQ_STR("YbcZ", terminal_input_get());

    type("\r");
    TEST_ASSERT_EQ_STR("YbcZ", submitted[0]);
}

static void test_control_keys(void)
{
    start();
    type("one two three");

    type("\x17"); /* Ctrl-W: the word before the cursor */
    TEST_ASSERT_EQ_STR("one two ", terminal_input_get());
    ASSERT_ROW(0, "> one two");

    type("\x01"); /* Ctrl-A */
    TEST_ASSERT_EQ_INT(2, cursor_x);
    type(RIGHT RIGHT RIGHT RIGHT "\x0b"); /* Ctrl-K: to the end */
    TEST_ASSERT_EQ_STR("one ", terminal_input_get());
    ASSERT_ROW(0, "> one");

    type("\x05" "x\x15"); /* Ctrl-E, x, Ctrl-U: all of it */
    TEST_ASSERT_EQ_STR("", terminal_input_get());
    ASSERT_ROW(0, ">");

    type("abc\x03"); /* Ctrl-C */
    TEST_ASSERT_EQ_INT(1, submitted_count);
    TEST_ASSERT_EQ_STR("", submitted[0]);
    TEST_ASSERT_EQ_STR("", terminal_input_get());
}

static void test_history(void)
{
    start();
    type("first\r");
    type("second\r");
    type("second\r"); /* a repeat is kept once */
    type("\r");       /* an empty line is not kept */

    type(UP);
    TEST_ASSERT_EQ_STR("second", terminal_input_get());
    type(UP);
    TEST_ASSERT_EQ_STR("first", terminal_input_get());
    type(UP);
    TEST_ASSERT_EQ_STR("first", terminal_input_get()); /* the oldest stays */

    /* A shorter line replaces a longer one completely. */
    

    type(DOWN);
    TEST_ASSERT_EQ_STR("second", terminal_input_get());
    type(DOWN);
    TEST_ASSERT_EQ_STR("", terminal_input_get()); /* back to the new line */
    type(DOWN);
    TEST_ASSERT_EQ_STR("", terminal_input_get());

    /* The line that was being typed comes back. */
    type("par");
    type(UP);
    TEST_ASSERT_EQ_STR("second", terminal_input_get());
    type(DOWN);
    TEST_ASSERT_EQ_STR("par", terminal_input_get());

    /* Recall, edit, run. */
    type(UP LEFT LEFT "X\r");
    TEST_ASSERT_EQ_STR("secoXnd", submitted[submitted_count - 1]);
}

static void test_wrapping_and_scrolling(void)
{
    start();

    /* 12 columns: 2 for the prompt, 10 for text. 25 characters need three rows. */
    type("0123456789abcdefghijklmno");

    ASSERT_ROW(0, "> 0123456789");
    ASSERT_ROW(1, "abcdefghijkl");
    ASSERT_ROW(2, "mno");

    /* Insert in the first row: the rest moves, over the row breaks. */
    type(HOME RIGHT "X");
    ASSERT_ROW(0, "> 0X12345678");
    ASSERT_ROW(1, "9abcdefghijk");
    ASSERT_ROW(2, "lmno");

    /* Delete it again. */
    type("\x7f");
    ASSERT_ROW(0, "> 0123456789");
    ASSERT_ROW(1, "abcdefghijkl");
    ASSERT_ROW(2, "mno");
    TEST_ASSERT_EQ_STR("0123456789abcdefghijklmno", terminal_input_get());

    /* A shorter recalled line blanks the end of the longer one. */
    type("\x01\x0b" "xy" "\r");
    type(UP);
    TEST_ASSERT_EQ_STR("xy", terminal_input_get());
}

static void test_scroll_while_editing(void)
{
    start();

    /* Fill the screen: 3 prompt-less rows, then a long line that scrolls. */
    type("a\rb\rc\r");
    submitted_count = 0;
    type("0123456789ABCDEFGHIJ");

    /* The line starts on the last two rows now; edit its start. */
    ASSERT_ROW(0, "> b");
    ASSERT_ROW(2, "> 0123456789");
    ASSERT_ROW(3, "ABCDEFGHIJ");

    type(HOME "Z");
    TEST_ASSERT_EQ_STR("Z0123456789ABCDEFGHIJ", terminal_input_get());
    ASSERT_ROW(2, "> Z012345678");
    ASSERT_ROW(3, "9ABCDEFGHIJ");

    type(END "!");
    ASSERT_ROW(3, "9ABCDEFGHIJ!");
    TEST_ASSERT_EQ_STR("Z0123456789ABCDEFGHIJ!", terminal_input_get());
    type("\r");
    TEST_ASSERT_EQ_STR("Z0123456789ABCDEFGHIJ!", submitted[0]);
}

static void test_full_line_edge(void)
{
    start();

    /* Exactly fills the first row (10 characters after the prompt). */
    type("0123456789");
    ASSERT_ROW(0, "> 0123456789");

    type("\x7f");
    ASSERT_ROW(0, "> 012345678");
    type("9");
    ASSERT_ROW(0, "> 0123456789");

    /* Move away and back to the end, then type on: it continues on the next row. */
    type(LEFT RIGHT "x");
    ASSERT_ROW(0, "> 0123456789");
    ASSERT_ROW(1, "x");
    TEST_ASSERT_EQ_STR("0123456789x", terminal_input_get());

    type("\r");
    TEST_ASSERT_EQ_STR("0123456789x", submitted[0]);
}

static void test_completion(void)
{
    start();

    /* One candidate: completed, with a space. */
    type("cl\t");
    TEST_ASSERT_EQ_STR("clear ", terminal_input_get());
    type("\x15");

    /* Several: the common prefix. */
    type("h\t");
    TEST_ASSERT_EQ_STR("he", terminal_input_get());

    /* No more in common: the candidates are listed and the line comes back. */
    type("\t");
    TEST_ASSERT_EQ_STR("he", terminal_input_get());
    

    /* A directory gets no space. */
    type("\x15" "di\t");
    TEST_ASSERT_EQ_STR("dir/", terminal_input_get());

    /* Nothing matches: nothing happens. */
    type("\x15" "zz\t");
    TEST_ASSERT_EQ_STR("zz", terminal_input_get());

    /* The word at the cursor, in the middle of a line. */
    type("\x15" "x cl y" LEFT LEFT "\t");
    TEST_ASSERT_EQ_STR("x clear  y", terminal_input_get());
}

static void test_without_display(void)
{
    text_display_reset();
    terminal_init();
    terminal_set_mirror(mirror);
    terminal_input_init();
    terminal_input_set_callback(on_submit);
    submitted_count = 0;
    mirrored_length = 0;
    mirrored[0] = 0;

    type("abc" LEFT "\x7f" "d");

    /* Cursor keys do nothing; Backspace works at the end. */
    TEST_ASSERT_EQ_STR("abd", terminal_input_get());

    type("\r");
    TEST_ASSERT_EQ_STR("abd", submitted[0]);

    terminal_set_mirror(NULL);
}

int main(void)
{
    TEST_RUN(test_typing_and_submit);
    TEST_RUN(test_cursor_editing);
    TEST_RUN(test_control_keys);
    TEST_RUN(test_history);
    TEST_RUN(test_wrapping_and_scrolling);
    TEST_RUN(test_scroll_while_editing);
    TEST_RUN(test_full_line_edge);
    TEST_RUN(test_completion);
    TEST_RUN(test_without_display);

    return TEST_REPORT("input");
}
