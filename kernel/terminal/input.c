/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "input.h"
#include "terminal.h"

#include <stdbool.h>

#define HISTORY_SIZE 16
#define PROMPT_MAX 16

static char input_buffer[TERMINAL_INPUT_SIZE];
static size_t length;
static size_t cursor; /* 0 .. length */

static terminal_submit_callback_t submit_callback;
static terminal_completer_t completer;
static char prompt[PROMPT_MAX];

/* Where buffer[0] is on the screen, as a linear position (row * width + column). */
static int origin;
static bool origin_valid;

static char history[HISTORY_SIZE][TERMINAL_INPUT_SIZE];
static int history_count;
static int history_position = -1; /* -1: typing a new line; else the line being looked at (0 = newest) */
static char draft[TERMINAL_INPUT_SIZE]; /* the line that was being typed when browsing began */

/* Escape sequence reader. */
typedef enum
{
    ESC_NONE,
    ESC_START,   /* after ESC */
    ESC_BRACKET, /* after ESC [ */
    ESC_O        /* after ESC O */
} escape_state_t;

static escape_state_t escape;
static int escape_number;

static void copy_text(char *to, const char *from)
{
    while ((*to++ = *from++) != '\0')
        ;
}

void terminal_input_init(void)
{
    length = 0;
    cursor = 0;
    input_buffer[0] = 0;
    origin_valid = false;
    history_count = 0;
    history_position = -1;
    escape = ESC_NONE;
}

void terminal_input_set_callback(terminal_submit_callback_t callback)
{
    submit_callback = callback;
}

void terminal_input_set_completer(terminal_completer_t function)
{
    completer = function;
}

void terminal_input_set_prompt(const char *text)
{
    size_t i = 0;

    for (; text != NULL && text[i] != '\0' && i < PROMPT_MAX - 1; i++)
        prompt[i] = text[i];

    prompt[i] = '\0';
}

/* --- screen --- */

static int width(void)
{
    return terminal_width();
}

/* The cursor as a linear position; a cursor waiting at the end of a full line counts as one more. */
static int screen_position(void)
{
    int x;
    int y;

    terminal_get_cursor(&x, &y);

    return y * width() + x + (terminal_cursor_pending() ? 1 : 0);
}

/* The line starts where the cursor is now, minus what is before the cursor in the line. */
static void find_origin(void)
{
    origin = screen_position() - (int)cursor;
    origin_valid = true;
}

static void move_to(int position)
{
    terminal_move_cursor(position % width(), position / width());
}

static void show_char(char c)
{
    terminal_put_char(c);
}

/*
 * Draw buffer[from ..] again over what is on the screen, then blank `extra` more
 * cells (the line got shorter), and put the cursor where the cursor index says.
 */
static void redraw_from(size_t from, size_t extra)
{
    bool was_suspended = terminal_set_mirror_suspended(true);

    /* A line that ends exactly at the right edge has no cell for position origin + from yet. */
    if (from > 0 && (origin + (int)from) % width() == 0)
        from--;

    move_to(origin + (int)from);

    for (size_t i = from; i < length; i++)
        show_char(input_buffer[i]);

    for (size_t i = 0; i < extra; i++)
        show_char(' ');

    /* The text reached the bottom and the screen scrolled: the line is higher up now. */
    int expected = origin + (int)(length + extra);
    int actual = screen_position();

    if (actual != expected)
        origin -= expected - actual;

    /* Leave the cursor at its place, unless it is at the end of the printed text already. */
    if (cursor != length + extra || extra != 0)
        move_to(origin + (int)cursor);

    terminal_set_mirror_suspended(was_suspended);
}

/* --- editing --- */

static void insert_char(char c)
{
    if (length >= TERMINAL_INPUT_SIZE - 1)
        return;

    if (!terminal_has_display())
    {
        /* No cursor to move: only appending works. */
        input_buffer[length++] = c;
        input_buffer[length] = 0;
        cursor = length;
        terminal_put_char(c);

        return;
    }

    if (!origin_valid)
        find_origin();

    for (size_t i = length; i > cursor; i--)
        input_buffer[i] = input_buffer[i - 1];

    input_buffer[cursor++] = c;
    input_buffer[++length] = 0;

    if (cursor == length)
    {
        /* Appending is the common case: show it and let the mirror (the serial port) see it. */
        terminal_put_char(c);

        /* At the bottom of the screen the new character may have scrolled the line up. */
        int expected = origin + (int)cursor;
        int actual = screen_position();

        if (actual != expected)
            origin -= expected - actual;

        return;
    }

    redraw_from(cursor - 1, 0);
}

