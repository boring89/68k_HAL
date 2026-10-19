/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * File operations, each in its own transactions. Writing is "ordered": the
 * data goes to its blocks first, then the transaction that gives the blocks
 * to the file (and changes its length) commits. Blocks a fork gets are filled
 * with zeros where nothing is written, so no old data shows. Large writes,
 * truncations and removals are split into transactions of at most CHUNK
 * blocks; a crash between them leaves a consistent, partly done result.
 */

#include "file.h"

#include "btree.h"

#include "kernel/lib/k_memory.h"

#define CHUNK 256u                  /* file blocks per transaction */
#define ATTR_HEADER 6u              /* u16 type (1 = inline), u32 length */
#define ATTR_KEY_MAX (8u + EHFS_ATTR_NAME_MAX)

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

static uint64_t blocks_for(uint64_t length)
{
    return length / EHFS_BLOCK_SIZE + (length % EHFS_BLOCK_SIZE != 0);
}

static int finish(ehfs_t *fs, int r)
{
    if (r == EHFS_OK)
        return ehfs_commit(fs);
    ehfs_abort(fs);
    return r;
}

/* ---- Catalog operations ---- */

int ehfs_create(ehfs_t *fs, uint64_t parent, const uint8_t *name, uint32_t length, bool is_dir, uint64_t now,
                ehfs_entry_t *entry)
{
    int r = ehfs_begin(fs);

    if (r != EHFS_OK)
        return r;
    return finish(fs, ehfs_cat_create(fs, parent, name, length, is_dir, now, entry));
}

int ehfs_move(ehfs_t *fs, uint64_t parent, const uint8_t *name, uint32_t length, uint64_t new_parent,
              const uint8_t *new_name, uint32_t new_length, uint64_t now)
{
    int r = ehfs_begin(fs);

    if (r != EHFS_OK)
        return r;
    return finish(fs, ehfs_cat_move(fs, parent, name, length, new_parent, new_name, new_length, now));
}

int ehfs_set_info(ehfs_t *fs, const ehfs_entry_t *entry)
{
    ehfs_entry_t e;
    int r = ehfs_begin(fs);

    if (r != EHFS_OK)
        return r;
    r = ehfs_cat_get(fs, entry->cnid, &e);
    if (r == EHFS_OK)
    {
        e.flags = entry->flags;
        k_memcpy(e.times, entry->times, sizeof e.times);
        k_memcpy(e.finder, entry->finder, sizeof e.finder);
        r = ehfs_cat_update(fs, &e);
    }
    return finish(fs, r);
}

/* ---- Attributes ---- */

static uint32_t attr_key(uint64_t cnid, const uint8_t *name, uint32_t length, uint8_t *key)
{
    put_be64(key, cnid);
    k_memcpy(key + 8, name, length);
    return 8 + length;
}

static bool attr_name_ok(const uint8_t *name, uint32_t length)
{
    if (length == 0 || length > EHFS_ATTR_NAME_MAX)
        return false;
    for (uint32_t i = 0; i < length; i++)
    {
        if (name[i] == 0)
            return false;
    }
    return true;
}

int ehfs_attr_set(ehfs_t *fs, uint64_t cnid, const uint8_t *name, uint32_t name_length, const void *value,
                  uint32_t value_length)
{
    uint8_t key[ATTR_KEY_MAX], record[ATTR_HEADER + EHFS_ATTR_VALUE_MAX];
    ehfs_entry_t e;
    int r;

    if (!attr_name_ok(name, name_length) || value_length > EHFS_ATTR_VALUE_MAX)
        return EHFS_EINVAL;
    r = ehfs_begin(fs);
    if (r != EHFS_OK)
        return r;
    r = ehfs_cat_get(fs, cnid, &e);
    if (r == EHFS_OK)
    {
        uint32_t k = attr_key(cnid, name, name_length, key);

        ehfs_put16(record, 1);
        ehfs_put32(record + 2, value_length);
        k_memcpy(record + ATTR_HEADER, value, value_length);
        r = ehfs_bt_replace(fs, EHFS_FORK_ATTRIBUTES, key, k, record, ATTR_HEADER + value_length);
        if (r == EHFS_ENOENT)
            r = ehfs_bt_insert(fs, EHFS_FORK_ATTRIBUTES, key, k, record, ATTR_HEADER + value_length);
    }
    return finish(fs, r);
}

