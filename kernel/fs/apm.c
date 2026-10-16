/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "apm.h"

#include "kernel/lib/k_string.h"

#define APM_BLOCK_SIZE 512

/* Entry layout: offsets of the fields (Inside Macintosh: Devices). */
#define PM_SIGNATURE 0x504D /* 'PM' */
#define PM_OFFSET_SIG 0
#define PM_OFFSET_MAP_BLOCKS 4
#define PM_OFFSET_START 8
#define PM_OFFSET_COUNT 12
#define PM_OFFSET_NAME 16
#define PM_OFFSET_TYPE 48
#define PM_OFFSET_STATUS 88

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(p[0] << 8 | p[1]);
}

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 |
           p[3];
}

/* Copy a 32-byte name field that may lack its NUL. */
static void copy_name(char out[APM_NAME_SIZE], const uint8_t *field)
{
    int i;

    for (i = 0; i < APM_NAME_SIZE - 1 && field[i] != 0; i++)
        out[i] = (char)field[i];

    out[i] = '\0';
}

int apm_parse_entry(const uint8_t block[512], apm_partition_t *partition)
{
    if (be16(block + PM_OFFSET_SIG) != PM_SIGNATURE)
        return -1;

    partition->start = be32(block + PM_OFFSET_START);
    partition->block_count = be32(block + PM_OFFSET_COUNT);
    partition->status = be32(block + PM_OFFSET_STATUS);
    copy_name(partition->name, block + PM_OFFSET_NAME);
    copy_name(partition->type, block + PM_OFFSET_TYPE);

    uint32_t map_blocks = be32(block + PM_OFFSET_MAP_BLOCKS);

    return map_blocks > 0x7FFFFFFF ? 0x7FFFFFFF : (int)map_blocks;
}

int apm_read(block_device_t *device, apm_partition_t *partitions, int max)
{
    uint8_t block[APM_BLOCK_SIZE];

    if (device == NULL || partitions == NULL || max <= 0 ||
        device->block_size != APM_BLOCK_SIZE)
        return -1;

    /* A disk too small to hold block 1 has no map. */
    if (device->block_count < 2)
        return 0;

    if (block_read(device, 1, 1, block) != 0)
        return -1;

    apm_partition_t first;
    int total = apm_parse_entry(block, &first);

    if (total < 0)
        return 0;   /* no partition map */

    /* The map cannot be longer than the disk. */
    if (total == 0 || (uint32_t)total > device->block_count - 1)
        return -1;

    int count = total < max ? total : max;

    for (int i = 0; i < count; i++)
    {
        if (block_read(device, 1 + (uint64_t)i, 1, block) != 0)
            return -1;

        /* Every entry carries the same map size; a different one is damage. */
        if (apm_parse_entry(block, &partitions[i]) != total)
            return -1;
    }

    return count;
}

int apm_find(block_device_t *device, const char *type,
             apm_partition_t *partition)
{
    apm_partition_t partitions[APM_MAX_PARTITIONS];

    if (type == NULL || partition == NULL)
        return -1;

    int count = apm_read(device, partitions, APM_MAX_PARTITIONS);

    for (int i = 0; i < count; i++)
    {
        if (k_strcmp(partitions[i].type, type) == 0)
        {
            *partition = partitions[i];
            return 0;
        }
    }

    return -1;
}
