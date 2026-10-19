/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Forks: runs of blocks (extents). The first 8 are in the fork descriptor;
 * the others in the extents tree, in records of 8 runs keyed by (CNID
 * big-endian, fork, the file block where the record's first run starts
 * big-endian). A fork's runs cover its file blocks in order with no gaps.
 */

#include "fork.h"

#include "btree.h"

#include "kernel/lib/k_memory.h"

#define KEY_LENGTH 17u
#define RECORD_LENGTH (EHFS_INLINE_EXTENTS * 16u)

static void put_be64(uint8_t *p, uint64_t v)
{
    for (int i = 7; i >= 0; i--)
    {
        p[i] = (uint8_t)v;
        v >>= 8;
    }
}

static uint64_t get_be64(const uint8_t *p)
{
    uint64_t v = 0;

    for (int i = 0; i < 8; i++)
        v = (v << 8) | p[i];
    return v;
}

static void make_key(uint8_t *key, uint64_t cnid, int which, uint64_t start)
{
    put_be64(key, cnid);
    key[8] = (uint8_t)which;
    put_be64(key + 9, start);
}

static uint64_t run_start(const uint8_t *runs, uint32_t i)
{
    return ehfs_le64(runs + 16 * i);
}

static uint64_t run_count(const uint8_t *runs, uint32_t i)
{
    return ehfs_le64(runs + 16 * i + 8);
}

static void set_run(uint8_t *runs, uint32_t i, uint64_t start, uint64_t count)
{
    ehfs_put64(runs + 16 * i, start);
    ehfs_put64(runs + 16 * i + 8, count);
}

/* The extents record of this fork with the greatest start <= index. ENOENT if none. */
static int find_record(ehfs_t *fs, uint64_t cnid, int which, uint64_t index, uint64_t *start, uint8_t *runs)
{
    uint8_t key[KEY_LENGTH], found[KEY_LENGTH];
    uint32_t found_length, value_length;
    int r;

    make_key(key, cnid, which, index);
    r = ehfs_bt_find_le(fs, EHFS_FORK_EXTENTS, key, KEY_LENGTH, found, KEY_LENGTH, &found_length, runs,
                        RECORD_LENGTH, &value_length);
    if (r != EHFS_OK)
        return r;
    if (found_length != KEY_LENGTH || k_memcmp(found, key, 9) != 0)
        return EHFS_ENOENT;             /* a record of another fork */
    if (value_length != RECORD_LENGTH)
        return EHFS_ECORRUPT;
    *start = get_be64(found + 9);
    return EHFS_OK;
}

int ehfs_fork_map(ehfs_t *fs, const uint8_t *fork, uint64_t cnid, int which, uint64_t index, uint64_t *block,
                  uint64_t *run)
{
    const uint8_t *runs = fork + FORK_EXTENT;
    uint64_t covered = 0;
    uint8_t record[RECORD_LENGTH];

    if (index >= ehfs_le64(fork + FORK_BLOCKS))
        return EHFS_EINVAL;
    for (uint32_t i = 0; i < EHFS_INLINE_EXTENTS && run_count(runs, i) != 0; i++)
    {
        if (index < covered + run_count(runs, i))
        {
            *block = run_start(runs, i) + (index - covered);
            *run = run_count(runs, i) - (index - covered);
            return EHFS_OK;
        }
        covered += run_count(runs, i);
    }

    int r = find_record(fs, cnid, which, index, &covered, record);

    if (r != EHFS_OK)
        return r == EHFS_ENOENT ? EHFS_ECORRUPT : r;
    for (uint32_t i = 0; i < EHFS_INLINE_EXTENTS && run_count(record, i) != 0; i++)
    {
        if (index < covered + run_count(record, i))
        {
            *block = run_start(record, i) + (index - covered);
            *run = run_count(record, i) - (index - covered);
            return EHFS_OK;
        }
        covered += run_count(record, i);
    }
    return EHFS_ECORRUPT;
}

static int last_used(const uint8_t *runs)
{
    int j = -1;

    for (uint32_t i = 0; i < EHFS_INLINE_EXTENTS && run_count(runs, i) != 0; i++)
        j = (int)i;
    return j;
}

int ehfs_fork_grow(ehfs_t *fs, uint8_t *fork, uint64_t cnid, int which, uint64_t more)
{
    uint8_t *inline_runs = fork + FORK_EXTENT;
    uint8_t record[RECORD_LENGTH], key[KEY_LENGTH];

    while (more > 0)
    {
        uint64_t record_start = 0, start, count;
        int r = find_record(fs, cnid, which, UINT64_MAX, &record_start, record);
        bool have_record = r == EHFS_OK;

        if (r != EHFS_OK && r != EHFS_ENOENT)
            return r;

        uint8_t *runs = have_record ? record : inline_runs;
        int j = last_used(runs);
        uint64_t end = j >= 0 ? run_start(runs, (uint32_t)j) + run_count(runs, (uint32_t)j) : fs->alloc_hint;

        if (have_record && j < 0)
            return EHFS_ECORRUPT;       /* records are never empty */
        r = ehfs_alloc(fs, end, more, &start, &count);
        if (r != EHFS_OK)
            return r;

        if (j >= 0 && start == end)
        {
            set_run(runs, (uint32_t)j, run_start(runs, (uint32_t)j), run_count(runs, (uint32_t)j) + count);
        }
        else if (j + 1 < (int)EHFS_INLINE_EXTENTS)
        {
            set_run(runs, (uint32_t)(j + 1), start, count);
        }
        else
        {
            /* A new record, starting at the file block this run will be. */
            k_memset(record, 0, sizeof record);
            set_run(record, 0, start, count);
            make_key(key, cnid, which, ehfs_le64(fork + FORK_BLOCKS));
            r = ehfs_bt_insert(fs, EHFS_FORK_EXTENTS, key, KEY_LENGTH, record, RECORD_LENGTH);
            if (r != EHFS_OK)
                return r;
            have_record = false;    /* written already */
        }
        if (have_record)
        {
            make_key(key, cnid, which, record_start);
            r = ehfs_bt_replace(fs, EHFS_FORK_EXTENTS, key, KEY_LENGTH, record, RECORD_LENGTH);
            if (r != EHFS_OK)
                return r;
        }
        ehfs_put64(fork + FORK_BLOCKS, ehfs_le64(fork + FORK_BLOCKS) + count);
        more -= count;
    }
    return EHFS_OK;
}

