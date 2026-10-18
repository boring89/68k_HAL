/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_BLOCK_OVERLAY_H
#define KERNEL_BLOCK_OVERLAY_H

#include <stdint.h>

#include "kernel/block/block.h"

/*
 * A copy-on-write block overlay: reads of blocks that were written come
 * from the overlay store, all other reads from the base device, and every
 * write goes to the store. The base is never written. Clearing the store
 * ("reset") gives the base back. docs/mac_disk.md has the picture.
 *
 * Store layout (all blocks are the base's block size; the store must have
 * the same block size):
 *
 *   block 0                header (magic, which System slot, base identity)
 *   blocks 1..map_blocks   map: one uint32 per base block, 0 = not written,
 *                          n = the data block n-1 holds it
 *   rest                   data blocks
 *
 * Power-fail safety: a block that is already mapped is overwritten in
 * place (one block write, assumed atomic). A new block is written to the
 * next free data block FIRST, and only then is the map block holding its
 * entry written (also one block). A crash in between leaves an unused data
 * block that open() reuses; the map never points at data that was not
 * written. Reset writes the header with the RESETTING flag first, so an
 * interrupted reset is finished at the next open.
 *
 * The identity of the base (size plus a CRC-32 of its first and last MiB)
 * is stored in the header; an overlay whose base changed is not applied.
 */

#define OVERLAY_FLAG_RESETTING 1u

/* How much of each end of the base overlay_identity reads. */
#define OVERLAY_IDENTITY_BYTES (1024 * 1024)

typedef enum
{
    OVERLAY_OK = 0,
    OVERLAY_EMPTY,      /* no overlay in the store: call overlay_format */
    OVERLAY_MISMATCH,   /* belongs to another base or slot: not applied */
    OVERLAY_ERROR       /* I/O error or damaged store */
} overlay_status_t;

typedef struct
{
    uint64_t base_blocks;
    uint32_t base_crc;
    uint32_t slot;          /* which System slot (0 = A, 1 = B) */
} overlay_identity_t;

typedef struct
{
    block_device_t device;  /* the merged view; registered by overlay_open */
    block_device_t *base;
    block_device_t *store;
    overlay_identity_t id;
    uint32_t *map;
    uint32_t map_blocks;
    uint32_t data_blocks;
    uint32_t next_free;     /* next unused data block */
    uint32_t mapped;        /* number of base blocks that are written */
    uint32_t block_size;
} overlay_t;

/* The identity base has now; slot is stored as given. -1 on an I/O error. */
int overlay_identity(block_device_t *base, uint32_t slot,
                     overlay_identity_t *id);

/*
 * Write an empty overlay for base into store (a reset: whatever the store
 * held is gone). Returns 0, or -1 if the store is too small, the block
 * sizes differ or an I/O error happens.
 */
int overlay_format(block_device_t *store, block_device_t *base,
                   uint32_t slot);

/*
 * Open the overlay in store for base and slot. On OVERLAY_OK the merged
 * device is registered under name (name must stay valid); the overlay
 * structure must too. Any other status registers nothing.
 */
overlay_status_t overlay_open(overlay_t *overlay, const char *name,
                              block_device_t *base, block_device_t *store,
                              uint32_t slot);

/*
 * Empty an open overlay (a reset): the store is formatted again and the
 * merged device reads as the base from now on. Returns 0, or -1 on an I/O
 * error (the overlay is then unusable until reopened).
 */
int overlay_reset(overlay_t *overlay);

/* Written blocks and capacity, for status displays. */
uint32_t overlay_used_blocks(const overlay_t *overlay);
uint32_t overlay_capacity_blocks(const overlay_t *overlay);

const char *overlay_status_name(overlay_status_t status);

#endif