static void erase_range(size_t from, size_t to)
{
    if (from >= to)
        return;

    size_t removed = to - from;

    for (size_t i = to; i <= length; i++)
        input_buffer[i - removed] = input_buffer[i];

    length -= removed;
    cursor = from;

    if (!terminal_has_display())
        return;

    if (!origin_valid)
        find_origin();

    redraw_from(from, removed);
}

static void backspace(void)
{
    if (cursor == 0)
        return;

    if (!terminal_has_display())
    {
        /* Only at the end of the line. */
        if (length > 0)
        {
            length--;
            cursor = length;
            input_buffer[length] = 0;
            terminal_backspace();
        }

        return;
    }

    if (cursor == length && !terminal_cursor_pending())
    {
        /* The simple case, also for the mirror: the character before the cursor. */
        length--;
        cursor = length;
        input_buffer[length] = 0;
        terminal_backspace();

        return;
    }

    if (!origin_valid)
        find_origin();

    erase_range(cursor - 1, cursor);
}

static void set_cursor_index(size_t index)
{
    if (!terminal_has_display() || index > length)
        return;

    if (!origin_valid)
        find_origin();

    cursor = index;

    int position = origin + (int)cursor;

    /*
     * The end of a line that fills the width exactly has no cell to put the cursor on: show
     * the last character again, which leaves the cursor waiting on that column as typing does.
     */
    if (cursor > 0 && cursor == length && position % width() == 0)
    {
        bool was_suspended = terminal_set_mirror_suspended(true);

        move_to(position - 1);
        show_char(input_buffer[length - 1]);
        terminal_set_mirror_suspended(was_suspended);

        return;
    }

    move_to(position);
}

static void replace_line(const char *text)
{
    size_t old_length = length;

    if (!origin_valid)
        find_origin();

    size_t i = 0;

    for (; text[i] != '\0' && i < TERMINAL_INPUT_SIZE - 1; i++)
        input_buffer[i] = text[i];

    input_buffer[i] = 0;
    length = i;
    cursor = i;

    redraw_from(0, old_length > length ? old_length - length : 0);

    /* After blanking the tail the cursor belongs at the end of the new text. */
    set_cursor_index(length);
}

static void history_add(const char *line)
{
    if (line[0] == '\0')
        return;

    /* The same line twice in a row is kept once. */
    if (history_count > 0)
    {
        const char *newest = history[history_count - 1];
        size_t i = 0;

        while (line[i] != '\0' && line[i] == newest[i])
            i++;

        if (line[i] == '\0' && newest[i] == '\0')
            return;
    }

    if (history_count == HISTORY_SIZE)
    {
        for (int i = 1; i < HISTORY_SIZE; i++)
            copy_text(history[i - 1], history[i]);

        history_count--;
    }

    copy_text(history[history_count++], line);
}

/* Up (older) or Down (newer) through the history. */
static void browse(int direction)
{
    if (!terminal_has_display() || history_count == 0)
        return;

    if (history_position < 0)
    {
        if (direction > 0)
            return; /* already at the newest: Down does nothing */

        copy_text(draft, input_buffer);
        history_position = 0;
    }
    else
    {
        int next = history_position - direction;

        if (next >= history_count)
            return;

        if (next < 0)
        {
            history_position = -1;
            replace_line(draft);

            return;
        }

        history_position = next;
    }

    replace_line(history[history_count - 1 - history_position]);
}

/* --- completion --- */

static size_t common_prefix(const terminal_completion_t *c)
{
    size_t common = 0;

    while (c->item[0][common] != '\0')
    {
        for (int i = 1; i < c->count; i++)
        {
            if (c->item[i][common] != c->item[0][common])
                return common;
        }

        common++;
    }

    return common;
}

static void show_prompt_and_line(void)
{
    bool was_suspended = terminal_set_mirror_suspended(true);

    terminal_write(prompt);
    origin = screen_position();
    origin_valid = true;

    for (size_t i = 0; i < length; i++)
        show_char(input_buffer[i]);

    /* Scrolling while printing moved the line up. */
    int expected = origin + (int)length;
    int actual = screen_position();

    if (actual != expected)
        origin -= expected - actual;

    terminal_set_mirror_suspended(was_suspended);
    set_cursor_index(cursor);
}

