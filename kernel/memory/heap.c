/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "heap.h"

/* Every block header and payload starts on an 8-byte boundary. */
#define HEAP_ALIGN 8

#define HEAP_ALIGN_UP(value) \
    (((value) + (HEAP_ALIGN - 1)) & ~(uintptr_t)(HEAP_ALIGN - 1))

/* "HEAP" in ASCII; marks a valid block header. */
#define HEAP_MAGIC 0x48454150u

typedef struct
{
    uint32_t magic;
    uint32_t used;
    size_t size;

} heap_block_t;

/* Header size rounded up so the payload after it stays aligned. */
#define HEAP_HEADER_SIZE HEAP_ALIGN_UP(sizeof(heap_block_t))

static size_t heap_used;

static uintptr_t heap_current;

static uintptr_t heap_start;

static uintptr_t heap_end;

static heap_block_t *heap_block_at(uintptr_t address)
{
    return (heap_block_t *)address;
}

static uintptr_t heap_next_address(uintptr_t address)
{
    return address + HEAP_HEADER_SIZE + heap_block_at(address)->size;
}

void heap_init(uintptr_t start, size_t size)
{
    uintptr_t aligned = HEAP_ALIGN_UP(start);
    uintptr_t padding = aligned - start;

    heap_start = aligned;

    heap_end = (padding < size) ? aligned + (size - padding) : aligned;

    heap_current = heap_start;

    heap_used = 0;
}

static void heap_split(
    heap_block_t *block,
    size_t size)
{
    size_t remaining =
        block->size - size;

    if (remaining <= HEAP_HEADER_SIZE)
        return;

    heap_block_t *new_block =
        heap_block_at(
            (uintptr_t)block + HEAP_HEADER_SIZE + size);

    new_block->magic = HEAP_MAGIC;

    new_block->size =
        remaining - HEAP_HEADER_SIZE;

    new_block->used = 0;

    block->size = size;
}

static heap_block_t *heap_find_free(size_t size)
{
    uintptr_t current = heap_start;

    while (current < heap_current)
    {
        heap_block_t *block =
            heap_block_at(current);

        if (!block->used &&
            block->size >= size)
        {
            return block;
        }

        current = heap_next_address(current);
    }

    return 0;
}

/*
 * Merge adjacent free blocks and give a trailing free block back to
 * the unreserved area. Absorbed headers lose their magic so a stale
 * address inside a merged block cannot pass kfree's check.
 */
static void heap_merge(void)
{
    uintptr_t current = heap_start;

    while (current < heap_current)
    {
        heap_block_t *block = heap_block_at(current);

        uintptr_t next_address =
            heap_next_address(current);

        if (next_address >= heap_current)
        {
            if (!block->used)
            {
                block->magic = 0;
                heap_current = current;
            }

            break;
        }

        heap_block_t *next =
            heap_block_at(next_address);

        if (!block->used && !next->used)
        {
            block->size +=
                HEAP_HEADER_SIZE +
                next->size;

            next->magic = 0;

            continue;
        }

        current = next_address;
    }
}

void *kmalloc(size_t size)
{
    if (size == 0 || size > SIZE_MAX - (HEAP_ALIGN - 1))
        return 0;

    size = HEAP_ALIGN_UP(size);

    heap_block_t *block =
        heap_find_free(size);

    if (block)
    {
        heap_split(block, size);

        block->used = 1;

        /* block->size may exceed size when the block was not split. */
        heap_used += block->size;

        return (void *)((uintptr_t)block + HEAP_HEADER_SIZE);
    }

    uintptr_t available = heap_end - heap_current;

    if (available < HEAP_HEADER_SIZE ||
        size > available - HEAP_HEADER_SIZE)
    {
        return 0;
    }

    block = heap_block_at(heap_current);

    block->magic = HEAP_MAGIC;
    block->size = size;
    block->used = 1;

    heap_current += HEAP_HEADER_SIZE;

    void *ptr = (void *)heap_current;

    heap_current += size;

    heap_used += size;

    return ptr;
}

/* Returns -1 without touching the heap if ptr is not an allocated block. */
int kfree(void *ptr)
{
    uintptr_t address = (uintptr_t)ptr;

    /* Range and alignment first, so the header read stays inside the heap. */
    if (address < heap_start + HEAP_HEADER_SIZE ||
        address >= heap_current ||
        address % HEAP_ALIGN != 0)
    {
        return -1;
    }

    heap_block_t *block =
        heap_block_at(address - HEAP_HEADER_SIZE);

    if (block->magic != HEAP_MAGIC || !block->used)
        return -1;

    block->used = 0;

    heap_used -= block->size;

    heap_merge();

    return 0;
}

uintptr_t heap_get_start(void)
{
    return heap_start;
}

uintptr_t heap_get_current(void)
{
    return heap_current;
}

size_t heap_get_used(void)
{
    return heap_used;
}

size_t heap_get_reserved(void)
{
    return heap_current - heap_start;
}

int heap_get_block(
    int index,
    heap_block_info_t *info)
{
    uintptr_t current = heap_start;

    int i = 0;

    while (current < heap_current)
    {
        heap_block_t *block =
            heap_block_at(current);

        if (i == index)
        {
            info->header = current;
            info->address =
                current + HEAP_HEADER_SIZE;

            info->size = block->size;
            info->used = block->used;

            return 0;
        }

        current = heap_next_address(current);

        i++;
    }

    return -1;
}