int ehfs_attr_get(ehfs_t *fs, uint64_t cnid, const uint8_t *name, uint32_t name_length, void *value,
                  uint32_t size, uint32_t *value_length)
{
    uint8_t key[ATTR_KEY_MAX], record[ATTR_HEADER + EHFS_ATTR_VALUE_MAX];
    uint32_t got;
    int r;

    if (!attr_name_ok(name, name_length))
        return EHFS_EINVAL;
    r = ehfs_bt_find(fs, EHFS_FORK_ATTRIBUTES, key, attr_key(cnid, name, name_length, key), record, sizeof record,
                     &got);
    if (r != EHFS_OK)
        return r;
    if (got < ATTR_HEADER || got > sizeof record || ehfs_le32(record + 2) != got - ATTR_HEADER)
        return EHFS_ECORRUPT;
    *value_length = got - ATTR_HEADER;
    k_memcpy(value, record + ATTR_HEADER, *value_length < size ? *value_length : size);
    return EHFS_OK;
}

int ehfs_attr_remove(ehfs_t *fs, uint64_t cnid, const uint8_t *name, uint32_t name_length)
{
    uint8_t key[ATTR_KEY_MAX];
    int r;

    if (!attr_name_ok(name, name_length))
        return EHFS_EINVAL;
    r = ehfs_begin(fs);
    if (r != EHFS_OK)
        return r;
    return finish(fs, ehfs_bt_delete(fs, EHFS_FORK_ATTRIBUTES, key, attr_key(cnid, name, name_length, key)));
}

/* The first attribute of cnid at or after key; ENOENT when cnid has no more. */
static int next_attr(ehfs_t *fs, uint64_t cnid, uint8_t *key, uint32_t *key_length, bool inclusive)
{
    uint8_t found[ATTR_KEY_MAX];
    uint32_t found_length;
    int r = ehfs_bt_next(fs, EHFS_FORK_ATTRIBUTES, key, *key_length, inclusive, found, sizeof found, &found_length,
                         NULL, 0xFFFFFFFFu, NULL);

    if (r != EHFS_OK)
        return r;
    if (found_length <= 8 || get_be64(found) != cnid)
        return EHFS_ENOENT;
    k_memcpy(key, found, found_length);
    *key_length = found_length;
    return EHFS_OK;
}

int ehfs_attr_name(ehfs_t *fs, uint64_t cnid, uint32_t index, uint8_t *name, uint32_t *name_length)
{
    uint8_t key[ATTR_KEY_MAX];
    uint32_t key_length = 8;
    int r;

    put_be64(key, cnid);
    for (uint32_t i = 0; i <= index; i++)
    {
        r = next_attr(fs, cnid, key, &key_length, i == 0);
        if (r != EHFS_OK)
            return r;
    }
    *name_length = key_length - 8;
    k_memcpy(name, key + 8, *name_length);
    return EHFS_OK;
}

/* ---- Reading and writing ---- */

static uint8_t *fork_of(ehfs_entry_t *e, int which)
{
    return which == EHFS_RESOURCE_FORK ? e->rsrc_fork : e->data_fork;
}

