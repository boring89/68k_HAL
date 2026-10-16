/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_DISPLAY_FBCON_H
#define KERNEL_DISPLAY_FBCON_H

#include "hal/framebuffer.h"
#include "text_display.h"

#include <stdint.h>

/*
 * A text display (text_display.h) drawn on a graphics framebuffer: the
 * console for machines that boot in a graphics mode, where there is no VGA
 * text buffer (UEFI machines, or the "graphics" GRUB entry).
 *
 * Cells are 8x16 pixels: the 8x8 font (public domain IBM BIOS font,
 * characters 0-127; others show as blanks) with every row drawn twice. The
 * 16 text colours are the standard VGA palette. The cursor is an underline.
 * The grid is centred in the framebuffer, below top_margin pixel rows that
 * are left alone. 640x480 gives 80x30 cells (80x29 with a 16 pixel margin).
 */
#define FBCON_CELL_WIDTH 8
#define FBCON_CELL_HEIGHT 16

/* Character glyphs: font[c][row], bit 0 = leftmost pixel. */
extern const uint8_t fbcon_font[128][8];

/*
 * Prepare the display for fb (which must stay valid, 32 bits per pixel).
 * The grid starts blank, but nothing is drawn yet. Returns 0, or -1 if fb is
 * NULL, not 32 bpp, too small for one cell, or memory ran out. Calling it
 * again (a new mode) replaces the old grid.
 */
int fbcon_init(const hal_framebuffer_t *fb, uint32_t top_margin);

/* The display for text_display_register(). NULL before a successful fbcon_init. */
const text_display_t *fbcon_display(void);

/* Draw the whole grid again, e.g. after a program used the framebuffer. */
void fbcon_redraw(void);

#endif
