/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_MEMORY_HEAP_H
#define KERNEL_MEMORY_HEAP_H

#include <stdint.h>

typedef struct
{
    uint32_t header;
    uint32_t address;
    uint32_t size;
    uint8_t used;

} heap_block_info_t;

void heap_init(void);

int heap_get_block(int index, heap_block_info_t *info);

void *kmalloc(uint32_t size);

void kfree(void *ptr);

uint32_t heap_get_start(void);

uint32_t heap_get_current(void);

uint32_t heap_get_used(void);

uint32_t heap_get_reserved(void);

#endif