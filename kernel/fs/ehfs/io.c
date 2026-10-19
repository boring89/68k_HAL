/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Blocks, transactions and the journal of eHFS (docs/ehfs.md section 5).
 *
 * The journal (CNID 7) is one contiguous run of blocks: a header block that
 * holds the number of the last transaction written home, then room for one
 * transaction: a descriptor block (which blocks it changes), their new
 * contents, and a commit block whose CRC32C covers the descriptor and the
 * contents. ehfs_commit writes the transaction, then every block home, then
 * the header. ehfs_mount finds a complete transaction newer than the header
 * and writes it home again (writing the same contents twice is harmless).
 * The device's writes are durable when they return (kernel/block/block.h).
 */

#include "ehfs.h"

#include "kernel/lib/k_crc32c.h"
#include "kernel/lib/k_memory.h"

uint64_t ehfs_le64(const uint8_t *p)
{
    return (uint64_t)ehfs_le32(p) | ((uint64_t)ehfs_le32(p + 4) << 32);
}

uint32_t ehfs_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint16_t ehfs_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

void ehfs_put64(uint8_t *p, uint64_t v)
{
    ehfs_put32(p, (uint32_t)v);
    ehfs_put32(p + 4, (uint32_t)(v >> 32));
}

void ehfs_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

void ehfs_put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

uint64_t ehfs_sb_get64(const ehfs_t *fs, uint32_t offset)
{
    return ehfs_le64(fs->sb + offset);
}

void ehfs_sb_set64(ehfs_t *fs, uint32_t offset, uint64_t value)
{
    ehfs_put64(fs->sb + offset, value);
    fs->sb_dirty = true;
}

uint32_t ehfs_sb_get32(const ehfs_t *fs, uint32_t offset)
{
    return ehfs_le32(fs->sb + offset);
}

void ehfs_sb_set32(ehfs_t *fs, uint32_t offset, uint32_t value)
{
    ehfs_put32(fs->sb + offset, value);
    fs->sb_dirty = true;
}

uint8_t *ehfs_sb_fork(ehfs_t *fs, int which)
{
    return fs->sb + SB_FORKS + (uint32_t)which * EHFS_FORK_SIZE;
}

/* ---- The device ---- */

static int dev_read(ehfs_t *fs, uint64_t n, void *buffer)
{
    if (n >= fs->blocks)
        return EHFS_EINVAL;
    return block_read(fs->dev, fs->start + n * EHFS_SECTORS_PER_BLOCK, EHFS_SECTORS_PER_BLOCK, buffer) == 0
               ? EHFS_OK
               : EHFS_EIO;
}

static int dev_write(ehfs_t *fs, uint64_t n, const void *buffer)
{
    if (n >= fs->blocks)
        return EHFS_EINVAL;
    if (fs->crash_after == 0)
        return EHFS_EIO;        /* a test's power failure: nothing more reaches the disk */
    if (fs->crash_after > 0)
        fs->crash_after--;
    return block_write(fs->dev, fs->start + n * EHFS_SECTORS_PER_BLOCK, EHFS_SECTORS_PER_BLOCK, buffer) == 0
               ? EHFS_OK
               : EHFS_EIO;
}

static int txn_find(const ehfs_t *fs, uint64_t n)
{
    for (uint32_t i = 0; i < fs->txn_count; i++)
    {
        if (fs->txn_target[i] == n)
            return (int)i;
    }
    return -1;
}

int ehfs_read_block(ehfs_t *fs, uint64_t n, void *buffer)
{
    int i = txn_find(fs, n);

    if (i >= 0)
    {
        k_memcpy(buffer, fs->txn_data[i], EHFS_BLOCK_SIZE);
        return EHFS_OK;
    }
    return dev_read(fs, n, buffer);
}

