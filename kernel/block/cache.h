/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_BLOCK_CACHE_H
#define KERNEL_BLOCK_CACHE_H

#include <stdint.h>

#include "kernel/block/block.h"

/*
 * A read cache for block devices, write-through (docs/mac_disk.md section 11):
 *
 *   - the disk is read in lines of BLOCK_CACHE_LINE_BYTES (consecutive blocks; a read of one block
 *     brings in its neighbours), kept in RAM and reused least-recently-used first;
 *   - every write goes to the disk at once, in the order it was made, and then into the cached line
 *     if there is one. So nothing has to be flushed before a power off, and the overlay's ordering
 *     (data first, then its map entry: kernel/block/overlay.h) reaches the disk unchanged.
 *
 * Only devices given to block_cache_enable are cached (the 68k_HAL disk; its partitions read through it).
 */

#define BLOCK_CACHE_LINE_BYTES (32u * 1024)
#define BLOCK_CACHE_MAX_DEVICES 4

typedef struct
{
    uint64_t hits;          /* lines found in the cache */
    uint64_t misses;        /* lines read from the disk */
    uint64_t evictions;     /* lines given to other data */
    uint64_t writes;        /* write requests (each also went to the disk) */
    uint32_t lines_used;    /* lines holding data */
    uint32_t lines_max;
} block_cache_stats_t;

/*
 * Make room for max_bytes of cached data (rounded down to whole lines; 0 = no cache). The line
 * buffers come from alloc_line (BLOCK_CACHE_LINE_BYTES each, as they are needed; NULL = no more
 * memory) and the line table from the heap. Returns 0, or -1 if the table cannot be allocated.
 */
int block_cache_init(uint64_t max_bytes, void *(*alloc_line)(void));

/* Cache device from now on. Returns 0, or -1 (no cache, table full, block size not a divisor of a line). */
int block_cache_enable(block_device_t *device);

/* 1 if device is cached. */
int block_cache_enabled(const block_device_t *device);

/* Called by block_read / block_write for a cached device (the range is already checked). */
int block_cache_read(block_device_t *device, uint64_t lba, uint32_t count, void *buffer);
int block_cache_write(block_device_t *device, uint64_t lba, uint32_t count, const void *buffer);

/* Forget every cached line (the memory stays for reuse). */
void block_cache_drop(void);

void block_cache_get_stats(block_cache_stats_t *stats);

/* Forget everything, devices included (for tests). The line buffers are not freed. */
void block_cache_reset(void);

#endif