int ehfs_read(ehfs_t *fs, uint64_t cnid, int which, uint64_t offset, void *buffer, uint32_t length,
              uint32_t *done)
{
    ehfs_entry_t e;
    uint8_t *out = buffer;
    int r = ehfs_cat_get(fs, cnid, &e);

    *done = 0;
    if (r != EHFS_OK)
        return r;
    if (e.is_dir)
        return EHFS_EISDIR;

    uint8_t *fork = fork_of(&e, which);
    uint64_t size = ehfs_le64(fork + FORK_LENGTH);

    if (offset >= size)
        return EHFS_OK;
    if (length > size - offset)
        length = (uint32_t)(size - offset);
    while (*done < length)
    {
        uint64_t pos = offset + *done, block, run;
        uint32_t inner = (uint32_t)(pos % EHFS_BLOCK_SIZE);
        uint32_t n = EHFS_BLOCK_SIZE - inner;

        if (n > length - *done)
            n = length - *done;
        r = ehfs_fork_map(fs, fork, cnid, which, pos / EHFS_BLOCK_SIZE, &block, &run);
        if (r == EHFS_OK)
            r = ehfs_read_block(fs, block, fs->block);
        if (r != EHFS_OK)
            return r;
        k_memcpy(out + *done, fs->block + inner, n);
        *done += n;
    }
    return EHFS_OK;
}

/*
 * Bytes [offset, offset + length) of a fork get data (or zeros when data is
 * NULL). File blocks from first_new on were just given to the fork: what is
 * not written in them is zero.
 */
static int put_range(ehfs_t *fs, const uint8_t *fork, uint64_t cnid, int which, uint64_t offset,
                     const uint8_t *data, uint32_t length, uint64_t first_new)
{
    uint32_t done = 0;

    while (done < length)
    {
        uint64_t pos = offset + done, index = pos / EHFS_BLOCK_SIZE, block, run;
        uint32_t inner = (uint32_t)(pos % EHFS_BLOCK_SIZE);
        uint32_t n = EHFS_BLOCK_SIZE - inner;
        int r;

        if (n > length - done)
            n = length - done;
        r = ehfs_fork_map(fs, fork, cnid, which, index, &block, &run);
        if (r != EHFS_OK)
            return r;
        if (n < EHFS_BLOCK_SIZE)
        {
            if (index >= first_new)
                k_memset(fs->block, 0, EHFS_BLOCK_SIZE);
            else if ((r = ehfs_read_block(fs, block, fs->block)) != EHFS_OK)
                return r;
        }
        if (data != NULL)
            k_memcpy(fs->block + inner, data + done, n);
        else
            k_memset(fs->block + inner, 0, n);
        r = ehfs_write_data(fs, block, fs->block);
        if (r != EHFS_OK)
            return r;
        done += n;
    }
    return EHFS_OK;
}

/*
 * One transaction's worth of a write: the bytes from offset on (at most CHUNK
 * blocks of them). A gap after the old end is zeroed first, in its own steps.
 * *used = bytes of data consumed (0 while zeroing a gap).
 */
static int write_step(ehfs_t *fs, uint64_t cnid, int which, uint64_t offset, const uint8_t *data, uint32_t length,
                      uint64_t now, uint32_t *used)
{
    ehfs_entry_t e;
    int r = ehfs_begin(fs);

    *used = 0;
    if (r != EHFS_OK)
        return r;
    r = ehfs_cat_get(fs, cnid, &e);
    if (r == EHFS_OK && e.is_dir)
        r = EHFS_EISDIR;
    if (r != EHFS_OK)
        return finish(fs, r);

    uint8_t *fork = fork_of(&e, which);
    uint64_t size = ehfs_le64(fork + FORK_LENGTH);
    uint64_t blocks = ehfs_le64(fork + FORK_BLOCKS);
    uint64_t start = offset, end;
    const uint8_t *source = data;

    if (offset > size)
    {
        start = size;                   /* zeros up to the data first */
        source = NULL;
        end = offset;
    }
    else
    {
        end = offset + length;
    }
    if (end - start > (uint64_t)CHUNK * EHFS_BLOCK_SIZE)
        end = start + (uint64_t)CHUNK * EHFS_BLOCK_SIZE;
    if (end < start)
        return finish(fs, EHFS_EINVAL);

    uint64_t need = blocks_for(end > size ? end : size);

    if (need > blocks)
        r = ehfs_fork_grow(fs, fork, cnid, which, need - blocks);
    if (r == EHFS_OK)
        r = put_range(fs, fork, cnid, which, start, source, (uint32_t)(end - start), blocks);
    if (r == EHFS_OK)
    {
        if (end > size)
            ehfs_put64(fork + FORK_LENGTH, end);
        e.times[1] = now;
        r = ehfs_cat_update(fs, &e);
    }
    if (source != NULL)
        *used = (uint32_t)(end - start);
    return finish(fs, r);
}

