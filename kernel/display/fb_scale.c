/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "fb_scale.h"

#include "kernel/lib/k_memory.h"

void fb_blit_scaled(uint8_t *destination, uint32_t pitch, const uint32_t *source,
                    uint32_t width, uint32_t height, uint32_t scale)
{
    if (scale == 0)
        return;

    for (uint32_t y = 0; y < height; y++)
    {
        uint32_t *row = (uint32_t *)(destination + (size_t)y * scale * pitch);

        /* Build the widened row once, straight into the frame buffer ... */
        for (uint32_t x = 0; x < width; x++)
        {
            uint32_t pixel = source[(size_t)y * width + x];

            for (uint32_t i = 0; i < scale; i++)
                row[x * scale + i] = pixel;
        }

        /* ... and copy it for the other rows of the block. */
        for (uint32_t j = 1; j < scale; j++)
            k_memcpy(destination + ((size_t)y * scale + j) * pitch, row, (size_t)width * scale * 4);
    }
}

uint32_t fb_best_scale(uint32_t width, uint32_t height, uint32_t screen_width,
                       uint32_t screen_height)
{
    uint32_t scale = 1;

    if (width == 0 || height == 0)
        return 1;

    while ((scale + 1) * width <= screen_width && (scale + 1) * height <= screen_height)
        scale++;

    return scale;
}
