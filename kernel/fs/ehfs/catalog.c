/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * The catalog: folder and file records keyed by (parent, comparison form of
 * the name), and thread records keyed by CNID (8 bytes, so a folder's thread
 * sorts just before its items). Integers in keys are big-endian; record
 * contents are little-endian (format.h).
 */

#include "catalog.h"

#include "btree.h"

#include "kernel/lib/k_memory.h"

#define KEY_MAX (8u + EHFS_KEY_MAX)
#define RECORD_MAX (REC_FILE_NAME + 2u + EHFS_NAME_MAX)
#define THREAD_MAX (THREAD_NAME + 2u + EHFS_NAME_MAX)

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

/* (parent, comparison form of name); its length, or a negative error. */
static int make_key(uint64_t parent, const uint8_t *name, uint32_t length, uint8_t *key)
{
    int n = ehfs_name_key(name, length, key + 8, EHFS_KEY_MAX);

    if (n < 0)
        return EHFS_ENAME;
    put_be64(key, parent);
    return 8 + n;
}

static uint32_t encode(const ehfs_entry_t *e, uint8_t *rec)
{
    uint32_t name_at = e->is_dir ? REC_FOLDER_NAME : REC_FILE_NAME;

    k_memset(rec, 0, name_at);
    ehfs_put16(rec + REC_TYPE, e->is_dir ? EHFS_REC_FOLDER : EHFS_REC_FILE);
    ehfs_put16(rec + REC_FLAGS, e->flags);
    ehfs_put64(rec + REC_CNID, e->cnid);
    ehfs_put64(rec + REC_VALENCE, e->is_dir ? e->valence : 0);
    for (int i = 0; i < EHFS_TIME_COUNT; i++)
        ehfs_put64(rec + REC_TIMES + 8 * i, e->times[i]);
    k_memcpy(rec + REC_FINDER, e->finder, 32);
    if (!e->is_dir)
    {
        k_memcpy(rec + REC_DATA_FORK, e->data_fork, EHFS_FORK_SIZE);
        k_memcpy(rec + REC_RSRC_FORK, e->rsrc_fork, EHFS_FORK_SIZE);
    }
    ehfs_put16(rec + name_at, (uint16_t)e->name_length);
    k_memcpy(rec + name_at + 2, e->name, e->name_length);
    return name_at + 2 + e->name_length;
}

static int decode(const uint8_t *rec, uint32_t length, uint64_t parent, ehfs_entry_t *e)
{
    uint32_t type, name_at;

    if (length < 4)
        return EHFS_ECORRUPT;
    type = ehfs_le16(rec + REC_TYPE);
    if (type != EHFS_REC_FOLDER && type != EHFS_REC_FILE)
        return EHFS_ECORRUPT;
    name_at = type == EHFS_REC_FOLDER ? REC_FOLDER_NAME : REC_FILE_NAME;
    if (length < name_at + 2 || ehfs_le16(rec + name_at) > EHFS_NAME_MAX ||
        length != name_at + 2u + ehfs_le16(rec + name_at))
        return EHFS_ECORRUPT;
    k_memset(e, 0, sizeof *e);
    e->is_dir = type == EHFS_REC_FOLDER;
    e->parent = parent;
    e->flags = ehfs_le16(rec + REC_FLAGS);
    e->cnid = ehfs_le64(rec + REC_CNID);
    e->valence = ehfs_le64(rec + REC_VALENCE);
    for (int i = 0; i < EHFS_TIME_COUNT; i++)
        e->times[i] = ehfs_le64(rec + REC_TIMES + 8 * i);
    k_memcpy(e->finder, rec + REC_FINDER, 32);
    if (!e->is_dir)
    {
        k_memcpy(e->data_fork, rec + REC_DATA_FORK, EHFS_FORK_SIZE);
        k_memcpy(e->rsrc_fork, rec + REC_RSRC_FORK, EHFS_FORK_SIZE);
    }
    e->name_length = ehfs_le16(rec + name_at);
    k_memcpy(e->name, rec + name_at + 2, e->name_length);
    return EHFS_OK;
}

static uint32_t encode_thread(uint64_t parent, bool is_dir, const uint8_t *name, uint32_t length, uint8_t *rec)
{
    ehfs_put16(rec + REC_TYPE, is_dir ? EHFS_REC_FOLDER_THREAD : EHFS_REC_FILE_THREAD);
    ehfs_put16(rec + REC_FLAGS, 0);
    ehfs_put64(rec + THREAD_PARENT, parent);
    ehfs_put16(rec + THREAD_NAME, (uint16_t)length);
    k_memcpy(rec + THREAD_NAME + 2, name, length);
    return THREAD_NAME + 2 + length;
}

