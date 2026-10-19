/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_FS_EHFS_FILE_H
#define KERNEL_FS_EHFS_FILE_H

#include <stdint.h>

#include "catalog.h"
#include "ehfs.h"
#include "fork.h"

/*
 * The operations the kernel, the Mac side and the Linux tools use: each one
 * is complete in itself (its own transactions). Times are nanoseconds since
 * 1970 (UTC). `which` is EHFS_DATA_FORK or EHFS_RESOURCE_FORK.
 */

#define EHFS_ATTR_NAME_MAX 255u
#define EHFS_ATTR_VALUE_MAX 3072u   /* attribute values are kept in their record (docs/ehfs.md 4.4) */

int ehfs_create(ehfs_t *fs, uint64_t parent, const uint8_t *name, uint32_t length, bool is_dir, uint64_t now,
                ehfs_entry_t *entry);

/* Remove a file (its forks' blocks and attributes too) or an empty folder. */
int ehfs_remove(ehfs_t *fs, uint64_t parent, const uint8_t *name, uint32_t length, uint64_t now);

int ehfs_move(ehfs_t *fs, uint64_t parent, const uint8_t *name, uint32_t length, uint64_t new_parent,
              const uint8_t *new_name, uint32_t new_length, uint64_t now);

/* Write back flags, times and Finder info of an item (not its forks). */
int ehfs_set_info(ehfs_t *fs, const ehfs_entry_t *entry);

/* Read up to length bytes at offset; *done = how many (0 at or past the end). */
int ehfs_read(ehfs_t *fs, uint64_t cnid, int which, uint64_t offset, void *buffer, uint32_t length,
              uint32_t *done);

/* Write length bytes at offset (the fork grows; a gap after its old end reads as zeros). */
int ehfs_write(ehfs_t *fs, uint64_t cnid, int which, uint64_t offset, const void *buffer, uint32_t length,
               uint64_t now);

/* Make a fork `length` bytes long (blocks freed, or zeros added). */
int ehfs_set_length(ehfs_t *fs, uint64_t cnid, int which, uint64_t length, uint64_t now);

/* Extended attributes: names are UTF-8 (1..255 bytes, letters keep their case), values up to 3 KiB. */
int ehfs_attr_set(ehfs_t *fs, uint64_t cnid, const uint8_t *name, uint32_t name_length, const void *value,
                  uint32_t value_length);
int ehfs_attr_get(ehfs_t *fs, uint64_t cnid, const uint8_t *name, uint32_t name_length, void *value,
                  uint32_t size, uint32_t *value_length);
int ehfs_attr_remove(ehfs_t *fs, uint64_t cnid, const uint8_t *name, uint32_t name_length);
/* The name of attribute number index (from 0) of cnid. ENOENT past the last. */
int ehfs_attr_name(ehfs_t *fs, uint64_t cnid, uint32_t index, uint8_t *name, uint32_t *name_length);

/* fsck of the forks (runs, lengths, extents records) and the attributes. */
int ehfs_file_check(ehfs_t *fs, ehfs_check_t *report);

/* Call visit for every run of every file's forks (for the bitmap check). */
int ehfs_file_runs(ehfs_t *fs, ehfs_run_fn visit, void *ctx);

#endif
