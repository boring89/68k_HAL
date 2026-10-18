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

uint32_t fb_scaled_size(uint32_t size, uint32_t halves)
{
    return size * halves / 2;
}

uint32_t fb_blit_halves(uint8_t *destination, uint32_t pitch, const uint32_t *source, const uint8_t *changed,
                        uint32_t width, uint32_t height, uint32_t halves, uint32_t *row)
{
    uint32_t out_width = fb_scaled_size(width, halves);
    uint32_t drawn = 0;

    if (halves < 2 || halves > FB_SCALE_HALVES_MAX)
        return 0;

    for (uint32_t y = 0; y < height; y++)
    {
        const uint32_t *line = source + (size_t)y * width;
        /* The destination rows j with j * 2 / halves == y, like the columns below. */
        uint32_t first = (y * halves + 1) / 2;
        uint32_t end = ((y + 1) * halves + 1) / 2;

        if ((changed != NULL && !changed[y]) || first == end)
            continue;

        drawn++;

        if (halves == 2)
        {
            k_memcpy(destination + (size_t)first * pitch, line, (size_t)width * 4);
            continue;
        }
        if (halves % 2 == 0)
        {
            uint32_t scale = halves / 2;

            for (uint32_t x = 0; x < width; x++)
            {
                for (uint32_t i = 0; i < scale; i++)
                    row[x * scale + i] = line[x];
            }
        }
        else
        {
            /* Destination pixel x shows source pixel x * 2 / halves. */
            for (uint32_t x = 0; x < out_width; x++)
                row[x] = line[x * 2 / halves];
        }
        for (uint32_t j = first; j < end; j++)
            k_memcpy(destination + (size_t)j * pitch, row, (size_t)out_width * 4);
    }
    return drawn;
}

int fb_choose_size(const uint16_t *widths, const uint16_t *heights, uint32_t count, uint32_t screen_width,
                   uint32_t screen_height, uint32_t *index, uint32_t *halves)
{
    static const uint32_t candidates[] = {4, 3, 2};
    uint64_t best_area = 0;

    *index = 0;
    *halves = 2;

    for (uint32_t c = 0; c < sizeof candidates / sizeof candidates[0]; c++)
    {
        for (uint32_t i = 0; i < count; i++)
        {
            uint32_t w = fb_scaled_size(widths[i], candidates[c]);
            uint32_t h = fb_scaled_size(heights[i], candidates[c]);
            uint64_t area = (uint64_t)w * h;

            /* Strictly larger: on a tie the earlier candidate, the larger enlargement, stays. */
            if (w <= screen_width && h <= screen_height && area > best_area)
            {
                best_area = area;
                *index = i;
                *halves = candidates[c];
            }
        }
    }
    return best_area > 0 ? 0 : -1;
}