int ehfs_cat_lookup(ehfs_t *fs, uint64_t parent, const uint8_t *name, uint32_t length, ehfs_entry_t *entry)
{
    uint8_t key[KEY_MAX], rec[RECORD_MAX];
    uint32_t got;
    int n = make_key(parent, name, length, key);

    if (n < 0)
        return n;

    int r = ehfs_bt_find(fs, EHFS_FORK_CATALOG, key, (uint32_t)n, rec, sizeof rec, &got);

    if (r != EHFS_OK)
        return r;
    if (got > sizeof rec)
        return EHFS_ECORRUPT;
    return decode(rec, got, parent, entry);
}

/* The thread of cnid: its parent and name. */
static int thread_of(ehfs_t *fs, uint64_t cnid, uint64_t *parent, uint8_t *name, uint32_t *length, bool *is_dir)
{
    uint8_t key[8], rec[THREAD_MAX];
    uint32_t got;
    int r;

    put_be64(key, cnid);
    r = ehfs_bt_find(fs, EHFS_FORK_CATALOG, key, 8, rec, sizeof rec, &got);
    if (r != EHFS_OK)
        return r;
    if (got < THREAD_NAME + 2 || got > sizeof rec || got != THREAD_NAME + 2u + ehfs_le16(rec + THREAD_NAME) ||
        (ehfs_le16(rec + REC_TYPE) != EHFS_REC_FOLDER_THREAD && ehfs_le16(rec + REC_TYPE) != EHFS_REC_FILE_THREAD))
        return EHFS_ECORRUPT;
    *parent = ehfs_le64(rec + THREAD_PARENT);
    *length = ehfs_le16(rec + THREAD_NAME);
    *is_dir = ehfs_le16(rec + REC_TYPE) == EHFS_REC_FOLDER_THREAD;
    k_memcpy(name, rec + THREAD_NAME + 2, *length);
    return EHFS_OK;
}

int ehfs_cat_get(ehfs_t *fs, uint64_t cnid, ehfs_entry_t *entry)
{
    uint8_t name[EHFS_NAME_MAX];
    uint32_t length;
    uint64_t parent;
    bool is_dir;
    int r = thread_of(fs, cnid, &parent, name, &length, &is_dir);

    if (r != EHFS_OK)
        return r;
    r = ehfs_cat_lookup(fs, parent, name, length, entry);
    if (r == EHFS_ENOENT || (r == EHFS_OK && entry->cnid != cnid))
        return EHFS_ECORRUPT;       /* a thread without its record */
    return r;
}

/* ---- Going through a folder (the B-tree walk cannot be nested, so one record at a time) ---- */

typedef struct
{
    const uint8_t *after;           /* the key to go past */
    uint32_t after_length;
    uint8_t key[KEY_MAX];
    uint32_t key_length;
    uint8_t value[RECORD_MAX];
    uint32_t value_length;
    bool found;
} next_t;

static int take_next(void *ctx, const uint8_t *key, uint32_t key_length, const uint8_t *value, uint32_t value_length)
{
    next_t *n = ctx;

    if (key_length == n->after_length && k_memcmp(key, n->after, key_length) == 0)
        return 0;                   /* the start itself */
    if (key_length > sizeof n->key || value_length > sizeof n->value)
        return EHFS_ECORRUPT;
    k_memcpy(n->key, key, key_length);
    n->key_length = key_length;
    k_memcpy(n->value, value, value_length);
    n->value_length = value_length;
    n->found = true;
    return 1;
}

/* The first record after key (or at it, if inclusive). */
static int next_record(ehfs_t *fs, const uint8_t *key, uint32_t length, bool inclusive, next_t *n)
{
    n->after = inclusive ? NULL : key;
    n->after_length = inclusive ? 0xFFFFFFFFu : length;
    n->found = false;

    int r = ehfs_bt_walk(fs, EHFS_FORK_CATALOG, key, length, take_next, n);

    return r < 0 ? r : EHFS_OK;
}

