/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "heap.h"
#include "memory.h"

typedef struct
{
    uint32_t size;
    uint8_t used;

} heap_block_t;

static uint32_t heap_used;

static uint32_t heap_current;

static uint32_t heap_start;

void heap_init(void)
{
    heap_start = memory_get_kernel_end();

    heap_current = heap_start;

    heap_used = 0;
}

heap_block_t *heap_find_free(uint32_t size)
{
    uint32_t current = heap_start;

    while (current < heap_current)
    {
        heap_block_t *block =
            (heap_block_t *)current;

        if (!block->used &&
            block->size >= size)
        {
            return block;
        }

        current += sizeof(heap_block_t);
        current += block->size;
    }

    return 0;
}

void *kmalloc(uint32_t size)
{
    if (size == 0)
        return 0;

    heap_block_t *block =
        heap_find_free(size);

    if (block)
    {
        block->used = 1;

        heap_used += block->size;

        return (void *)(block + 1);
    }

    block = (heap_block_t *)heap_current;

    block->size = size;
    block->used = 1;

    heap_current += sizeof(heap_block_t);

    void *ptr = (void *)heap_current;

    heap_current += size;

    heap_used += size;

    return ptr;
}

void kfree(void *ptr)
{
    if (!ptr)
        return;

    uint32_t address = (uint32_t)ptr;

    if (address < heap_start ||
        address >= heap_current)
    {
        return;
    }

    heap_block_t *block =
        ((heap_block_t *)ptr) - 1;

    if (!block->used)
        return;

    block->used = 0;

    heap_used -= block->size;
}

uint32_t heap_get_start(void)
{
    return heap_start;
}

uint32_t heap_get_current(void)
{
    return heap_current;
}

uint32_t heap_get_used(void)
{
    return heap_used;
}

uint32_t heap_get_reserved(void)
{
    return heap_current - heap_start;
}

int heap_get_block(
    int index,
    heap_block_info_t *info)
{
    uint32_t current = heap_start;

    int i = 0;

    while (current < heap_current)
    {
        heap_block_t *block =
            (heap_block_t *)current;

        if (i == index)
        {
            info->header = current;
            info->address =
                current + sizeof(heap_block_t);

            info->size = block->size;
            info->used = block->used;

            return 0;
        }

        current += sizeof(heap_block_t);
        current += block->size;

        i++;
    }

    return -1;
}