int ehfs_write(ehfs_t *fs, uint64_t cnid, int which, uint64_t offset, const void *buffer, uint32_t length,
               uint64_t now)
{
    const uint8_t *data = buffer;
    uint32_t done = 0;

    if (offset + length < offset)
        return EHFS_EINVAL;
    while (done < length)
    {
        uint32_t used;
        int r = write_step(fs, cnid, which, offset + done, data + done, length - done, now, &used);

        if (r != EHFS_OK)
            return r;
        done += used;
    }
    return EHFS_OK;
}

int ehfs_set_length(ehfs_t *fs, uint64_t cnid, int which, uint64_t length, uint64_t now)
{
    for (;;)
    {
        ehfs_entry_t e;
        int r = ehfs_cat_get(fs, cnid, &e);

        if (r != EHFS_OK)
            return r;
        if (e.is_dir)
            return EHFS_EISDIR;

        uint8_t *fork = fork_of(&e, which);
        uint64_t size = ehfs_le64(fork + FORK_LENGTH);

        if (length == size)
            return EHFS_OK;
        if (length > size)
        {
            /* Longer: zeros, the way a write past the end adds them. */
            uint32_t used;

            r = write_step(fs, cnid, which, length, NULL, 0, now, &used);
            if (r != EHFS_OK)
                return r;
            continue;
        }

        /*
         * Shorter: give back the blocks after the new end, CHUNK at a time; until the last step the
         * length is what the kept blocks hold.
         */
        uint64_t blocks = ehfs_le64(fork + FORK_BLOCKS), target = blocks_for(length);
        uint64_t keep = blocks - target > CHUNK ? blocks - CHUNK : target;

        r = ehfs_begin(fs);
        if (r != EHFS_OK)
            return r;
        r = ehfs_fork_shrink(fs, fork, cnid, which, keep);
        if (r == EHFS_OK)
        {
            ehfs_put64(fork + FORK_LENGTH, keep == target ? length : keep * EHFS_BLOCK_SIZE);
            e.times[1] = now;
            r = ehfs_cat_update(fs, &e);
        }
        r = finish(fs, r);
        if (r != EHFS_OK)
            return r;
    }
}

int ehfs_remove(ehfs_t *fs, uint64_t parent, const uint8_t *name, uint32_t length, uint64_t now)
{
    ehfs_entry_t e;
    int r = ehfs_cat_lookup(fs, parent, name, length, &e);

    if (r != EHFS_OK)
        return r;
    if (e.is_dir && e.valence != 0)
        return EHFS_ENOTEMPTY;
    if (!e.is_dir)
    {
        /* The forks' blocks, then the attributes, then the record: each step leaves a consistent volume. */
        for (int which = EHFS_DATA_FORK; which <= EHFS_RESOURCE_FORK; which++)
        {
            r = ehfs_set_length(fs, e.cnid, which, 0, now);
            if (r != EHFS_OK)
                return r;
        }
    }
    for (;;)
    {
        uint8_t key[ATTR_KEY_MAX];
        uint32_t key_length = 8;

        put_be64(key, e.cnid);
        r = next_attr(fs, e.cnid, key, &key_length, true);
        if (r == EHFS_ENOENT)
            break;
        if (r != EHFS_OK)
            return r;
        r = ehfs_begin(fs);
        if (r != EHFS_OK)
            return r;
        r = finish(fs, ehfs_bt_delete(fs, EHFS_FORK_ATTRIBUTES, key, key_length));
        if (r != EHFS_OK)
            return r;
    }
    r = ehfs_begin(fs);
    if (r != EHFS_OK)
        return r;
    return finish(fs, ehfs_cat_remove(fs, parent, name, length, now));
}

