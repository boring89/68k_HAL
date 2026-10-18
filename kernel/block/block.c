/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "block.h"

#include "cache.h"

#include "kernel/lib/k_string.h"

static block_device_t *devices[BLOCK_MAX_DEVICES];
static unsigned int device_total;

int block_register(block_device_t *device)
{
    if (device == NULL || device->name == NULL || device->ops == NULL ||
        device->ops->read == NULL || device->block_size == 0)
        return -1;

    if (device_total == BLOCK_MAX_DEVICES ||
        block_device_find(device->name) != NULL)
        return -1;

    devices[device_total++] = device;

    return 0;
}

unsigned int block_device_count(void)
{
    return device_total;
}

block_device_t *block_device_get(unsigned int index)
{
    return index < device_total ? devices[index] : NULL;
}

block_device_t *block_device_find(const char *name)
{
    if (name == NULL)
        return NULL;

    for (unsigned int i = 0; i < device_total; i++)
    {
        if (k_strcmp(devices[i]->name, name) == 0)
            return devices[i];
    }

    return NULL;
}

int block_read(block_device_t *device, uint64_t lba, uint32_t count,
               void *buffer)
{
    if (device == NULL || buffer == NULL || count == 0)
        return -1;

    /* Written so that lba + count cannot overflow. */
    if (lba >= device->block_count || count > device->block_count - lba)
        return -1;

    if (block_cache_enabled(device))
        return block_cache_read(device, lba, count, buffer);

    return device->ops->read(device, lba, count, buffer);
}

int block_write(block_device_t *device, uint64_t lba, uint32_t count,
                const void *buffer)
{
    if (device == NULL || buffer == NULL || count == 0 || device->ops->write == NULL)
        return -1;

    if (lba >= device->block_count || count > device->block_count - lba)
        return -1;

    if (block_cache_enabled(device))
        return block_cache_write(device, lba, count, buffer);

    return device->ops->write(device, lba, count, buffer);
}

void block_reset(void)
{
    device_total = 0;
    block_cache_reset();
}
