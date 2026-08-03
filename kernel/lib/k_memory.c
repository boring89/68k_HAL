/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "k_memory.h"


void *k_memcpy(
    void *dest,
    const void *src,
    uint32_t size
)
{
    uint8_t *d = dest;
    const uint8_t *s = src;

    while(size--)
    {
        *d++ = *s++;
    }

    return dest;
}


void *k_memset(
    void *dest,
    uint8_t value,
    uint32_t size
)
{
    uint8_t *d = dest;

    while(size--)
    {
        *d++ = value;
    }

    return dest;
}


int k_memcmp(
    const void *a,
    const void *b,
    uint32_t size
)
{
    const uint8_t *x = a;
    const uint8_t *y = b;

    while(size--)
    {
        if(*x != *y)
            return *x - *y;

        x++;
        y++;
    }

    return 0;
}