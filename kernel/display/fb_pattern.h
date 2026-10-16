/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_DISPLAY_FB_PATTERN_H
#define KERNEL_DISPLAY_FB_PATTERN_H

#include "hal/framebuffer.h"

/*
 * Fill the framebuffer with a test pattern: eight vertical colour bars
 * (white, yellow, cyan, green, magenta, red, blue, black) in the top
 * three quarters and a black-to-white gradient in the rest. Used to see
 * that a graphics mode works.
 */
void fb_draw_test_pattern(const hal_framebuffer_t *fb);

/* Only the eight colour bars, in the top height pixel rows (the strip above the graphics console). */
void fb_draw_color_bars(const hal_framebuffer_t *fb, uint32_t height);

#endif