int ehfs_cat_list(ehfs_t *fs, uint64_t parent, ehfs_cat_fn fn, void *ctx)
{
    static next_t n;
    uint8_t start[KEY_MAX];
    uint32_t start_length = 8;
    ehfs_entry_t e;

    put_be64(start, parent);
    for (;;)
    {
        int r = next_record(fs, start, start_length, false, &n);

        if (r != EHFS_OK)
            return r;
        if (!n.found || n.key_length <= 8 || get_be64(n.key) != parent)
            return EHFS_OK;
        r = decode(n.value, n.value_length, parent, &e);
        if (r != EHFS_OK)
            return r;
        k_memcpy(start, n.key, n.key_length);
        start_length = n.key_length;
        r = fn(ctx, &e);
        if (r != 0)
            return r;
    }
}

/* ---- Changes ---- */

static int adjust_folder(ehfs_t *fs, uint64_t cnid, int delta, uint64_t now)
{
    ehfs_entry_t folder;
    int r = ehfs_cat_get(fs, cnid, &folder);

    if (r != EHFS_OK)
        return r;
    if (!folder.is_dir)
        return EHFS_ENOTDIR;
    folder.valence = (uint64_t)((int64_t)folder.valence + delta);
    folder.times[1] = now;
    return ehfs_cat_update(fs, &folder);
}

int ehfs_cat_update(ehfs_t *fs, const ehfs_entry_t *entry)
{
    uint8_t key[KEY_MAX], rec[RECORD_MAX];
    int n = make_key(entry->parent, entry->name, entry->name_length, key);

    if (n < 0)
        return n;
    return ehfs_bt_replace(fs, EHFS_FORK_CATALOG, key, (uint32_t)n, rec, encode(entry, rec));
}

static int insert_item(ehfs_t *fs, const ehfs_entry_t *e)
{
    uint8_t key[KEY_MAX], rec[RECORD_MAX];
    int n = make_key(e->parent, e->name, e->name_length, key);

    if (n < 0)
        return n;

    int r = ehfs_bt_insert(fs, EHFS_FORK_CATALOG, key, (uint32_t)n, rec, encode(e, rec));

    if (r == EHFS_OK)
    {
        put_be64(key, e->cnid);
        r = ehfs_bt_insert(fs, EHFS_FORK_CATALOG, key, 8, rec,
                           encode_thread(e->parent, e->is_dir, e->name, e->name_length, rec));
    }
    return r;
}

static int delete_item(ehfs_t *fs, const ehfs_entry_t *e)
{
    uint8_t key[KEY_MAX];
    int n = make_key(e->parent, e->name, e->name_length, key);

    if (n < 0)
        return n;

    int r = ehfs_bt_delete(fs, EHFS_FORK_CATALOG, key, (uint32_t)n);

    if (r == EHFS_OK)
    {
        put_be64(key, e->cnid);
        r = ehfs_bt_delete(fs, EHFS_FORK_CATALOG, key, 8);
    }
    return r;
}

int ehfs_cat_create(ehfs_t *fs, uint64_t parent, const uint8_t *name, uint32_t length, bool is_dir, uint64_t now,
                    ehfs_entry_t *entry)
{
    ehfs_entry_t folder, e;
    int r;

    if (!ehfs_name_valid(name, length))
        return EHFS_ENAME;
    r = ehfs_cat_get(fs, parent, &folder);
    if (r != EHFS_OK)
        return r;
    if (!folder.is_dir)
        return EHFS_ENOTDIR;
    r = ehfs_cat_lookup(fs, parent, name, length, &e);
    if (r == EHFS_OK)
        return EHFS_EEXIST;
    if (r != EHFS_ENOENT)
        return r;

    uint64_t cnid = ehfs_sb_get64(fs, SB_NEXT_CNID);

    if (cnid > EHFS_CNID_MAC_MAX)
        return EHFS_ENOSPC;         /* E5: the Mac's IDs are 32-bit */

    k_memset(&e, 0, sizeof e);
    e.cnid = cnid;
    e.parent = parent;
    e.is_dir = is_dir;
    e.times[0] = e.times[1] = e.times[2] = e.times[3] = now;
    e.name_length = length;
    k_memcpy(e.name, name, length);
    r = insert_item(fs, &e);
    if (r == EHFS_OK)
        r = adjust_folder(fs, parent, 1, now);
    if (r != EHFS_OK)
        return r;
    ehfs_sb_set64(fs, SB_NEXT_CNID, cnid + 1);
    ehfs_sb_set64(fs, is_dir ? SB_FOLDER_COUNT : SB_FILE_COUNT,
                  ehfs_sb_get64(fs, is_dir ? SB_FOLDER_COUNT : SB_FILE_COUNT) + 1);
    if (entry != NULL)
        *entry = e;
    return EHFS_OK;
}

