/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Making an empty volume (mkfs). Layout, from block 0:
 *
 *   0                 superblock (bytes 1024..4095)
 *   1 ..              allocation bitmap
 *   ..                journal
 *   ..                catalog, extents and attributes B-trees (their first nodes)
 *   ..                free
 *   last              copy of the superblock
 *
 * Written straight to the device (not through the journal): a crash during
 * mkfs leaves no volume, which is what mkfs started from.
 */

#include "catalog.h"
#include "ehfs.h"
#include "name.h"

#include "kernel/lib/k_crc32c.h"
#include "kernel/lib/k_memory.h"
#include "kernel/lib/k_string.h"
#include "unicode_data.h"

#define MIN_BLOCKS 1024u            /* 4 MiB */
#define CATALOG_NODES 32u
#define EXTENTS_NODES 8u
#define ATTRIBUTE_NODES 8u

void ehfs_node_seal(uint8_t *node)
{
    ehfs_put32(node + NODE_CRC, k_crc32c(0, node + 4, EHFS_NODE_SIZE - 4));
}

bool ehfs_node_intact(const uint8_t *node)
{
    return ehfs_le32(node + NODE_CRC) == k_crc32c(0, node + 4, EHFS_NODE_SIZE - 4);
}

void ehfs_tree_header(uint8_t *node, uint64_t nodes)
{
    k_memset(node, 0, EHFS_NODE_SIZE);
    node[NODE_KIND] = EHFS_NODE_HEADER;
    ehfs_put64(node + HDR_NODES, nodes);
    ehfs_put64(node + HDR_FREE_NODES, nodes - 1);
    ehfs_put32(node + HDR_NODE_SIZE, EHFS_NODE_SIZE);
    node[HDR_MAP] = 1;          /* node 0, this one */
    ehfs_node_seal(node);
}

static void set_fork(uint8_t *fork, uint64_t start, uint64_t count)
{
    k_memset(fork, 0, EHFS_FORK_SIZE);
    ehfs_put64(fork + FORK_LENGTH, count * EHFS_BLOCK_SIZE);
    ehfs_put64(fork + FORK_BLOCKS, count);
    ehfs_put64(fork + FORK_EXTENT, start);
    ehfs_put64(fork + FORK_EXTENT + 8, count);
}

static int write_raw(ehfs_t *fs, uint64_t n, const void *buffer)
{
    return block_write(fs->dev, fs->start + n * EHFS_SECTORS_PER_BLOCK, EHFS_SECTORS_PER_BLOCK, buffer) == 0
               ? EHFS_OK
               : EHFS_EIO;
}

