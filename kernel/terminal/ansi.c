/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ansi.h"

#include "terminal.h"

#include <stdbool.h>

#define ANSI_MAX_PARAMS 4
#define ANSI_PARAM_LIMIT 9999

typedef enum
{
    STATE_NORMAL,
    STATE_ESCAPE, /* after ESC */
    STATE_CSI,    /* after ESC [ */
} state_t;

static state_t state;
static int params[ANSI_MAX_PARAMS];
static int param_count;
static bool param_seen; /* the parameter being read has at least one digit */
static bool private_marker;

/* SGR state */
static int sgr_fg = 7;
static int sgr_bg = 0;
static bool sgr_bright_fg;
static bool sgr_bright_bg;
static bool sgr_bold;
static bool sgr_reverse;

static int saved_x;
static int saved_y;

/* ECMA-48 colour number (black, red, green, yellow, blue, magenta, cyan, white) to VGA colour. */
static const color_t color_map[8] = {
    COLOR_BLACK, COLOR_RED, COLOR_GREEN, COLOR_BROWN,
    COLOR_BLUE, COLOR_MAGENTA, COLOR_CYAN, COLOR_LIGHT_GREY};

static void apply_colors(void)
{
    int fg = sgr_fg | (sgr_bright_fg || sgr_bold ? 8 : 0);
    int bg = sgr_bg | (sgr_bright_bg ? 8 : 0);

    if (sgr_reverse)
    {
        int swap = fg;

        fg = bg;
        bg = swap;
    }

    /* color_map gives the VGA colour of the low three bits; bit 3 is the same in both numberings. */
    terminal_set_color(
        (color_t)(color_map[fg & 7] | (fg & 8)),
        (color_t)(color_map[bg & 7] | (bg & 8)));
}

static void sgr_defaults(void)
{
    sgr_fg = 7;
    sgr_bg = 0;
    sgr_bright_fg = false;
    sgr_bright_bg = false;
    sgr_bold = false;
    sgr_reverse = false;
}

void ansi_reset(void)
{
    state = STATE_NORMAL;
    sgr_defaults();
    apply_colors();
}

static int param(int index, int fallback)
{
    if (index >= param_count || params[index] == 0)
        return fallback;

    return params[index];
}

static void select_graphic_rendition(void)
{
    if (param_count == 0)
        sgr_defaults();

    for (int i = 0; i < param_count; i++)
    {
        int code = params[i];

        if (code == 0)
            sgr_defaults();
        else if (code == 1)
            sgr_bold = true;
        else if (code == 22)
            sgr_bold = false;
        else if (code == 7)
            sgr_reverse = true;
        else if (code == 27)
            sgr_reverse = false;
        else if (code >= 30 && code <= 37)
        {
            sgr_fg = code - 30;
            sgr_bright_fg = false;
        }
        else if (code == 39)
        {
            sgr_fg = 7;
            sgr_bright_fg = false;
        }
        else if (code >= 40 && code <= 47)
        {
            sgr_bg = code - 40;
            sgr_bright_bg = false;
        }
        else if (code == 49)
        {
            sgr_bg = 0;
            sgr_bright_bg = false;
        }
        else if (code >= 90 && code <= 97)
        {
            sgr_fg = code - 90;
            sgr_bright_fg = true;
        }
        else if (code >= 100 && code <= 107)
        {
            sgr_bg = code - 100;
            sgr_bright_bg = true;
        }
    }

    apply_colors();
}

static void erase_display(int mode)
{
    int x;
    int y;
    int last_x = terminal_width() - 1;
    int last_y = terminal_height() - 1;

    terminal_get_cursor(&x, &y);

    if (mode == 0)
        terminal_erase(x, y, last_x, last_y);
    else if (mode == 1)
        terminal_erase(0, 0, x, y);
    else if (mode == 2 || mode == 3)
        terminal_erase(0, 0, last_x, last_y);
}

static void erase_line(int mode)
{
    int x;
    int y;

    terminal_get_cursor(&x, &y);

    if (mode == 0)
        terminal_erase(x, y, terminal_width() - 1, y);
    else if (mode == 1)
        terminal_erase(0, y, x, y);
    else if (mode == 2)
        terminal_erase(0, y, terminal_width() - 1, y);
}