int ehfs_cat_remove(ehfs_t *fs, uint64_t parent, const uint8_t *name, uint32_t length, uint64_t now)
{
    ehfs_entry_t e;
    int r = ehfs_cat_lookup(fs, parent, name, length, &e);

    if (r != EHFS_OK)
        return r;
    if (e.cnid == EHFS_CNID_ROOT)
        return EHFS_EINVAL;
    if (e.is_dir && e.valence != 0)
        return EHFS_ENOTEMPTY;
    r = delete_item(fs, &e);
    if (r == EHFS_OK)
        r = adjust_folder(fs, parent, -1, now);
    if (r != EHFS_OK)
        return r;
    ehfs_sb_set64(fs, e.is_dir ? SB_FOLDER_COUNT : SB_FILE_COUNT,
                  ehfs_sb_get64(fs, e.is_dir ? SB_FOLDER_COUNT : SB_FILE_COUNT) - 1);
    return EHFS_OK;
}

int ehfs_cat_move(ehfs_t *fs, uint64_t parent, const uint8_t *name, uint32_t length, uint64_t new_parent,
                  const uint8_t *new_name, uint32_t new_length, uint64_t now)
{
    ehfs_entry_t e, other, folder;
    int r;

    if (!ehfs_name_valid(new_name, new_length))
        return EHFS_ENAME;
    r = ehfs_cat_lookup(fs, parent, name, length, &e);
    if (r != EHFS_OK)
        return r;
    if (e.cnid == EHFS_CNID_ROOT)
        return EHFS_EINVAL;
    r = ehfs_cat_get(fs, new_parent, &folder);
    if (r != EHFS_OK)
        return r;
    if (!folder.is_dir)
        return EHFS_ENOTDIR;

    /* The new place: free, or this same item (a new spelling of its name). */
    r = ehfs_cat_lookup(fs, new_parent, new_name, new_length, &other);
    if (r == EHFS_OK && other.cnid != e.cnid)
        return EHFS_EEXIST;
    if (r != EHFS_OK && r != EHFS_ENOENT)
        return r;

    /* A folder cannot go into itself or a folder inside it. */
    if (e.is_dir)
    {
        uint64_t up = new_parent;

        for (int depth = 0; up != EHFS_CNID_ROOT_PARENT; depth++)
        {
            ehfs_entry_t step;

            if (up == e.cnid)
                return EHFS_EINVAL;
            if (depth > 100000 || ehfs_cat_get(fs, up, &step) != EHFS_OK)
                return EHFS_ECORRUPT;
            up = step.parent;
        }
    }

    r = delete_item(fs, &e);
    if (r != EHFS_OK)
        return r;
    e.parent = new_parent;
    e.name_length = new_length;
    k_memcpy(e.name, new_name, new_length);
    e.times[2] = now;
    r = insert_item(fs, &e);
    if (r == EHFS_OK && new_parent != parent)
    {
        r = adjust_folder(fs, parent, -1, now);
        if (r == EHFS_OK)
            r = adjust_folder(fs, new_parent, 1, now);
    }
    else if (r == EHFS_OK)
    {
        r = adjust_folder(fs, parent, 0, now);
    }
    return r;
}

int ehfs_cat_each_file(ehfs_t *fs, ehfs_cat_fn fn, void *ctx)
{
    static next_t n;
    uint8_t start[KEY_MAX];
    uint32_t start_length = 0;
    bool inclusive = true;
    ehfs_entry_t e;

    for (;;)
    {
        int r = next_record(fs, start, start_length, inclusive, &n);

        if (r != EHFS_OK)
            return r;
        inclusive = false;
        if (!n.found)
            return EHFS_OK;
        k_memcpy(start, n.key, n.key_length);
        start_length = n.key_length;
        if (n.key_length <= 8 || ehfs_le16(n.value + REC_TYPE) != EHFS_REC_FILE)
            continue;
        r = decode(n.value, n.value_length, get_be64(n.key), &e);
        if (r != EHFS_OK)
            return r;
        r = fn(ctx, &e);
        if (r != 0)
            return r;
    }
}

