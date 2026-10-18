/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "volume.h"

#include "apm.h"

#include "kernel/block/block.h"

static hfs_volume_t volume;
static bool mounted;

/* Try one device: its HFS partition if it has a partition map, else block 0. */
static int mount_device(block_device_t *device)
{
    apm_partition_t partition;

    if (apm_find(device, APM_TYPE_HFS, &partition) == 0)
        return hfs_mount(&volume, device, partition.start,
                         partition.block_count);

    return hfs_mount(&volume, device, 0, 0);
}

int volume_mount_any(void)
{
    for (unsigned int i = 0; i < block_device_count(); i++)
    {
        if (mount_device(block_device_get(i)) == 0)
        {
            mounted = true;

            return 0;
        }
    }

    return -1;
}

int volume_mount_device(block_device_t *device)
{
    mounted = device != NULL && mount_device(device) == 0;

    return mounted ? 0 : -1;
}

hfs_volume_t *volume_get(void)
{
    return mounted ? &volume : NULL;
}

void volume_unmount(void)
{
    mounted = false;
}
