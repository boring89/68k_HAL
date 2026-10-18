/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "k_crc32.h"

static uint32_t table[256];
static int table_ready;

static void make_table(void)
{
    for (uint32_t n = 0; n < 256; n++)
    {
        uint32_t c = n;

        for (int bit = 0; bit < 8; bit++)
            c = (c >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(c & 1));

        table[n] = c;
    }

    table_ready = 1;
}

uint32_t k_crc32(uint32_t crc, const void *data, size_t size)
{
    const uint8_t *p = data;

    if (!table_ready)
        make_table();

    crc = ~crc;

    while (size-- > 0)
        crc = table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);

    return ~crc;
}