int ehfs_cat_make_root(ehfs_t *fs, const uint8_t *name, uint32_t length, uint64_t now)
{
    ehfs_entry_t e;

    k_memset(&e, 0, sizeof e);
    e.cnid = EHFS_CNID_ROOT;
    e.parent = EHFS_CNID_ROOT_PARENT;
    e.is_dir = true;
    e.times[0] = e.times[1] = e.times[2] = e.times[3] = now;
    e.name_length = length;
    k_memcpy(e.name, name, length);
    return insert_item(fs, &e);
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

static int count_items(void *ctx, const uint8_t *key, uint32_t key_length, const uint8_t *value,
                       uint32_t value_length)
{
    uint64_t *count = ctx;

    (void)value;
    (void)value_length;
    if (key_length < 8 || get_be64(key) != count[1])
        return 1;               /* past the folder's items */
    if (key_length > 8)
        count[0]++;             /* (the folder's own thread comes first) */
    return 0;
}

int ehfs_cat_check(ehfs_t *fs, ehfs_check_t *report)
{
    static next_t n;
    static uint8_t start[KEY_MAX];
    uint32_t start_length = 0;
    uint64_t files = 0, folders = 0, threads = 0, items = 0;
    uint64_t next_cnid = ehfs_sb_get64(fs, SB_NEXT_CNID);
    bool inclusive = true;

    for (;;)
    {
        if (next_record(fs, start, start_length, inclusive, &n) != EHFS_OK)
        {
            problem(report, "the catalog cannot be read");
            return EHFS_OK;
        }
        inclusive = false;
        if (!n.found)
            break;
        k_memcpy(start, n.key, n.key_length);
        start_length = n.key_length;

        if (n.key_length == 8)
        {
            /* A thread: its record must be where it says. */
            uint64_t cnid = get_be64(n.key);
            ehfs_entry_t e;

            threads++;
            if (n.value_length < THREAD_NAME + 2 ||
                n.value_length != THREAD_NAME + 2u + ehfs_le16(n.value + THREAD_NAME))
            {
                problem(report, "a catalog thread is damaged");
                continue;
            }

            uint64_t parent = ehfs_le64(n.value + THREAD_PARENT);

            if (cnid < EHFS_CNID_ROOT || cnid >= next_cnid)
                problem(report, "a catalog thread's CNID is out of range");
            if (ehfs_cat_lookup(fs, parent, n.value + THREAD_NAME + 2, ehfs_le16(n.value + THREAD_NAME), &e) !=
                    EHFS_OK ||
                e.cnid != cnid ||
                e.is_dir != (ehfs_le16(n.value + REC_TYPE) == EHFS_REC_FOLDER_THREAD))
                problem(report, "a catalog thread does not lead to its record");
            continue;
        }

        /* A file or folder record. */
        ehfs_entry_t e;
        uint64_t parent = get_be64(n.key);
        uint8_t key[KEY_MAX];

        items++;
        if (decode(n.value, n.value_length, parent, &e) != EHFS_OK)
        {
            problem(report, "a catalog record is damaged");
            continue;
        }
        int kl = make_key(parent, e.name, e.name_length, key);

        if (kl < 0 || (uint32_t)kl != n.key_length || k_memcmp(key, n.key, n.key_length) != 0)
            problem(report, "a catalog record's key does not match its name");
        if (e.cnid == EHFS_CNID_ROOT ? parent != EHFS_CNID_ROOT_PARENT || !e.is_dir
                                     : e.cnid < EHFS_CNID_FIRST || e.cnid >= next_cnid)
            problem(report, "a catalog record's CNID is out of range");
        if (e.cnid > EHFS_CNID_MAC_MAX)
            problem(report, "a CNID does not fit the Mac's 32 bits");

        ehfs_entry_t back;

        if (ehfs_cat_get(fs, e.cnid, &back) != EHFS_OK || back.parent != parent)
            problem(report, "a catalog record has no thread back to it");
        if (e.cnid != EHFS_CNID_ROOT)
        {
            ehfs_entry_t up;

            if (ehfs_cat_get(fs, parent, &up) != EHFS_OK || !up.is_dir)
                problem(report, "a catalog record's parent is not a folder");
        }
        if (e.is_dir)
        {
            uint64_t count[2] = {0, e.cnid};
            uint8_t from[8];

            if (e.cnid != EHFS_CNID_ROOT)
                folders++;
            put_be64(from, e.cnid);
            if (ehfs_bt_walk(fs, EHFS_FORK_CATALOG, from, 8, count_items, count) < 0 || count[0] != e.valence)
                problem(report, "a folder's item count is wrong");
        }
        else
        {
            files++;
        }
    }
    if (threads != items)
        problem(report, "the catalog has more threads than records, or fewer");
    if (files != ehfs_sb_get64(fs, SB_FILE_COUNT) || folders != ehfs_sb_get64(fs, SB_FOLDER_COUNT))
        problem(report, "the volume's file or folder count is wrong");
    return EHFS_OK;
}
