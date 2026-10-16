/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "fb_pattern.h"

#include <stddef.h>

#define BAR_COUNT 8

/* 0x00RRGGBB, the order of the classic SMPTE-style bars. */
static const uint32_t bar_colors[BAR_COUNT] = {
    0x00FFFFFF, 0x00FFFF00, 0x0000FFFF, 0x0000FF00,
    0x00FF00FF, 0x00FF0000, 0x000000FF, 0x00000000,
};

void fb_draw_test_pattern(const hal_framebuffer_t *fb)
{
    if (fb == NULL || fb->bpp != 32)
        return;

    uint32_t bars_height = fb->height / 4 * 3;

    for (uint32_t y = 0; y < fb->height; y++)
    {
        uint32_t *row = (uint32_t *)(fb->base + (size_t)y * fb->pitch);

        for (uint32_t x = 0; x < fb->width; x++)
        {
            if (y < bars_height)
            {
                row[x] = bar_colors[x * BAR_COUNT / fb->width];
            }
            else
            {
                uint32_t level = x * 255 / (fb->width > 1 ? fb->width - 1 : 1);

                row[x] = level << 16 | level << 8 | level;
            }
        }
    }
}
