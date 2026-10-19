/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Checking a volume (fsck): it reads, it does not repair. The allocation
 * bitmap is compared with the blocks every owner says it has, one bitmap
 * block at a time: the owners are walked again for each bitmap block, which
 * keeps the memory fixed (slow on large volumes; tools/ehfs may do better).
 */

#include "btree.h"
#include "catalog.h"
#include "ehfs.h"

#include "kernel/lib/k_crc32c.h"
#include "kernel/lib/k_memory.h"

#define BITS_PER_BLOCK (EHFS_BLOCK_SIZE * 8u)

static void problem(ehfs_check_t *report, const char *what)
{
    if (report->errors++ == 0)
    {
        uint32_t i = 0;

        while (what[i] != '\0' && i + 1 < sizeof report->first)
        {
            report->first[i] = what[i];
            i++;
        }
        report->first[i] = '\0';
    }
}

typedef int (*extent_fn)(void *ctx, uint64_t start, uint64_t count);

typedef struct
{
    extent_fn visit;
    void *ctx;
} file_walk_t;

static int file_extents(void *ctx, const ehfs_entry_t *e)
{
    file_walk_t *w = ctx;
    const uint8_t *forks[2] = {e->data_fork, e->rsrc_fork};
    int r = EHFS_OK;

    for (int f = 0; f < 2 && r == EHFS_OK; f++)
    {
        for (uint32_t i = 0; r == EHFS_OK && i < EHFS_INLINE_EXTENTS; i++)
        {
            uint64_t count = ehfs_le64(forks[f] + FORK_EXTENT + 16 * i + 8);

            if (count != 0)
                r = w->visit(w->ctx, ehfs_le64(forks[f] + FORK_EXTENT + 16 * i), count);
        }
    }
    return r;
}

/* Every run of blocks that something owns. */
static int walk_owners(ehfs_t *fs, extent_fn visit, void *ctx)
{
    int r = visit(ctx, 0, 1);       /* the superblock */

    if (r == EHFS_OK)
        r = visit(ctx, fs->blocks - 1, 1);  /* its copy */
    for (int f = 0; r == EHFS_OK && f < EHFS_SPECIAL_FORKS; f++)
    {
        const uint8_t *fork = ehfs_sb_fork(fs, f);

        for (uint32_t i = 0; r == EHFS_OK && i < EHFS_INLINE_EXTENTS; i++)
        {
            uint64_t count = ehfs_le64(fork + FORK_EXTENT + 16 * i + 8);

            if (count != 0)
                r = visit(ctx, ehfs_le64(fork + FORK_EXTENT + 16 * i), count);
        }
    }
    if (r == EHFS_OK)
    {
        file_walk_t w = {visit, ctx};

        r = ehfs_cat_each_file(fs, file_extents, &w);
    }
    return r;
}

typedef struct
{
    ehfs_t *fs;
    ehfs_check_t *report;
    uint64_t first;             /* the bitmap block's first block number */
    uint8_t *expected;          /* EHFS_BLOCK_SIZE bytes of bits */
} expect_t;

static int expect(void *ctx, uint64_t start, uint64_t count)
{
    expect_t *e = ctx;
    uint64_t end = start + count;

    if (end > e->fs->blocks || end < start)
    {
        problem(e->report, "an extent goes past the end of the volume");
        return EHFS_OK;
    }
    if (start < e->first)
        start = e->first;
    if (end > e->first + BITS_PER_BLOCK)
        end = e->first + BITS_PER_BLOCK;
    for (uint64_t n = start; n < end; n++)
    {
        uint8_t *byte = &e->expected[(n - e->first) / 8];
        uint8_t bit = (uint8_t)(1u << (n % 8));

        if (*byte & bit)
            problem(e->report, "two owners share a block");
        *byte |= bit;
    }
    return EHFS_OK;
}

static void check_special_forks(ehfs_t *fs, ehfs_check_t *report)
{
    for (int f = 0; f < EHFS_SPECIAL_FORKS; f++)
    {
        const uint8_t *fork = ehfs_sb_fork(fs, f);
        uint64_t sum = 0;

        for (uint32_t i = 0; i < EHFS_INLINE_EXTENTS; i++)
            sum += ehfs_le64(fork + FORK_EXTENT + 16 * i + 8);
        if (sum != ehfs_le64(fork + FORK_BLOCKS))
            problem(report, "a special file's extents do not add up to its block count");
        if (f != EHFS_FORK_BAD_BLOCKS && ehfs_le64(fork + FORK_LENGTH) != sum * EHFS_BLOCK_SIZE)
            problem(report, "a special file's length is not its blocks");
    }

    const uint8_t *journal = ehfs_sb_fork(fs, EHFS_FORK_JOURNAL);

    if (ehfs_le64(journal + FORK_EXTENT + 8) != ehfs_le64(journal + FORK_BLOCKS))
        problem(report, "the journal is not one run of blocks");
}

