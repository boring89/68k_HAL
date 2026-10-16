/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_TERMINAL_TERMINAL_H
#define KERNEL_TERMINAL_TERMINAL_H

#include "kernel/console/color.h"

#include <stdbool.h>

/*
 * Text output with a cursor on the registered text display
 * (kernel/display/text_display.h). Does nothing without one.
 */

/* Attach to the registered display and clear it. */
void terminal_init(void);

/*
 * Mirror: a function that gets every character the terminal shows, as
 * the text appears (a newline for a line break, '\b' for a backspace;
 * not for soft wrapping at the right edge). The console uses it to copy
 * the screen to the serial port. NULL (the default) = no mirror. The
 * mirror also receives text when there is no display (graphics mode).
 */
void terminal_set_mirror(void (*mirror)(char c));

void terminal_newline(void);

void terminal_backspace(void);

void terminal_tab(void);

void terminal_put_char(char c);

void terminal_write(const char *str);

void terminal_write_line(const char *str);

void terminal_set_color(
    color_t foreground,
    color_t background
);

void terminal_write_color(
    const char *str,
    color_t fg,
    color_t bg
);

void terminal_write_color_line(
    const char *str,
    color_t fg,
    color_t bg
);

void terminal_clear(void);

/*
 * Cursor and screen primitives for the escape-sequence parser (ansi.h).
 * Without a display they do nothing (the size is then 80x25 and the
 * cursor is 0, 0).
 */

/*
 * While suspended, nothing shown reaches the mirror (the line editor redraws
 * text that the mirror has already seen). Returns the previous state.
 */
bool terminal_set_mirror_suspended(bool suspended);

/* True with a display (otherwise cursor and erase calls do nothing). */
bool terminal_has_display(void);

/*
 * The last column of the line was written and the line has not broken yet:
 * the cursor shows on that column but the next character goes to the start of
 * the next line.
 */
bool terminal_cursor_pending(void);

/* Send c to the mirror only, without showing it. */
void terminal_mirror_raw(char c);

int terminal_width(void);
int terminal_height(void);

void terminal_get_cursor(int *x, int *y);

/* Move the cursor, limited to the screen. */
void terminal_move_cursor(int x, int y);

/* Fill the cells from (from_x, from_y) to (to_x, to_y), both included, with blanks in the current colours. */
void terminal_erase(int from_x, int from_y, int to_x, int to_y);

#endif