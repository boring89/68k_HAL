/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_FS_EHFS_CATALOG_H
#define KERNEL_FS_EHFS_CATALOG_H

#include <stdbool.h>
#include <stdint.h>

#include "ehfs.h"
#include "name.h"

/*
 * The catalog (docs/ehfs.md section 4.2): one record per file or folder, keyed
 * by (parent CNID, comparison form of the name), and a thread record per item,
 * keyed by its own CNID, that leads back to (parent, name). The functions that
 * change it must run inside a transaction.
 */

typedef struct
{
    uint64_t cnid, parent;
    bool is_dir;
    uint16_t flags;                         /* bit 0: locked */
    uint64_t valence;                       /* folders: items inside */
    uint64_t times[EHFS_TIME_COUNT];        /* created, modified, attributes changed, accessed, backed up (ns) */
    uint8_t finder[32];                     /* FInfo+FXInfo (files) or DInfo+DXInfo (folders) */
    uint8_t data_fork[EHFS_FORK_SIZE];      /* files */
    uint8_t rsrc_fork[EHFS_FORK_SIZE];
    uint32_t name_length;
    uint8_t name[EHFS_NAME_MAX];            /* as it was given (UTF-8) */
} ehfs_entry_t;

/* The item called name in folder parent. ENOENT, ENAME (not a valid name). */
int ehfs_cat_lookup(ehfs_t *fs, uint64_t parent, const uint8_t *name, uint32_t length, ehfs_entry_t *entry);

/* The item with this CNID (through its thread). ENOENT. */
int ehfs_cat_get(ehfs_t *fs, uint64_t cnid, ehfs_entry_t *entry);

/*
 * Call fn for each item of folder parent, in the order of their comparison
 * forms, until it returns non-zero. fn must not change the volume.
 */
typedef int (*ehfs_cat_fn)(void *ctx, const ehfs_entry_t *entry);
int ehfs_cat_list(ehfs_t *fs, uint64_t parent, ehfs_cat_fn fn, void *ctx);

/* Make a new empty file or folder. EEXIST, ENOENT/ENOTDIR (no such folder), ENAME, ENOSPC (no CNID left). */
int ehfs_cat_create(ehfs_t *fs, uint64_t parent, const uint8_t *name, uint32_t length, bool is_dir, uint64_t now,
                    ehfs_entry_t *entry);

/* Write an item's record back (times, Finder info, flags, forks); its place and name stay. */
int ehfs_cat_update(ehfs_t *fs, const ehfs_entry_t *entry);

/*
 * Remove the record and thread of an item (a folder must be empty). The
 * blocks of a file's forks are the caller's business (ehfs_file_remove).
 */
int ehfs_cat_remove(ehfs_t *fs, uint64_t parent, const uint8_t *name, uint32_t length, uint64_t now);

/* Move and/or rename an item. EEXIST if the new place is taken; a folder cannot go into itself. */
int ehfs_cat_move(ehfs_t *fs, uint64_t parent, const uint8_t *name, uint32_t length, uint64_t new_parent,
                  const uint8_t *new_name, uint32_t new_length, uint64_t now);

/* Call fn for every file of the volume (in catalog order) until it returns non-zero (fsck). */
int ehfs_cat_each_file(ehfs_t *fs, ehfs_cat_fn fn, void *ctx);

/* For mkfs: the root folder and its thread. */
int ehfs_cat_make_root(ehfs_t *fs, const uint8_t *name, uint32_t length, uint64_t now);

/* fsck of the catalog: records and threads agree, valences, counts, CNIDs. */
int ehfs_cat_check(ehfs_t *fs, ehfs_check_t *report);

#endif
