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
 * The same, but only the rows of source that differ from previous (a picture of the same size that was
 * drawn before; NULL = all rows). Nothing is read from the destination, which may be display memory
 * (write combining: reads are slow). Returns the number of source rows that were drawn.
 */
uint32_t fb_blit_scaled_rows(uint8_t *destination, uint32_t pitch, const uint32_t *source,
                             const uint32_t *previous, uint32_t width, uint32_t height, uint32_t scale);

/*
 * The largest scale (at least 1) at which a width x height picture fits a
 * screen_width x screen_height display.
 */
uint32_t fb_best_scale(uint32_t width, uint32_t height, uint32_t screen_width,
                       uint32_t screen_height);

/*
 * Enlargement in steps of a half: 'halves' = 2 is 1x, 3 is 1.5x, 4 is 2x. Each destination pixel takes the
 * nearest source pixel (no blending: one-pixel lines and the Mac's dither patterns stay sharp; at 1.5x every
 * second source pixel is drawn twice as wide and twice as tall). Destination pixel (x, y) shows source pixel
 * (x * 2 / halves, y * 2 / halves).
 */
#define FB_SCALE_HALVES_MAX 8

/* Size of a picture of 'size' pixels enlarged by halves/2 (rounded down). */
uint32_t fb_scaled_size(uint32_t size, uint32_t halves);

/*
 * Draw the rows of source (width x height) whose entry in 'changed' is not 0 (NULL = all), enlarged by
 * halves/2, at destination (the top left pixel of the target area, rows pitch bytes apart). 'row' is room for
 * one destination row (fb_scaled_size(width, halves) pixels): each destination row is built there and then
 * copied, so the destination (display memory) is only written, in long runs. Returns the number of source
 * rows drawn.
 */
uint32_t fb_blit_halves(uint8_t *destination, uint32_t pitch, const uint32_t *source, const uint8_t *changed,
                        uint32_t width, uint32_t height, uint32_t halves, uint32_t *row);

/*
 * Choose a picture size and enlargement for a screen: among 'count' sizes (widths[i] x heights[i]) and the
 * enlargements 2x, 1.5x and 1x, the one that covers the largest part of the screen; on a tie the larger
 * enlargement (sharper text on a high resolution screen). Writes the index and the enlargement in halves.
 * Returns 0, or -1 if nothing fits (then index 0 at 1x).
 */
int fb_choose_size(const uint16_t *widths, const uint16_t *heights, uint32_t count, uint32_t screen_width,
                   uint32_t screen_height, uint32_t *index, uint32_t *halves);

#endif
