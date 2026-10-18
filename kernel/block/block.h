/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_BLOCK_BLOCK_H
#define KERNEL_BLOCK_BLOCK_H

#include <stddef.h>
#include <stdint.h>

/*
 * Block devices: storage that is read in fixed-size blocks (disks, RAM
 * disks). Drivers fill in a block_device_t and register it; file systems
 * read through block_read and never see the driver.
 */

#define BLOCK_MAX_DEVICES 16

typedef struct block_device block_device_t;

typedef struct
{
    /*
     * Read count blocks starting at block lba into buffer (count *
     * block_size bytes). The range has been checked by block_read. Returns
     * 0, or -1 on an I/O error.
     */
    int (*read)(block_device_t *device, uint64_t lba, uint32_t count,
                void *buffer);

    /*
     * Write count blocks from buffer starting at block lba, and return only
     * when the data is on the medium (not just in the drive's cache). NULL for
     * a read-only device. The range has been checked by block_write. Returns
     * 0, or -1 on an I/O error.
     */
    int (*write)(block_device_t *device, uint64_t lba, uint32_t count,
                 const void *buffer);
} block_ops_t;

struct block_device
{
    const char *name;       /* unique, e.g. "ram0"; must stay valid */
    uint32_t block_size;    /* bytes per block, > 0 */
    uint64_t block_count;   /* number of blocks */
    const block_ops_t *ops;
    void *driver_data;      /* for the driver's own use */
};

/*
 * Add a device. The structure must stay valid and unchanged for as long as
 * it is registered (forever, for now). Returns 0, or -1 if device, its name
 * or its ops are NULL, block_size is 0, the name is already taken or the
 * table is full.
 */
int block_register(block_device_t *device);

/* Number of registered devices. */
unsigned int block_device_count(void);

/* Device index in registration order, or NULL if index is out of range. */
block_device_t *block_device_get(unsigned int index);

/* The device called name, or NULL. */
block_device_t *block_device_find(const char *name);

/*
 * Read count blocks from lba. Returns 0, or -1 if an argument is NULL or
 * zero, the range does not lie inside the device, or the driver fails.
 */
int block_read(block_device_t *device, uint64_t lba, uint32_t count,
               void *buffer);

/* Forget all devices (for tests). */
/* Like block_read for writing; -1 if the device is read-only. */
int block_write(block_device_t *device, uint64_t lba, uint32_t count,
                const void *buffer);

void block_reset(void);

#endif
