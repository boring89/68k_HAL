/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_LIB_K_MEMORY_H
#define KERNEL_LIB_K_MEMORY_H

#include <stdint.h>


void *k_memcpy(
    void *dest,
    const void *src,
    uint32_t size
);


void *k_memset(
    void *dest,
    uint8_t value,
    uint32_t size
);


int k_memcmp(
    const void *a,
    const void *b,
    uint32_t size
);


#endif