/* Cut runs (file blocks from `position` on) down to `keep` file blocks; returns whether any run is left. */
static int cut(ehfs_t *fs, uint8_t *runs, uint64_t position, uint64_t keep, bool *left)
{
    *left = false;
    for (uint32_t i = 0; i < EHFS_INLINE_EXTENTS && run_count(runs, i) != 0; i++)
    {
        uint64_t s = run_start(runs, i), c = run_count(runs, i);
        int r = EHFS_OK;

        if (position >= keep)
        {
            r = ehfs_free(fs, s, c);
            set_run(runs, i, 0, 0);
        }
        else if (position + c > keep)
        {
            r = ehfs_free(fs, s + (keep - position), position + c - keep);
            set_run(runs, i, s, keep - position);
            *left = true;
        }
        else
        {
            *left = true;
        }
        if (r != EHFS_OK)
            return r;
        position += c;
    }
    return EHFS_OK;
}

int ehfs_fork_shrink(ehfs_t *fs, uint8_t *fork, uint64_t cnid, int which, uint64_t keep)
{
    uint8_t record[RECORD_LENGTH], key[KEY_LENGTH];
    bool left;
    int r;

    if (keep >= ehfs_le64(fork + FORK_BLOCKS))
        return EHFS_OK;

    /* The extents records, the last first, while they start at or after `keep`, and then the one inside. */
    for (;;)
    {
        uint64_t start;

        r = find_record(fs, cnid, which, UINT64_MAX, &start, record);
        if (r == EHFS_ENOENT)
            break;
        if (r != EHFS_OK)
            return r;
        if (start < keep)
        {
            uint64_t sum = 0;

            for (uint32_t i = 0; i < EHFS_INLINE_EXTENTS; i++)
                sum += run_count(record, i);
            if (start + sum <= keep)
                break;          /* everything after `keep` was in later records */
        }
        r = cut(fs, record, start, keep, &left);
        if (r != EHFS_OK)
            return r;
        make_key(key, cnid, which, start);
        r = left ? ehfs_bt_replace(fs, EHFS_FORK_EXTENTS, key, KEY_LENGTH, record, RECORD_LENGTH)
                 : ehfs_bt_delete(fs, EHFS_FORK_EXTENTS, key, KEY_LENGTH);
        if (r != EHFS_OK)
            return r;
        if (left)
            break;
    }
    r = cut(fs, fork + FORK_EXTENT, 0, keep, &left);
    if (r != EHFS_OK)
        return r;
    ehfs_put64(fork + FORK_BLOCKS, keep);
    return EHFS_OK;
}

int ehfs_fork_runs(ehfs_t *fs, const uint8_t *fork, uint64_t cnid, int which, ehfs_run_fn visit, void *ctx,
                   uint64_t *records)
{
    const uint8_t *runs = fork + FORK_EXTENT;
    uint8_t key[KEY_LENGTH], found[KEY_LENGTH], record[RECORD_LENGTH];
    uint32_t found_length, value_length;
    uint64_t position = 0;
    bool ended = false;
    int r;

    *records = 0;
    for (uint32_t i = 0; i < EHFS_INLINE_EXTENTS; i++)
    {
        if (run_count(runs, i) == 0)
        {
            ended = true;
            continue;
        }
        if (ended)
            return EHFS_ECORRUPT;       /* a run after an empty slot */
        r = visit(ctx, run_start(runs, i), run_count(runs, i));
        if (r != EHFS_OK)
            return r;
        position += run_count(runs, i);
    }

    make_key(key, cnid, which, 0);
    for (bool inclusive = true;; inclusive = false)
    {
        r = ehfs_bt_next(fs, EHFS_FORK_EXTENTS, key, KEY_LENGTH, inclusive, found, KEY_LENGTH, &found_length, record,
                         RECORD_LENGTH, &value_length);
        if (r == EHFS_ENOENT)
            break;
        if (r != EHFS_OK)
            return r;
        if (found_length != KEY_LENGTH || k_memcmp(found, key, 9) != 0)
            break;
        if (value_length != RECORD_LENGTH || get_be64(found + 9) != position || run_count(record, 0) == 0)
            return EHFS_ECORRUPT;
        for (uint32_t i = 0; i < EHFS_INLINE_EXTENTS && run_count(record, i) != 0; i++)
        {
            r = visit(ctx, run_start(record, i), run_count(record, i));
            if (r != EHFS_OK)
                return r;
            position += run_count(record, i);
        }
        (*records)++;
        k_memcpy(key, found, KEY_LENGTH);
    }
    return position == ehfs_le64(fork + FORK_BLOCKS) ? EHFS_OK : EHFS_ECORRUPT;
}
