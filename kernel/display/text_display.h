/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_DISPLAY_TEXT_DISPLAY_H
#define KERNEL_DISPLAY_TEXT_DISPLAY_H

#include "kernel/console/color.h"

/*
 * A grid of character cells, each with a foreground and background
 * colour, plus a hardware cursor. Implemented by drivers, registered by
 * the platform, drawn on by the terminal. Coordinates outside the grid
 * are ignored.
 */
typedef struct
{
    int width;
    int height;

    void (*put_cell)(int x, int y, char c, color_t fg, color_t bg);
    void (*copy_cell)(int src_x, int src_y, int dst_x, int dst_y);
    void (*set_cursor)(int x, int y);

    /* Blank every cell, COLOR_LIGHT_GREY on COLOR_BLACK. */
    void (*clear)(void);
} text_display_t;

/*
 * Make display the one the terminal uses; it must stay valid forever.
 * Returns 0, or -1 if display or one of its functions is NULL, its size
 * is not positive, or a display is already registered.
 */
int text_display_register(const text_display_t *display);

/* The registered display, or NULL if there is none (headless). */
const text_display_t *text_display_get(void);

/* Forget the registered display. */
void text_display_reset(void);

#endif
