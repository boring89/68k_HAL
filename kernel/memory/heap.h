/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_MEMORY_HEAP_H
#define KERNEL_MEMORY_HEAP_H

#include <stddef.h>
#include <stdint.h>

/*
 * The kernel heap lives at its own virtual addresses, from HEAP_VIRT_BASE up
 * to HEAP_VIRT_LIMIT, and grows by mapping more PMM frames at its end (see
 * heap_vm_init). KERNEL_HEAP_SIZE is how much it maps at first.
 */
#define HEAP_VIRT_BASE 0x20000000u
#define HEAP_VIRT_LIMIT 0x30000000u
#define KERNEL_HEAP_SIZE (1024 * 1024)

typedef struct
{
    uintptr_t header;
    uintptr_t address;
    size_t size;
    uint8_t used;

} heap_block_info_t;

/*
 * Make [start, start + size) the heap, which must be usable memory. Forgets
 * all blocks and any growth function.
 */
void heap_init(uintptr_t start, size_t size);

/*
 * Called when the heap is full: make memory usable at address (the current
 * end of the heap, so the heap stays one contiguous range) and return how
 * many bytes it made usable, at least min_bytes, or 0 if it cannot.
 */
typedef size_t (*heap_grow_t)(uintptr_t address, size_t min_bytes);

/*
 * Let the heap grow with grow, but never past limit (an address at or above
 * the current end of the heap). Pass NULL to stop growing.
 */
void heap_set_grow(heap_grow_t grow, uintptr_t limit);

/*
 * heap_init at HEAP_VIRT_BASE with growth by PMM frames mapped through
 * hal/mmu.h, up to HEAP_VIRT_LIMIT, and map KERNEL_HEAP_SIZE bytes at once.
 * Call after pmm_init and hal_init (paging must be on). Panics if the first
 * megabyte cannot be mapped.
 */
void heap_vm_init(void);

int heap_get_block(int index, heap_block_info_t *info);

void *kmalloc(size_t size);

int kfree(void *ptr);

uintptr_t heap_get_start(void);

uintptr_t heap_get_current(void);

size_t heap_get_used(void);

size_t heap_get_reserved(void);

#endif
