/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_DISPLAY_FB_SCALE_H
#define KERNEL_DISPLAY_FB_SCALE_H

#include <stddef.h>
#include <stdint.h>

/*
 * Draw a picture enlarged by a whole factor: every source pixel becomes a
 * scale x scale block of the same colour ("pixel doubling" for scale 2). The
 * result is sharp, no blur, which suits the Mac's 640x480 screen on a modern
 * display.
 *
 * source is width x height pixels (0x00RRGGBB, rows follow each other
 * directly). destination points at the top left pixel of the target area
 * inside a frame buffer whose rows are pitch bytes apart. The target area is
 * width*scale x height*scale pixels; the caller makes sure it fits.
 */
void fb_blit_scaled(uint8_t *destination, uint32_t pitch, const uint32_t *source,
                    uint32_t width, uint32_t height, uint32_t scale);

/*
 * The largest scale (at least 1) at which a width x height picture fits a
 * screen_width x screen_height display.
 */
uint32_t fb_best_scale(uint32_t width, uint32_t height, uint32_t screen_width,
                       uint32_t screen_height);

#endif
