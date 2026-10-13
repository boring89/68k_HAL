/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_MEMORY_HEAP_H
#define KERNEL_MEMORY_HEAP_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uintptr_t header;
    uintptr_t address;
    size_t size;
    uint8_t used;

} heap_block_info_t;

void heap_init(uintptr_t start, size_t size);

int heap_get_block(int index, heap_block_info_t *info);

void *kmalloc(size_t size);

int kfree(void *ptr);

uintptr_t heap_get_start(void);

uintptr_t heap_get_current(void);

size_t heap_get_used(void);

size_t heap_get_reserved(void);

#endif