/* ---- Checking ---- */

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

typedef struct
{
    ehfs_t *fs;
    ehfs_check_t *report;           /* NULL: only visit the runs */
    ehfs_run_fn visit;
    void *ctx;
    uint64_t records;
    int error;
} file_walk_t;

static int ignore_run(void *ctx, uint64_t start, uint64_t count)
{
    (void)ctx;
    (void)start;
    (void)count;
    return EHFS_OK;
}

static int each_fork(void *ctx, const ehfs_entry_t *e)
{
    file_walk_t *w = ctx;
    ehfs_entry_t copy = *e;

    for (int which = EHFS_DATA_FORK; which <= EHFS_RESOURCE_FORK; which++)
    {
        const uint8_t *fork = fork_of(&copy, which);
        uint64_t records = 0;
        int r = ehfs_fork_runs(w->fs, fork, e->cnid, which, w->visit != NULL ? w->visit : ignore_run, w->ctx,
                               &records);

        if (r != EHFS_OK)
        {
            if (w->report == NULL)
            {
                w->error = r;
                return 1;
            }
            problem(w->report, "a file's runs do not add up to its blocks");
            continue;
        }
        if (w->report != NULL && blocks_for(ehfs_le64(fork + FORK_LENGTH)) != ehfs_le64(fork + FORK_BLOCKS))
            problem(w->report, "a file's length does not match its blocks");
        w->records += records;
    }
    return 0;
}

int ehfs_file_runs(ehfs_t *fs, ehfs_run_fn visit, void *ctx)
{
    file_walk_t w = {fs, NULL, visit, ctx, 0, EHFS_OK};
    int r = ehfs_cat_each_file(fs, each_fork, &w);

    return r < 0 ? r : w.error;
}

int ehfs_file_check(ehfs_t *fs, ehfs_check_t *report)
{
    file_walk_t w = {fs, report, NULL, NULL, 0, EHFS_OK};
    uint8_t key[ATTR_KEY_MAX], record[ATTR_HEADER + EHFS_ATTR_VALUE_MAX];
    uint32_t key_length = 0, value_length;

    if (ehfs_cat_each_file(fs, each_fork, &w) < 0)
        problem(report, "the files cannot be read");
    else if (w.records != ehfs_bt_records(fs, EHFS_FORK_EXTENTS))
        problem(report, "the extents tree has records of no file's fork");

    /* Every attribute belongs to an item and is well formed. */
    for (bool inclusive = true;; inclusive = false)
    {
        ehfs_entry_t e;
        int r = ehfs_bt_next(fs, EHFS_FORK_ATTRIBUTES, key, key_length, inclusive, key, sizeof key, &key_length,
                             record, sizeof record, &value_length);

        if (r == EHFS_ENOENT)
            break;
        if (r != EHFS_OK)
        {
            problem(report, "the attributes cannot be read");
            break;
        }
        if (key_length <= 8 || ehfs_cat_get(fs, get_be64(key), &e) != EHFS_OK)
            problem(report, "an attribute belongs to no file or folder");
        if (value_length < ATTR_HEADER || ehfs_le16(record) != 1 ||
            ehfs_le32(record + 2) != value_length - ATTR_HEADER)
            problem(report, "an attribute is damaged");
    }
    return EHFS_OK;
}
