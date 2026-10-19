/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_FS_EHFS_FORK_H
#define KERNEL_FS_EHFS_FORK_H

#include <stdint.h>

#include "ehfs.h"

/*
 * A file's forks (docs/ehfs.md section 4.3): the fork descriptor in the
 * catalog record holds the length, the blocks allocated and the first 8 runs
 * of blocks; the runs after them are in the extents tree, 8 per record, keyed
 * by (CNID, fork, the file block the record starts at). The functions that
 * change a fork run inside a transaction; the caller writes the descriptor
 * back to the catalog.
 */

#define EHFS_DATA_FORK 0
#define EHFS_RESOURCE_FORK 1

/* The device block of file block `index`, and how many file blocks from there on are contiguous. */
int ehfs_fork_map(ehfs_t *fs, const uint8_t *fork, uint64_t cnid, int which, uint64_t index, uint64_t *block,
                  uint64_t *run);

/* Give the fork `more` blocks after its last one (near it if possible). */
int ehfs_fork_grow(ehfs_t *fs, uint8_t *fork, uint64_t cnid, int which, uint64_t more);

/* Keep the first `keep` blocks of the fork and free the rest (with the extents records they used). */
int ehfs_fork_shrink(ehfs_t *fs, uint8_t *fork, uint64_t cnid, int which, uint64_t keep);

/*
 * For fsck: call visit for every run of the fork; *records = how many extents
 * records it has. ECORRUPT if the runs do not add up to its blocks.
 */
typedef int (*ehfs_run_fn)(void *ctx, uint64_t start, uint64_t count);
int ehfs_fork_runs(ehfs_t *fs, const uint8_t *fork, uint64_t cnid, int which, ehfs_run_fn visit, void *ctx,
                   uint64_t *records);

#endif
