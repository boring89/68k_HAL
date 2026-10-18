/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_FS_GPT_H
#define KERNEL_FS_GPT_H

#include <stdint.h>

#include "kernel/block/block.h"

/*
 * GUID Partition Table (UEFI Specification 2.10, section 5.3). The header is
 * at LBA 1, the entry array where the header says (normally LBA 2). The
 * device block size is the LBA size (512 or 4096). Integers are little-endian.
 */

#define GPT_MAX_PARTITIONS 32
#define GPT_NAME_SIZE 37 /* 36 UTF-16 units reduced to ASCII + NUL */

/* A GUID in its on-disk byte order (first three fields little-endian). */
typedef struct
{
    uint8_t b[16];
} gpt_guid_t;

typedef struct
{
    gpt_guid_t type;
    gpt_guid_t id;
    char name[GPT_NAME_SIZE];   /* non-ASCII characters become '?' */
    uint64_t start;             /* first LBA, on the whole device */
    uint64_t block_count;
    uint64_t attributes;
} gpt_partition_t;

/* Partition names the 68k_HAL disk layout uses (docs/mac_disk.md). */
#define GPT_NAME_BOOT "68k_HAL Boot"
#define GPT_NAME_SYSTEM_A "68k_HAL System A"
#define GPT_NAME_SYSTEM_B "68k_HAL System B"
#define GPT_NAME_OVERLAY "68k_HAL Overlay"
#define GPT_NAME_APPLICATIONS "Applications"

/* Apple HFS/HFS+ type GUID, and our own for the system overlay. */
#define GPT_TYPE_APPLE_HFS "48465300-0000-11AA-AA11-00306543ECAC"
#define GPT_TYPE_OVERLAY "68AC0001-4F56-4C59-8D1E-68A1D5C0DE01"

/*
 * Parse "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX" (the textual form) into the
 * on-disk byte order. Returns 0, or -1 if text is malformed.
 */
int gpt_guid_parse(const char *text, gpt_guid_t *guid);

/* 1 if the two GUIDs are equal. */
int gpt_guid_equal(const gpt_guid_t *a, const gpt_guid_t *b);

/* 1 if the GUID is all zero (an unused entry). */
int gpt_guid_is_zero(const gpt_guid_t *guid);

/*
 * Read the used entries of device's GPT into partitions (room for max).
 * The header signature, header CRC32 and entry array CRC32 are checked; if
 * the primary table (LBA 1) is damaged, the backup at the last block is used.
 * Returns the number of used entries (0 if the disk has no GPT), or -1 on
 * an I/O error or a damaged table. More than max used entries are cut.
 */
int gpt_read(block_device_t *device, gpt_partition_t *partitions, int max);

/*
 * The first partition whose name is name. Returns 0 and fills *partition, or
 * -1 if there is none (or gpt_read fails).
 */
int gpt_find_by_name(block_device_t *device, const char *name,
                     gpt_partition_t *partition);

/* The first partition whose type GUID is type_text (textual form). */
int gpt_find_by_type(block_device_t *device, const char *type_text,
                     gpt_partition_t *partition);

/*
 * Register a window of device as a new block device called name (a
 * partition seen as a disk, same block size). The caller provides the
 * storage for view and its block_device_t; both stay valid forever. Returns
 * 0, or -1 if the range is outside device or registration fails.
 */
typedef struct
{
    block_device_t device;
    block_device_t *parent;
    uint64_t start;
} gpt_view_t;

int gpt_view_register(gpt_view_t *view, const char *name,
                      block_device_t *parent, uint64_t start,
                      uint64_t block_count);

#endif