int ehfs_write_meta(ehfs_t *fs, uint64_t n, const void *buffer)
{
    if (fs->read_only)
        return EHFS_EROFS;
    if (!fs->in_txn || n >= fs->blocks)
        return EHFS_EINVAL;

    int i = txn_find(fs, n);

    if (i < 0)
    {
        if (fs->txn_count == EHFS_TXN_BLOCKS)
            return EHFS_ETXN;
        i = (int)fs->txn_count++;
        fs->txn_target[i] = n;
    }
    k_memcpy(fs->txn_data[i], buffer, EHFS_BLOCK_SIZE);
    return EHFS_OK;
}

int ehfs_write_data(ehfs_t *fs, uint64_t n, const void *buffer)
{
    if (fs->read_only)
        return EHFS_EROFS;
    return dev_write(fs, n, buffer);
}

/* ---- The superblock ---- */

static void sb_seal(uint8_t *sb)
{
    ehfs_put32(sb + SB_CRC, k_crc32c(0, sb, SB_CRC));
}

static int sb_valid(const uint8_t *sb)
{
    if (k_memcmp(sb + SB_MAGIC, EHFS_MAGIC, 8) != 0 || ehfs_le32(sb + SB_CRC) != k_crc32c(0, sb, SB_CRC))
        return EHFS_ECORRUPT;
    if (ehfs_le32(sb + SB_VERSION) != EHFS_VERSION || ehfs_le32(sb + SB_BLOCK_SIZE) != EHFS_BLOCK_SIZE ||
        ehfs_le32(sb + SB_INCOMPAT) != 0 || ehfs_le32(sb + SB_NODE_SIZE) != EHFS_NODE_SIZE)
        return EHFS_EUNSUPPORTED;
    return EHFS_OK;
}

/* Block 0 as it is to be written: 1024 reserved bytes, then the sealed superblock. */
static void sb_block(ehfs_t *fs, uint8_t *block)
{
    k_memset(block, 0, EHFS_SB_OFFSET);
    k_memcpy(block + EHFS_SB_OFFSET, fs->sb, EHFS_SB_SIZE);
    sb_seal(block + EHFS_SB_OFFSET);
}

/* ---- Transactions ---- */

int ehfs_begin(ehfs_t *fs)
{
    if (fs->read_only)
        return EHFS_EROFS;
    if (fs->in_txn)
        return EHFS_EINVAL;
    fs->in_txn = true;
    fs->sb_dirty = false;
    fs->txn_count = 0;
    return EHFS_OK;
}

static uint64_t journal_start(ehfs_t *fs)
{
    return ehfs_le64(ehfs_sb_fork(fs, EHFS_FORK_JOURNAL) + FORK_EXTENT);
}

static uint64_t journal_blocks(ehfs_t *fs)
{
    return ehfs_le64(ehfs_sb_fork(fs, EHFS_FORK_JOURNAL) + FORK_EXTENT + 8);
}

static int write_journal_header(ehfs_t *fs, uint64_t sequence)
{
    uint8_t *b = fs->block;

    k_memset(b, 0, EHFS_BLOCK_SIZE);
    k_memcpy(b + JHDR_MAGIC, EHFS_JOURNAL_MAGIC, 8);
    ehfs_put64(b + JHDR_SEQUENCE, sequence);
    ehfs_put32(b + JHDR_CRC, k_crc32c(0, b, JHDR_CRC));
    return dev_write(fs, journal_start(fs), b);
}