static void complete(void)
{
    static terminal_completion_t completion;

    if (completer == NULL || !terminal_has_display())
        return;

    if (!origin_valid)
        find_origin();

    completion.start = 0;
    completion.count = 0;
    completer(input_buffer, cursor, &completion);

    if (completion.count <= 0 || completion.start > cursor)
        return;

    size_t typed = cursor - completion.start;
    size_t common = common_prefix(&completion);

    /* Longer than what is typed: put the extra text in, then (one candidate) a space. */
    if (common > typed)
    {
        for (size_t i = typed; i < common; i++)
            insert_char(completion.item[0][i]);

        if (completion.count == 1 && completion.item[0][common - 1] != '/')
            insert_char(' ');

        return;
    }

    if (completion.count == 1)
        return;

    /* Nothing to add: list the candidates under the line and show the line again. */
    terminal_set_mirror_suspended(true);
    terminal_newline();

    for (int i = 0; i < completion.count; i++)
    {
        terminal_write(completion.item[i]);
        terminal_put_char(' ');
    }

    terminal_newline();
    terminal_set_mirror_suspended(false);
    show_prompt_and_line();
}

/* --- keys --- */

void terminal_input_submit(void)
{
    /* The callback may print; it gets its own copy so the editor state can be reset first. */
    char line[TERMINAL_INPUT_SIZE];

    copy_text(line, input_buffer);
    history_add(line);
    terminal_input_clear();

    if (submit_callback)
        submit_callback(line);
}

static void clear_screen(void)
{
    terminal_clear();
    show_prompt_and_line();
}

static void control(char c)
{
    switch (c)
    {
    case 1: /* Ctrl-A */
        set_cursor_index(0);
        break;

    case 5: /* Ctrl-E */
        set_cursor_index(length);
        break;

    case 11: /* Ctrl-K */
        erase_range(cursor, length);
        break;

    case 21: /* Ctrl-U */
        erase_range(0, cursor);
        break;

    case 23: /* Ctrl-W */
    {
        size_t start = cursor;

        while (start > 0 && input_buffer[start - 1] == ' ')
            start--;

        while (start > 0 && input_buffer[start - 1] != ' ')
            start--;

        erase_range(start, cursor);
        break;
    }

    case 12: /* Ctrl-L */
        if (terminal_has_display())
            clear_screen();

        break;

    case 3: /* Ctrl-C */
        terminal_write("^C");
        terminal_newline();
        terminal_input_clear();

        /* An empty line makes the shell show its prompt again. */
        if (submit_callback)
            submit_callback("");

        break;

    default:
        break;
    }
}

/* The final byte of ESC [ ... or ESC O ... */
static void escape_final(char c, int number)
{
    switch (c)
    {
    case 'A':
        browse(-1);
        break;

    case 'B':
        browse(1);
        break;

    case 'C':
        set_cursor_index(cursor + 1);
        break;

    case 'D':
        if (cursor > 0)
            set_cursor_index(cursor - 1);

        break;

    case 'H':
        set_cursor_index(0);
        break;

    case 'F':
        set_cursor_index(length);
        break;

    case '~':
        if (number == 1 || number == 7)
            set_cursor_index(0);
        else if (number == 4 || number == 8)
            set_cursor_index(length);
        else if (number == 3 && cursor < length)
            erase_range(cursor, cursor + 1);

        break;

    default:
        break;
    }
}

void terminal_input_char(char c)
{
    if (escape != ESC_NONE)
    {
        if (escape == ESC_START)
        {
            escape = c == '[' ? ESC_BRACKET : c == 'O' ? ESC_O : ESC_NONE;
            escape_number = 0;

            return;
        }

        if (c >= '0' && c <= '9')
        {
            escape_number = escape_number * 10 + (c - '0');

            return;
        }

        if (c == ';')
        {
            escape_number = 0;

            return;
        }

        escape = ESC_NONE;
        escape_final(c, escape_number);

        return;
    }

    if (c == 0x1B)
    {
        escape = ESC_START;

        return;
    }

    if (c == '\b' || c == 0x7F)
    {
        backspace();

        return;
    }

    if (c == '\n' || c == '\r')
    {
        if (terminal_has_display() && origin_valid && cursor != length)
            set_cursor_index(length); /* the newline goes after the whole line */

        terminal_newline();
        terminal_input_submit();

        return;
    }

    if (c == '\t')
    {
        complete();

        return;
    }

    if ((unsigned char)c < 0x20)
    {
        control(c);

        return;
    }

    history_position = -1;
    insert_char(c);
}

const char *terminal_input_get(void)
{
    return input_buffer;
}

void terminal_input_clear(void)
{
    length = 0;
    cursor = 0;
    input_buffer[0] = 0;
    origin_valid = false;
    history_position = -1;
    escape = ESC_NONE;
}
