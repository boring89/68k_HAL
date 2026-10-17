/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ramdisk.h"
#include "block.h"

#include "hal/boot.h"
#include "hal/mmu.h"

#include "kernel/lib/k_memory.h"

typedef struct
{
    block_device_t device;
    const uint8_t *data;
    char name[8];
} ramdisk_t;

static ramdisk_t disks[RAMDISK_MAX];
static unsigned int disk_total;

static int ramdisk_read(block_device_t *device, uint64_t lba, uint32_t count,
                        void *buffer)
{
    const ramdisk_t *disk = device->driver_data;

    k_memcpy(buffer, disk->data + lba * RAMDISK_BLOCK_SIZE,
             (size_t)count * RAMDISK_BLOCK_SIZE);

    return 0;
}

static const block_ops_t ramdisk_ops = {ramdisk_read, NULL};

int ramdisk_register(const char *name, const void *data, size_t size)
{
    if (name == NULL || data == NULL || disk_total == RAMDISK_MAX ||
        size < RAMDISK_BLOCK_SIZE)
        return -1;

    ramdisk_t *disk = &disks[disk_total];

    disk->data = data;
    disk->device.name = name;
    disk->device.block_size = RAMDISK_BLOCK_SIZE;
    disk->device.block_count = size / RAMDISK_BLOCK_SIZE;
    disk->device.ops = &ramdisk_ops;
    disk->device.driver_data = disk;

    if (block_register(&disk->device) != 0)
        return -1;

    disk_total++;

    return 0;
}

unsigned int ramdisk_register_boot_modules(void)
{
    unsigned int registered = 0;
    hal_boot_module_t module;

    for (unsigned int i = 0; hal_boot_module(i, &module) == 0; i++)
    {
        if (i >= RAMDISK_MAX)
            break;

        /* "ram" + the module number (at most one digit: RAMDISK_MAX < 10). */
        ramdisk_t *slot = &disks[disk_total];

        slot->name[0] = 'r';
        slot->name[1] = 'a';
        slot->name[2] = 'm';
        slot->name[3] = (char)('0' + i);
        slot->name[4] = '\0';

        if (ramdisk_register(slot->name, hal_phys_to_virt(module.base),
                             (size_t)module.length) == 0)
            registered++;
    }

    return registered;
}

void ramdisk_reset(void)
{
    disk_total = 0;
}
