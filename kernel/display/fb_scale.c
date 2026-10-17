/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "fb_scale.h"

#include "kernel/lib/k_memory.h"

uint32_t fb_blit_scaled_rows(uint8_t *destination, uint32_t pitch, const uint32_t *source,
                             const uint32_t *previous, uint32_t width, uint32_t height, uint32_t scale)
{
    uint32_t drawn = 0;

    if (scale == 0)
        return 0;

    for (uint32_t y = 0; y < height; y++)
    {
        const uint32_t *line = source + (size_t)y * width;

        if (previous != NULL && k_memcmp(line, previous + (size_t)y * width, (size_t)width * 4) == 0)
            continue;

        drawn++;

        /* Every output row of the block is written from the source: no read of the destination. */
        for (uint32_t j = 0; j < scale; j++)
        {
            uint32_t *row = (uint32_t *)(destination + ((size_t)y * scale + j) * pitch);

            for (uint32_t x = 0; x < width; x++)
            {
                for (uint32_t i = 0; i < scale; i++)
                    row[x * scale + i] = line[x];
            }
        }
    }

    return drawn;
}

void fb_blit_scaled(uint8_t *destination, uint32_t pitch, const uint32_t *source,
                    uint32_t width, uint32_t height, uint32_t scale)
{
    fb_blit_scaled_rows(destination, pitch, source, NULL, width, height, scale);
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
