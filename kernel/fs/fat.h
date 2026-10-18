/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_FS_FAT_H
#define KERNEL_FS_FAT_H

#include <stdint.h>

#include "kernel/block/block.h"

/*
 * Just enough FAT32 to find where a small existing file is on the disk, so
 * that its sectors can be rewritten in place (the GRUB environment block on
 * the Boot partition). Nothing is allocated, created or resized: writing the
 * sectors of a file keeps its size. Layout: Microsoft "FAT: General Overview
 * of On-Disk Format" (boot sector BPB, FAT entries, 32-byte directory
 * entries with 8.3 names).
 */

#define FAT_FILE_MAX_SECTORS 16

typedef struct
{
    uint32_t size;                          /* bytes */
    uint32_t sector_count;                  /* sectors holding the file */
    uint64_t sector[FAT_FILE_MAX_SECTORS];  /* their numbers on the partition, in order */
} fat_file_t;

/*
 * Find path ("68k_hal/grubenv": folders separated by '/', each part a valid
 * 8.3 name, compared without regard to case) on the FAT32 volume that fills
 * device. Returns 0 and fills *file, or -1 if the volume is not FAT32 with
 * 512-byte sectors, a part is missing, or the file needs more than
 * FAT_FILE_MAX_SECTORS sectors.
 */
int fat_find(block_device_t *device, const char *path, fat_file_t *file);

/* Read the file (file->size bytes, at most buffer_size). Returns bytes read, or -1. */
int fat_read(block_device_t *device, const fat_file_t *file, void *buffer, uint32_t buffer_size);

/*
 * Overwrite the file with exactly file->size bytes from data (the rest of
 * its last sector is kept). Returns 0, or -1 on an I/O error.
 */
int fat_overwrite(block_device_t *device, const fat_file_t *file, const void *data);

#endif
