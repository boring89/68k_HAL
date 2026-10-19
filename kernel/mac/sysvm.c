/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysvm.h"

#include <stdbool.h>
#include <stdint.h>

#include "rsrc.h"

#include "kernel/fs/apm.h"
#include "kernel/fs/hfs/hfs.h"

#define BLOCK_SIZE 512
#define SYSTEM_FILE_TYPE MAC_RSRC_TYPE('z', 's', 'y', 's')
#define VM_TYPE MAC_RSRC_TYPE('v', 'm', ' ', ' ')
#define VM_ID 0
#define VM_FLAG_SIZE 4      /* the first word: 0 = off, 1 = on */

typedef struct
{
    hfs_volume_t *volume;
    hfs_entry_t entry;
    bool found;
} system_search_t;

static int find_system(const hfs_entry_t *entry, void *context)
{
    system_search_t *search = context;

    if (entry->is_folder || entry->file_type != SYSTEM_FILE_TYPE)
        return 0;

    search->entry = *entry;
    search->found = true;

    return 1;
}

static int read_fork(void *context, uint32_t offset, void *buffer,
                     uint32_t length)
{
    system_search_t *search = context;

    return hfs_read(search->volume, &search->entry, HFS_FORK_RESOURCE, offset,
                    buffer, length) == (int)length
               ? 0
               : -1;
}

/* Write 0 over one byte of the System file's resource fork. */
static int clear_byte(system_search_t *search, uint32_t offset)
{
    uint8_t block[BLOCK_SIZE];
    uint64_t lba;
    uint32_t inner;
    block_device_t *device = search->volume->device;

    if (hfs_fork_locate(search->volume, &search->entry, HFS_FORK_RESOURCE,
                        offset, &lba, &inner) != 0 ||
        block_read(device, lba, 1, block) != 0)
        return -1;

    block[inner] = 0;

    return block_write(device, lba, 1, block);
}

int sysvm_turn_off(block_device_t *disk)
{
    hfs_volume_t volume;
    apm_partition_t partition;
    system_search_t search;

    if (disk == NULL || disk->block_size != BLOCK_SIZE)
        return 0;

    search.volume = &volume;
    search.found = false;

    int mounted = apm_find(disk, APM_TYPE_HFS, &partition) == 0
                      ? hfs_mount(&volume, disk, partition.start,
                                  partition.block_count)
                      : hfs_mount(&volume, disk, 0, 0);

    if (mounted != 0 || volume.blessed_folder == 0)
        return 0;

    if (hfs_list(&volume, volume.blessed_folder, find_system, &search) < 0)
        return -1;
    if (!search.found)
        return 0;

    uint32_t offset, length;
    int status = mac_rsrc_find(read_fork, &search, search.entry.rsrc_length,
                               VM_TYPE, VM_ID, &offset, &length);

    if (status != 0)
        return status < 0 ? -1 : 0;
    if (length < VM_FLAG_SIZE)
        return -1;

    uint8_t flag[VM_FLAG_SIZE];

    if (read_fork(&search, offset, flag, VM_FLAG_SIZE) != 0)
        return -1;

    bool on = false;

    for (uint32_t i = 0; i < VM_FLAG_SIZE; i++)
        on = on || flag[i] != 0;

    if (!on)
        return 0;
    if (disk->ops->write == NULL)
        return -1;

    for (uint32_t i = 0; i < VM_FLAG_SIZE; i++)
    {
        if (flag[i] != 0 && clear_byte(&search, offset + i) != 0)
            return -1;
    }

    return 1;
}
