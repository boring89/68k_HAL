/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_TERMINAL_TERMINAL_H
#define KERNEL_TERMINAL_TERMINAL_H

#include "kernel/console/color.h"

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

#endif