int ehfs_commit(ehfs_t *fs)
{
    int r;

    if (!fs->in_txn)
        return EHFS_EINVAL;
    if (fs->sb_dirty)
    {
        sb_block(fs, fs->block);
        r = ehfs_write_meta(fs, 0, fs->block);
        if (r != EHFS_OK)
        {
            ehfs_abort(fs);
            return r;
        }
    }
    if (fs->txn_count == 0)
    {
        fs->in_txn = false;
        return EHFS_OK;
    }
    if (fs->txn_count > EHFS_TXN_MAX || fs->txn_count + 3 > journal_blocks(fs))
    {
        ehfs_abort(fs);
        return EHFS_ETXN;
    }

    uint64_t j = journal_start(fs);
    uint64_t sequence = fs->journal_sequence + 1;
    uint8_t *b = fs->block;
    uint32_t crc;

    /* 1. The transaction in the journal: descriptor, contents, commit. */
    k_memset(b, 0, EHFS_BLOCK_SIZE);
    k_memcpy(b + TXN_MAGIC, EHFS_TXN_MAGIC, 8);
    ehfs_put64(b + TXN_SEQUENCE, sequence);
    ehfs_put32(b + TXN_COUNT, fs->txn_count);
    for (uint32_t i = 0; i < fs->txn_count; i++)
        ehfs_put64(b + TXN_TARGETS + 8 * i, fs->txn_target[i]);
    crc = k_crc32c(0, b, EHFS_BLOCK_SIZE);
    r = dev_write(fs, j + 1, b);
    for (uint32_t i = 0; r == EHFS_OK && i < fs->txn_count; i++)
    {
        crc = k_crc32c(crc, fs->txn_data[i], EHFS_BLOCK_SIZE);
        r = dev_write(fs, j + 2 + i, fs->txn_data[i]);
    }
    if (r == EHFS_OK)
    {
        k_memset(b, 0, EHFS_BLOCK_SIZE);
        k_memcpy(b + COMMIT_MAGIC, EHFS_COMMIT_MAGIC, 8);
        ehfs_put64(b + COMMIT_SEQUENCE, sequence);
        ehfs_put32(b + COMMIT_COUNT, fs->txn_count);
        ehfs_put32(b + COMMIT_CRC, crc);
        r = dev_write(fs, j + 2 + fs->txn_count, b);
    }

    /* 2. Every block home; 3. the header: done. */
    for (uint32_t i = 0; r == EHFS_OK && i < fs->txn_count; i++)
        r = dev_write(fs, fs->txn_target[i], fs->txn_data[i]);
    if (r == EHFS_OK)
        r = write_journal_header(fs, sequence);
    if (r != EHFS_OK)
    {
        /* The disk is in some state the next mount repairs; this mount must not go on. */
        fs->in_txn = false;
        fs->txn_count = 0;
        fs->read_only = true;
        return r;
    }
    fs->journal_sequence = sequence;
    fs->txn_count = 0;
    fs->in_txn = false;
    fs->sb_dirty = false;
    return EHFS_OK;
}

void ehfs_abort(ehfs_t *fs)
{
    fs->txn_count = 0;
    fs->in_txn = false;
    if (fs->sb_dirty && dev_read(fs, 0, fs->block) == EHFS_OK)
        k_memcpy(fs->sb, fs->block + EHFS_SB_OFFSET, EHFS_SB_SIZE);
    fs->sb_dirty = false;
}

/*
 * A transaction newer than the journal's header, complete and intact? Its
 * blocks are then loaded into the transaction buffer (txn_count > 0).
 */
