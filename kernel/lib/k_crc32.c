/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "k_crc32.h"

uint32_t k_crc32(uint32_t crc, const void *data, size_t size)
{
    const uint8_t *p = data;

    crc = ~crc;

    while (size-- > 0)
    {
        crc ^= *p++;

        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1));
    }

    return ~crc;
}
