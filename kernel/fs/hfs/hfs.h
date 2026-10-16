/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_FS_HFS_HFS_H
#define KERNEL_FS_HFS_HFS_H

#include <stdbool.h>
#include <stdint.h>

#include "kernel/block/block.h"

/*
 * Read-only HFS (the Macintosh Hierarchical File System, not HFS+). See
 * docs/hfs.md for the on-disk format and the decisions made here.
 *
 * Names are Mac Roman bytes (not ASCII above 0x7F). Paths separate folders
 * with ':'; they are relative to the root of the volume, and empty
 * components (a leading ':' or "::") are ignored.
 */

#define HFS_NAME_MAX 31
#define HFS_VOLUME_NAME_MAX 27

#define HFS_ROOT_ID 2 /* catalog node ID of the root folder (parent is 1) */

typedef struct
{
    uint16_t start;  /* first allocation block */
    uint16_t count;  /* number of allocation blocks */
} hfs_extent_t;

typedef struct
{
    block_device_t *device;
    uint64_t base;                      /* device block of the volume's block 0 */
    uint32_t alloc_block_size;          /* bytes per allocation block */
    uint32_t alloc_start;               /* first allocation block, in 512-byte blocks */
    uint16_t alloc_block_count;
    uint16_t free_alloc_blocks;
    uint32_t file_count;
    uint32_t folder_count;
    char name[HFS_VOLUME_NAME_MAX + 1];

    /* The two B*-trees (catalog and extents overflow), as plain files. */
    hfs_extent_t catalog_extents[3];
    uint32_t catalog_length;
    uint32_t catalog_first_leaf;
    hfs_extent_t overflow_extents[3];
    uint32_t overflow_length;
    uint32_t overflow_first_leaf;
} hfs_volume_t;

typedef struct
{
    char name[HFS_NAME_MAX + 1];
    bool is_folder;
    uint32_t id;            /* catalog node ID (file number or folder ID) */
    uint32_t parent_id;
    uint32_t file_type;     /* four-character code, e.g. 'TEXT' (files only) */
    uint32_t file_creator;
    uint32_t data_length;   /* logical length of the data fork (files only) */
    uint32_t rsrc_length;
    uint32_t valence;       /* number of items in the folder (folders only) */
    hfs_extent_t data_extents[3];
    hfs_extent_t rsrc_extents[3];
} hfs_entry_t;

typedef enum
{
    HFS_FORK_DATA,
    HFS_FORK_RESOURCE
} hfs_fork_kind_t;

/*
 * Mount the HFS volume that starts at device block start (and has
 * block_count blocks; 0 = unknown) on a device with 512-byte blocks.
 * Returns 0, or -1 if it is not a valid HFS volume or reading fails.
 */
int hfs_mount(hfs_volume_t *volume, block_device_t *device, uint64_t start,
              uint64_t block_count);

/* Find the file or folder at path. Returns 0, or -1 if there is none. */
int hfs_lookup(hfs_volume_t *volume, const char *path, hfs_entry_t *entry);

/*
 * Call fn for every item in folder folder_id, in catalog order (which is not
 * alphabetical for non-ASCII names). Stops when fn returns non-zero. Returns
 * 0, or -1 on a read error or a damaged catalog.
 */
typedef int (*hfs_list_fn)(const hfs_entry_t *entry, void *context);

int hfs_list(hfs_volume_t *volume, uint32_t folder_id, hfs_list_fn fn,
             void *context);

/*
 * Read up to length bytes of a fork of file entry, starting at offset.
 * Returns the number of bytes read (0 at or past the end), or -1 on error
 * (including entry being a folder).
 */
int hfs_read(hfs_volume_t *volume, const hfs_entry_t *entry,
             hfs_fork_kind_t fork, uint32_t offset, void *buffer,
             uint32_t length);

/* Compare two names the way hfs_lookup does (ASCII letters fold case). */
bool hfs_names_equal(const char *a, const char *b);

#endif