static int journal_pending(ehfs_t *fs, uint64_t *sequence)
{
    uint64_t j = journal_start(fs), size = journal_blocks(fs);
    uint8_t *b = fs->block;
    uint32_t count, crc;

    fs->txn_count = 0;
    if (size < 4 || j + size > fs->blocks)
        return EHFS_ECORRUPT;
    if (dev_read(fs, j, b) != EHFS_OK)
        return EHFS_EIO;
    if (k_memcmp(b + JHDR_MAGIC, EHFS_JOURNAL_MAGIC, 8) != 0 || ehfs_le32(b + JHDR_CRC) != k_crc32c(0, b, JHDR_CRC))
        return EHFS_ECORRUPT;
    fs->journal_sequence = ehfs_le64(b + JHDR_SEQUENCE);
    *sequence = fs->journal_sequence + 1;

    if (dev_read(fs, j + 1, b) != EHFS_OK)
        return EHFS_EIO;
    count = ehfs_le32(b + TXN_COUNT);
    if (k_memcmp(b + TXN_MAGIC, EHFS_TXN_MAGIC, 8) != 0 || ehfs_le64(b + TXN_SEQUENCE) != *sequence ||
        count == 0 || count > EHFS_TXN_BLOCKS || count + 3 > size)
        return EHFS_OK;         /* nothing (or a transaction already written home) */
    crc = k_crc32c(0, b, EHFS_BLOCK_SIZE);
    for (uint32_t i = 0; i < count; i++)
    {
        fs->txn_target[i] = ehfs_le64(b + TXN_TARGETS + 8 * i);
        if (fs->txn_target[i] >= fs->blocks)
            return EHFS_OK;
    }
    for (uint32_t i = 0; i < count; i++)
    {
        if (dev_read(fs, j + 2 + i, fs->txn_data[i]) != EHFS_OK)
            return EHFS_EIO;
        crc = k_crc32c(crc, fs->txn_data[i], EHFS_BLOCK_SIZE);
    }
    if (dev_read(fs, j + 2 + count, b) != EHFS_OK)
        return EHFS_EIO;
    if (k_memcmp(b + COMMIT_MAGIC, EHFS_COMMIT_MAGIC, 8) != 0 || ehfs_le64(b + COMMIT_SEQUENCE) != *sequence ||
        ehfs_le32(b + COMMIT_COUNT) != count || ehfs_le32(b + COMMIT_CRC) != crc)
        return EHFS_OK;         /* the crash came before the commit block: the transaction never happened */
    fs->txn_count = count;
    return EHFS_OK;
}

static int load_sb(ehfs_t *fs)
{
    int r = ehfs_read_block(fs, 0, fs->block);

    if (r != EHFS_OK)
        return r;
    k_memcpy(fs->sb, fs->block + EHFS_SB_OFFSET, EHFS_SB_SIZE);
    return sb_valid(fs->sb);
}

int ehfs_mount(ehfs_t *fs, block_device_t *dev, uint64_t start_sector, bool read_only)
{
    uint64_t sequence;
    int r;

    if (fs == NULL || dev == NULL || dev->block_size != 512)
        return EHFS_EINVAL;
    k_memset(fs, 0, sizeof *fs);
    fs->dev = dev;
    fs->start = start_sector;
    fs->blocks = 1;
    fs->crash_after = -1;
    fs->read_only = read_only;

    r = load_sb(fs);
    if (r != EHFS_OK)
        return r;
    fs->blocks = ehfs_sb_get64(fs, SB_BLOCK_COUNT);
    if (fs->blocks < 16 || start_sector + fs->blocks * EHFS_SECTORS_PER_BLOCK > dev->block_count)
        return EHFS_ECORRUPT;

    r = journal_pending(fs, &sequence);
    if (r != EHFS_OK)
        return r;
    if (fs->txn_count > 0)
    {
        if (read_only)
        {
            /* Not written home, but read through: the transaction's blocks stay in the buffer. */
            r = load_sb(fs);
            if (r != EHFS_OK)
                return r;
        }
        else
        {
            for (uint32_t i = 0; i < fs->txn_count; i++)
            {
                r = dev_write(fs, fs->txn_target[i], fs->txn_data[i]);
                if (r != EHFS_OK)
                    return r;
            }
            r = write_journal_header(fs, sequence);
            if (r != EHFS_OK)
                return r;
            fs->journal_sequence = sequence;
            fs->txn_count = 0;
            r = load_sb(fs);
            if (r != EHFS_OK)
                return r;
        }
    }

    if (!read_only)
    {
        r = ehfs_begin(fs);
        if (r == EHFS_OK)
        {
            ehfs_sb_set32(fs, SB_STATE, EHFS_STATE_MOUNTED);
            r = ehfs_commit(fs);
        }
    }
    return r;
}

int ehfs_unmount(ehfs_t *fs)
{
    int r;

    if (fs->read_only)
        return EHFS_OK;
    r = ehfs_begin(fs);
    if (r != EHFS_OK)
        return r;
    ehfs_sb_set32(fs, SB_STATE, EHFS_STATE_CLEAN);
    return ehfs_commit(fs);
}
