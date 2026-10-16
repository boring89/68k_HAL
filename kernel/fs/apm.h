/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_FS_APM_H
#define KERNEL_FS_APM_H

#include <stdint.h>

#include "kernel/block/block.h"

/*
 * Apple Partition Map: how a classic Mac disk is divided into partitions
 * (Inside Macintosh: Devices, SCSI Manager chapter). Block 0 holds the
 * Driver Descriptor Map ("ER", optional here), blocks 1.. hold one 512-byte
 * entry ("PM") per partition. All numbers are big-endian. Only disks whose
 * block size is 512 are supported.
 */

#define APM_MAX_PARTITIONS 32
#define APM_NAME_SIZE 33 /* 32 characters + NUL */

#define APM_TYPE_HFS "Apple_HFS"

typedef struct
{
    char name[APM_NAME_SIZE];
    char type[APM_NAME_SIZE];   /* e.g. "Apple_HFS", "Apple_Driver43" */
    uint32_t start;             /* first block, on the whole device */
    uint32_t block_count;
    uint32_t status;            /* pmPartStatus bits */
} apm_partition_t;

/*
 * Decode one partition map entry from a 512-byte block. Returns the number
 * of entries the map says it has (pmMapBlkCnt), or -1 if the block is not
 * an entry ("PM" signature missing).
 */
int apm_parse_entry(const uint8_t block[512], apm_partition_t *partition);

/*
 * Read the partition map of device into partitions (room for max entries).
 * Returns how many entries were read (0 if the disk has no partition map),
 * or -1 on an I/O error, an unsupported block size or a damaged map. A map
 * with more than max entries is cut at max.
 */
int apm_read(block_device_t *device, apm_partition_t *partitions, int max);

/*
 * The first partition of type type, found by reading the map. Returns 0 and
 * fills *partition, or -1 if there is none (or apm_read fails).
 */
int apm_find(block_device_t *device, const char *type,
             apm_partition_t *partition);

#endif
