/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * The allocation bitmap (CNID 5): bit n (least significant first within a
 * byte) is 1 when block n is in use. Its blocks are metadata: every change
 * goes through the transaction.
 */

#include "ehfs.h"

#include "kernel/lib/k_memory.h"

#define BITS_PER_BLOCK (EHFS_BLOCK_SIZE * 8u)

/* The device block of logical block `index` of a fork with only inline extents. */
int ehfs_fork_inline_block(const uint8_t *fork, uint64_t index, uint64_t *block)
{
    uint64_t covered = 0;

    for (uint32_t i = 0; i < EHFS_INLINE_EXTENTS; i++)
    {
        uint64_t start = ehfs_le64(fork + FORK_EXTENT + 16 * i);
        uint64_t count = ehfs_le64(fork + FORK_EXTENT + 16 * i + 8);

        if (index < covered + count)
        {
            *block = start + (index - covered);
            return EHFS_OK;
        }
        covered += count;
    }
    return EHFS_ECORRUPT;
}

static int bitmap_block(ehfs_t *fs, uint64_t n, uint64_t *block)
{
    if (n >= fs->blocks)
        return EHFS_EINVAL;
    return ehfs_fork_inline_block(ehfs_sb_fork(fs, EHFS_FORK_BITMAP), n / BITS_PER_BLOCK, block);
}

int ehfs_block_used(ehfs_t *fs, uint64_t n, bool *used)
{
    uint64_t b;
    int r = bitmap_block(fs, n, &b);

    if (r == EHFS_OK)
        r = ehfs_read_block(fs, b, fs->block);
    if (r == EHFS_OK)
        *used = (fs->block[(n % BITS_PER_BLOCK) / 8] >> (n % 8)) & 1;
    return r;
}

/* Set (value 1) or clear bits [start, start + count); every bit must change. */
static int mark(ehfs_t *fs, uint64_t start, uint64_t count, int value)
{
    uint64_t n = start;

    while (n < start + count)
    {
        uint64_t b;
        int r = bitmap_block(fs, n, &b);

        if (r == EHFS_OK)
            r = ehfs_read_block(fs, b, fs->block);
        if (r != EHFS_OK)
            return r;
        do
        {
            uint8_t *byte = &fs->block[(n % BITS_PER_BLOCK) / 8];
            uint8_t bit = (uint8_t)(1u << (n % 8));

            if (((*byte & bit) != 0) == (value != 0))
                return EHFS_ECORRUPT;       /* allocating a used block, or freeing a free one */
            *byte ^= bit;
            n++;
        } while (n < start + count && n % BITS_PER_BLOCK != 0);
        r = ehfs_write_meta(fs, b, fs->block);
        if (r != EHFS_OK)
            return r;
    }

    uint64_t free_blocks = ehfs_sb_get64(fs, SB_FREE_BLOCKS);

    ehfs_sb_set64(fs, SB_FREE_BLOCKS, value ? free_blocks - count : free_blocks + count);
    return EHFS_OK;
}

int ehfs_alloc(ehfs_t *fs, uint64_t near, uint64_t want, uint64_t *start, uint64_t *count)
{
    if (want == 0 || !fs->in_txn)
        return EHFS_EINVAL;
    if (ehfs_sb_get64(fs, SB_FREE_BLOCKS) == 0)
        return EHFS_ENOSPC;
    if (near >= fs->blocks)
        near = 0;

    /* The first free block from near on (wrapping round), then as many free ones after it as wanted. */
    for (uint64_t pass = 0; pass < 2; pass++)
    {
        uint64_t n = pass == 0 ? near : 0;
        uint64_t end = pass == 0 ? fs->blocks : near;

        while (n < end)
        {
            uint64_t b;
            int r = bitmap_block(fs, n, &b);

            if (r == EHFS_OK)
                r = ehfs_read_block(fs, b, fs->block);
            if (r != EHFS_OK)
                return r;

            uint64_t block_end = (n / BITS_PER_BLOCK + 1) * BITS_PER_BLOCK;

            if (block_end > end)
                block_end = end;
            while (n < block_end)
            {
                uint8_t byte = fs->block[(n % BITS_PER_BLOCK) / 8];

                if (n % 8 == 0 && byte == 0xFF && n + 8 <= block_end)
                {
                    n += 8;
                    continue;
                }
                if (((byte >> (n % 8)) & 1) == 0)
                    break;
                n++;
            }
            if (n == block_end)
                continue;

            /* n is free: the run goes on while the bits stay 0 (across bitmap blocks too). */
            uint64_t len = 0;
            uint64_t loaded = n / BITS_PER_BLOCK;

            while (len < want && n + len < fs->blocks)
            {
                uint64_t m = n + len;

                if (m / BITS_PER_BLOCK != loaded)
                {
                    loaded = m / BITS_PER_BLOCK;
                    r = bitmap_block(fs, m, &b);
                    if (r == EHFS_OK)
                        r = ehfs_read_block(fs, b, fs->block);
                    if (r != EHFS_OK)
                        return r;
                }
                if ((fs->block[(m % BITS_PER_BLOCK) / 8] >> (m % 8)) & 1)
                    break;
                len++;
            }
            r = mark(fs, n, len, 1);
            if (r != EHFS_OK)
                return r;
            *start = n;
            *count = len;
            fs->alloc_hint = n + len;
            return EHFS_OK;
        }
    }
    return EHFS_ENOSPC;
}

int ehfs_free(ehfs_t *fs, uint64_t start, uint64_t count)
{
    if (!fs->in_txn || count == 0 || start + count > fs->blocks || start + count < start)
        return EHFS_EINVAL;
    return mark(fs, start, count, 0);
}
