/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_FS_EHFS_BTREE_H
#define KERNEL_FS_EHFS_BTREE_H

#include "ehfs.h"

/*
 * The B+-trees of eHFS (catalog, extents, attributes; docs/ehfs.md section
 * 4): records of (key, value) in 16 KiB nodes, keys ordered by plain byte
 * comparison (shorter first when one is the start of the other). Changes go
 * through the transaction (ehfs_begin .. ehfs_commit).
 *
 * `tree` is EHFS_FORK_CATALOG, EHFS_FORK_EXTENTS or EHFS_FORK_ATTRIBUTES.
 */

#define EHFS_BT_KEY_MAX 1100u       /* bytes of a key */
#define EHFS_BT_VALUE_MAX 4096u     /* bytes of a value */

/* The value of `key` (at most value_size bytes copied; *value_length = its real length). ENOENT if none. */
int ehfs_bt_find(ehfs_t *fs, int tree, const uint8_t *key, uint32_t key_length, void *value, uint32_t value_size,
                 uint32_t *value_length);

/* Add a record. EEXIST if the key is there already. */
int ehfs_bt_insert(ehfs_t *fs, int tree, const uint8_t *key, uint32_t key_length, const void *value,
                   uint32_t value_length);

/* Change the value of a record. ENOENT if the key is not there. */
int ehfs_bt_replace(ehfs_t *fs, int tree, const uint8_t *key, uint32_t key_length, const void *value,
                    uint32_t value_length);

/* Remove a record. ENOENT if the key is not there. */
int ehfs_bt_delete(ehfs_t *fs, int tree, const uint8_t *key, uint32_t key_length);

/*
 * Call visit for every record whose key is >= start, in order, until it
 * returns non-zero. The buffers given to visit are valid during the call
 * only; visit must not change the tree. Returns 0, or visit's non-zero value,
 * or an error.
 */
typedef int (*ehfs_bt_visit_t)(void *ctx, const uint8_t *key, uint32_t key_length, const uint8_t *value,
                               uint32_t value_length);
int ehfs_bt_walk(ehfs_t *fs, int tree, const uint8_t *start, uint32_t start_length, ehfs_bt_visit_t visit,
                 void *ctx);

/* Records in the tree (the header's count). */
uint64_t ehfs_bt_records(ehfs_t *fs, int tree);

/*
 * fsck of one tree: every node reachable from the root, sorted keys within
 * the parent's bounds, the leaf chain, the header's counts and node map.
 * Problems are counted in report. Calls owner for the tree's own blocks too.
 */
int ehfs_bt_check(ehfs_t *fs, int tree, ehfs_check_t *report);

#endif