static void check_tree_header(ehfs_t *fs, int which, ehfs_check_t *report)
{
    const uint8_t *fork = ehfs_sb_fork(fs, which);
    uint8_t *node = fs->node[0];
    uint64_t nodes = ehfs_le64(fork + FORK_BLOCKS) / EHFS_NODE_BLOCKS;

    for (uint32_t k = 0; k < EHFS_NODE_BLOCKS; k++)
    {
        uint64_t b;

        if (ehfs_fork_inline_block(fork, k, &b) != EHFS_OK || ehfs_read_block(fs, b, node + k * EHFS_BLOCK_SIZE) != EHFS_OK)
        {
            problem(report, "a B-tree's header node cannot be read");
            return;
        }
    }
    if (!ehfs_node_intact(node) || node[NODE_KIND] != EHFS_NODE_HEADER)
        problem(report, "a B-tree's header node is damaged");
    else if (ehfs_le64(node + HDR_NODES) != nodes || ehfs_le32(node + HDR_NODE_SIZE) != EHFS_NODE_SIZE ||
             !(node[HDR_MAP] & 1))
        problem(report, "a B-tree's header node does not match its file");
}

int ehfs_check(ehfs_t *fs, ehfs_check_t *report)
{
    k_memset(report, 0, sizeof *report);

    /* The superblock's copy in the last block. */
    if (ehfs_read_block(fs, fs->blocks - 1, fs->block) != EHFS_OK)
    {
        problem(report, "the superblock's copy cannot be read");
    }
    else
    {
        const uint8_t *copy = fs->block + EHFS_SB_OFFSET;

        if (k_memcmp(copy + SB_MAGIC, EHFS_MAGIC, 8) != 0 || ehfs_le32(copy + SB_CRC) != k_crc32c(0, copy, SB_CRC) ||
            k_memcmp(copy + SB_UUID, fs->sb + SB_UUID, 16) != 0 ||
            ehfs_le64(copy + SB_BLOCK_COUNT) != fs->blocks)
            problem(report, "the superblock's copy does not match");
    }
    if (ehfs_sb_get64(fs, SB_FREE_BLOCKS) > fs->blocks)
        problem(report, "more free blocks than blocks");

    check_special_forks(fs, report);
    check_tree_header(fs, EHFS_FORK_CATALOG, report);
    check_tree_header(fs, EHFS_FORK_EXTENTS, report);
    check_tree_header(fs, EHFS_FORK_ATTRIBUTES, report);
    if (report->errors == 0)
    {
        ehfs_bt_check(fs, EHFS_FORK_CATALOG, report);
        ehfs_bt_check(fs, EHFS_FORK_EXTENTS, report);
        ehfs_bt_check(fs, EHFS_FORK_ATTRIBUTES, report);
    }
    if (report->errors == 0)
        ehfs_cat_check(fs, report);

    /* The bitmap, one block at a time, against the owners. */
    uint64_t used = 0;
    uint64_t bitmap_blocks = (fs->blocks + BITS_PER_BLOCK - 1) / BITS_PER_BLOCK;
    uint8_t *expected = fs->node[1];
    expect_t e = {fs, report, 0, expected};

    for (uint64_t i = 0; i < bitmap_blocks; i++)
    {
        uint64_t b;

        e.first = i * BITS_PER_BLOCK;
        k_memset(expected, 0, EHFS_BLOCK_SIZE);
        if (walk_owners(fs, expect, &e) != EHFS_OK ||
            ehfs_fork_inline_block(ehfs_sb_fork(fs, EHFS_FORK_BITMAP), i, &b) != EHFS_OK ||
            ehfs_read_block(fs, b, fs->block) != EHFS_OK)
        {
            problem(report, "the bitmap cannot be read");
            break;
        }
        for (uint64_t n = e.first; n < e.first + BITS_PER_BLOCK && n < fs->blocks; n++)
        {
            uint32_t at = (uint32_t)((n - e.first) / 8);
            uint8_t bit = (uint8_t)(1u << (n % 8));
            bool on_disk = (fs->block[at] & bit) != 0;

            if (on_disk != ((expected[at] & bit) != 0))
                problem(report, on_disk ? "a block is marked in use but nothing owns it"
                                        : "a block in use is marked free");
            used += on_disk;
        }
    }
    if (report->errors == 0 && used + ehfs_sb_get64(fs, SB_FREE_BLOCKS) != fs->blocks)
        problem(report, "the free block count does not match the bitmap");

    return report->errors == 0 ? EHFS_OK : EHFS_ECORRUPT;
}