static void run_csi(char final)
{
    int x;
    int y;

    /* ESC [ ? ... (DEC private modes such as hiding the cursor) is read and ignored. */
    if (private_marker)
        return;

    terminal_get_cursor(&x, &y);

    switch (final)
    {
    case 'A':
        terminal_move_cursor(x, y - param(0, 1));
        break;

    case 'B':
        terminal_move_cursor(x, y + param(0, 1));
        break;

    case 'C':
        terminal_move_cursor(x + param(0, 1), y);
        break;

    case 'D':
        terminal_move_cursor(x - param(0, 1), y);
        break;

    case 'E':
        terminal_move_cursor(0, y + param(0, 1));
        break;

    case 'F':
        terminal_move_cursor(0, y - param(0, 1));
        break;

    case 'G':
        terminal_move_cursor(param(0, 1) - 1, y);
        break;

    case 'd':
        terminal_move_cursor(x, param(0, 1) - 1);
        break;

    case 'H':
    case 'f':
        terminal_move_cursor(param(1, 1) - 1, param(0, 1) - 1);
        break;

    case 'J':
        erase_display(param_count > 0 ? params[0] : 0);
        break;

    case 'K':
        erase_line(param_count > 0 ? params[0] : 0);
        break;

    case 'm':
        select_graphic_rendition();
        break;

    case 's':
        saved_x = x;
        saved_y = y;
        break;

    case 'u':
        terminal_move_cursor(saved_x, saved_y);
        break;

    default:
        break;
    }
}

static void csi_byte(char c)
{
    if (c >= '0' && c <= '9')
    {
        if (param_count == 0)
            param_count = 1;

        int *value = &params[param_count - 1];

        if (!param_seen)
            *value = 0;

        param_seen = true;
        *value = *value * 10 + (c - '0');

        if (*value > ANSI_PARAM_LIMIT)
            *value = ANSI_PARAM_LIMIT;

        return;
    }

    if (c == ';')
    {
        /* An empty parameter counts as 0. */
        if (param_count == 0)
        {
            params[0] = 0;
            param_count = 1;
        }

        if (param_count < ANSI_MAX_PARAMS)
        {
            params[param_count++] = 0;
        }

        param_seen = false;

        return;
    }

    if (c == '?' || c == '>' || c == '=' || c == '<')
    {
        private_marker = true;

        return;
    }

    if (c >= 0x20 && c <= 0x2F)
        return; /* intermediate byte */

    if (c >= 0x40 && c <= 0x7E)
        run_csi(c);

    /* A final byte ends the sequence; so does anything else that cannot be part of one. */
    state = STATE_NORMAL;
}

static void normal_byte(char c)
{
    int x;
    int y;

    switch (c)
    {
    case 0x1B:
        terminal_mirror_raw(c);
        state = STATE_ESCAPE;
        return;

    case '\r':
        terminal_mirror_raw(c);
        terminal_get_cursor(&x, &y);
        terminal_move_cursor(0, y);
        return;

    case '\t':
        terminal_mirror_raw(c);
        terminal_get_cursor(&x, &y);
        terminal_move_cursor((x / 8 + 1) * 8, y);
        return;

    case '\n':
        terminal_put_char(c);
        return;

    case '\b':
        /* Moves left without erasing (a program that wants a blank prints "\b \b"). */
        terminal_mirror_raw(c);
        terminal_get_cursor(&x, &y);
        terminal_move_cursor(x - 1, y);
        return;

    default:
        break;
    }

    /* Other control characters (bell and so on) are not shown. */
    if ((unsigned char)c < 0x20 || c == 0x7F)
    {
        terminal_mirror_raw(c);
        return;
    }

    terminal_put_char(c);
}

void ansi_put_char(char c)
{
    if (state == STATE_NORMAL)
    {
        normal_byte(c);

        return;
    }

    /* The serial side gets the sequence as it is. */
    terminal_mirror_raw(c);

    if (state == STATE_ESCAPE)
    {
        if (c == '[')
        {
            state = STATE_CSI;
            param_count = 0;
            param_seen = false;
            private_marker = false;
        }
        else if (c == 'c')
        {
            /* RIS: full reset */
            ansi_reset();
            terminal_clear();
            terminal_move_cursor(0, 0);
            state = STATE_NORMAL;
        }
        else
        {
            state = STATE_NORMAL;
        }

        return;
    }

    csi_byte(c);
}