int ehfs_format(ehfs_t *fs, block_device_t *dev, uint64_t start_sector, uint64_t sectors,
                const ehfs_format_options_t *options)
{
    uint64_t blocks = sectors / EHFS_SECTORS_PER_BLOCK;
    uint32_t name_length;

    if (fs == NULL || dev == NULL || options == NULL || options->name == NULL || dev->block_size != 512 ||
        dev->ops->write == NULL || start_sector + sectors > dev->block_count || blocks < MIN_BLOCKS)
        return EHFS_EINVAL;
    name_length = (uint32_t)k_strlen(options->name);
    if (!ehfs_name_valid((const uint8_t *)options->name, name_length))
        return EHFS_ENAME;

    k_memset(fs, 0, sizeof *fs);
    fs->dev = dev;
    fs->start = start_sector;
    fs->blocks = blocks;
    fs->crash_after = -1;

    uint64_t bitmap = (blocks + EHFS_BLOCK_SIZE * 8 - 1) / (EHFS_BLOCK_SIZE * 8);
    uint64_t journal = options->journal_blocks;

    if (journal == 0)
    {
        journal = blocks / 256;
        if (journal > 16384)
            journal = 16384;
    }
    if (journal < EHFS_TXN_BLOCKS + 4)
        journal = EHFS_TXN_BLOCKS + 4;

    uint64_t catalog_start = 1 + bitmap + journal;
    uint64_t extents_start = catalog_start + CATALOG_NODES * EHFS_NODE_BLOCKS;
    uint64_t attributes_start = extents_start + EXTENTS_NODES * EHFS_NODE_BLOCKS;
    uint64_t used_end = attributes_start + ATTRIBUTE_NODES * EHFS_NODE_BLOCKS;

    if (used_end + 1 >= blocks)
        return EHFS_ENOSPC;

    /* The superblock. */
    uint8_t *sb = fs->sb;

    k_memcpy(sb + SB_MAGIC, EHFS_MAGIC, 8);
    ehfs_put32(sb + SB_VERSION, EHFS_VERSION);
    ehfs_put32(sb + SB_BLOCK_SIZE, EHFS_BLOCK_SIZE);
    ehfs_put64(sb + SB_BLOCK_COUNT, blocks);
    ehfs_put64(sb + SB_FREE_BLOCKS, blocks - used_end - 1);
    ehfs_put64(sb + SB_NEXT_CNID, EHFS_CNID_FIRST);
    ehfs_put32(sb + SB_STATE, EHFS_STATE_CLEAN);
    k_memcpy(sb + SB_UUID, options->uuid, 16);
    ehfs_put64(sb + SB_CREATED, options->now);
    ehfs_put64(sb + SB_MODIFIED, options->now);
    ehfs_put64(sb + SB_CHECKED, options->now);
    set_fork(sb + SB_FORKS + EHFS_FORK_BITMAP * EHFS_FORK_SIZE, 1, bitmap);
    set_fork(sb + SB_FORKS + EHFS_FORK_JOURNAL * EHFS_FORK_SIZE, 1 + bitmap, journal);
    set_fork(sb + SB_FORKS + EHFS_FORK_CATALOG * EHFS_FORK_SIZE, catalog_start, CATALOG_NODES * EHFS_NODE_BLOCKS);
    set_fork(sb + SB_FORKS + EHFS_FORK_EXTENTS * EHFS_FORK_SIZE, extents_start, EXTENTS_NODES * EHFS_NODE_BLOCKS);
    set_fork(sb + SB_FORKS + EHFS_FORK_ATTRIBUTES * EHFS_FORK_SIZE, attributes_start,
             ATTRIBUTE_NODES * EHFS_NODE_BLOCKS);
    sb[SB_NAME] = (uint8_t)name_length;
    k_memcpy(sb + SB_NAME + 1, options->name, name_length);
    ehfs_put32(sb + SB_UNICODE, EHFS_UNICODE_VERSION);
    ehfs_put32(sb + SB_NODE_SIZE, EHFS_NODE_SIZE);

    /* The bitmap: blocks 0 .. used_end - 1 and the last block are in use. */
    uint8_t *b = fs->block;
    int r = EHFS_OK;

    for (uint64_t i = 0; r == EHFS_OK && i < bitmap; i++)
    {
        uint64_t first = i * EHFS_BLOCK_SIZE * 8;

        k_memset(b, 0, EHFS_BLOCK_SIZE);
        for (uint64_t n = first; n < first + EHFS_BLOCK_SIZE * 8 && n < blocks; n++)
        {
            if (n < used_end || n == blocks - 1)
                b[(n - first) / 8] |= (uint8_t)(1u << (n % 8));
        }
        r = write_raw(fs, 1 + i, b);
    }

    /* The journal: header (no transaction yet) and an empty descriptor block. */
    if (r == EHFS_OK)
    {
        k_memset(b, 0, EHFS_BLOCK_SIZE);
        k_memcpy(b + JHDR_MAGIC, EHFS_JOURNAL_MAGIC, 8);
        ehfs_put32(b + JHDR_CRC, k_crc32c(0, b, JHDR_CRC));
        r = write_raw(fs, 1 + bitmap, b);
    }
    if (r == EHFS_OK)
    {
        k_memset(b, 0, EHFS_BLOCK_SIZE);
        r = write_raw(fs, 2 + bitmap, b);
    }

    /* The trees: a header node each (empty tree). */
    const uint64_t starts[3] = {catalog_start, extents_start, attributes_start};
    const uint64_t nodes[3] = {CATALOG_NODES, EXTENTS_NODES, ATTRIBUTE_NODES};
    uint8_t *node = fs->node[0];

    for (int t = 0; r == EHFS_OK && t < 3; t++)
    {
        ehfs_tree_header(node, nodes[t]);
        for (uint32_t k = 0; r == EHFS_OK && k < EHFS_NODE_BLOCKS; k++)
            r = write_raw(fs, starts[t] + k, node + k * EHFS_BLOCK_SIZE);
    }

    /* The superblock and its copy. */
    if (r == EHFS_OK)
    {
        k_memset(b, 0, EHFS_SB_OFFSET);
        k_memcpy(b + EHFS_SB_OFFSET, sb, EHFS_SB_SIZE);
        ehfs_put32(b + EHFS_SB_OFFSET + SB_CRC, k_crc32c(0, b + EHFS_SB_OFFSET, SB_CRC));
        r = write_raw(fs, blocks - 1, b);
        if (r == EHFS_OK)
            r = write_raw(fs, 0, b);
    }

    /* The root folder, through the journal like any other change. */
    if (r == EHFS_OK)
        r = ehfs_mount(fs, dev, start_sector, false);
    if (r == EHFS_OK)
        r = ehfs_begin(fs);
    if (r == EHFS_OK)
    {
        r = ehfs_cat_make_root(fs, (const uint8_t *)options->name, name_length, options->now);
        if (r == EHFS_OK)
            r = ehfs_commit(fs);
        else
            ehfs_abort(fs);
    }
    if (r == EHFS_OK)
        r = ehfs_unmount(fs);
    return r;
}
