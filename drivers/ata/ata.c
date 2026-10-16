/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ata.h"

#include "kernel/block/block.h"

#include "arch/x86/io/io.h"

#include <stdint.h>

/*
 * Primary channel registers and commands (OSDev Wiki "ATA PIO Mode"; ATA/ATAPI-6
 * specification).
 */
#define ATA_IO_BASE 0x1F0
#define ATA_REG_DATA (ATA_IO_BASE + 0)
#define ATA_REG_SECTOR_COUNT (ATA_IO_BASE + 2)
#define ATA_REG_LBA_LOW (ATA_IO_BASE + 3)
#define ATA_REG_LBA_MID (ATA_IO_BASE + 4)
#define ATA_REG_LBA_HIGH (ATA_IO_BASE + 5)
#define ATA_REG_DRIVE (ATA_IO_BASE + 6)
#define ATA_REG_STATUS (ATA_IO_BASE + 7)  /* read */
#define ATA_REG_COMMAND (ATA_IO_BASE + 7) /* write */
#define ATA_REG_CONTROL 0x3F6             /* device control, alternate status */

#define ATA_STATUS_ERR 0x01
#define ATA_STATUS_DRQ 0x08 /* data ready to transfer */
#define ATA_STATUS_DF 0x20  /* device fault */
#define ATA_STATUS_BSY 0x80

#define ATA_CONTROL_NIEN 0x02 /* do not raise IRQ 14 */

#define ATA_DRIVE_MASTER_LBA 0xE0 /* bits 7,5 set, LBA mode, master */
#define ATA_CMD_READ_SECTORS 0x20
#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_CMD_FLUSH_CACHE 0xE7
#define ATA_CMD_IDENTIFY 0xEC

#define ATA_SECTOR_SIZE 512
#define ATA_MAX_LBA28 0x0FFFFFFFu

/* IDENTIFY data: word 60-61 is the number of LBA28 sectors. */
#define IDENTIFY_WORDS 256
#define IDENTIFY_LBA28_SECTORS 60

/* Polling limits: a working drive answers within a few thousand reads. */
#define ATA_POLL_LOOPS 1000000

static block_device_t ata_device;

/* The 400 ns the drive needs to update its status: four reads of the alternate status. */
static void ata_delay(void)
{
    for (int i = 0; i < 4; i++)
        inb(ATA_REG_CONTROL);
}

/*
 * Wait until BSY clears, then (if want_data) until DRQ is set. Returns 0 on
 * success, -1 on an error bit, a fault or a timeout.
 */
static int ata_wait(int want_data)
{
    uint8_t status = 0;

    for (int i = 0; i < ATA_POLL_LOOPS; i++)
    {
        status = inb(ATA_REG_STATUS);

        if (status & ATA_STATUS_BSY)
            continue;

        if (status & (ATA_STATUS_ERR | ATA_STATUS_DF))
            return -1;

        if (!want_data || (status & ATA_STATUS_DRQ))
            return 0;
    }

    return -1;
}

static int ata_read(block_device_t *device, uint64_t lba, uint32_t count,
                    void *buffer)
{
    (void)device;

    uint16_t *words = buffer;

    while (count > 0)
    {
        /* The sector count register is 8 bits; use at most 255 per command. */
        uint32_t batch = count > 255 ? 255 : count;

        if (ata_wait(0) != 0)
            return -1;

        outb(ATA_REG_DRIVE, ATA_DRIVE_MASTER_LBA | ((lba >> 24) & 0x0F));
        outb(ATA_REG_SECTOR_COUNT, (uint8_t)batch);
        outb(ATA_REG_LBA_LOW, (uint8_t)lba);
        outb(ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
        outb(ATA_REG_LBA_HIGH, (uint8_t)(lba >> 16));
        outb(ATA_REG_COMMAND, ATA_CMD_READ_SECTORS);

        for (uint32_t sector = 0; sector < batch; sector++)
        {
            ata_delay();

            if (ata_wait(1) != 0)
                return -1;

            for (int i = 0; i < ATA_SECTOR_SIZE / 2; i++)
                *words++ = inw(ATA_REG_DATA);
        }

        lba += batch;
        count -= batch;
    }

    return 0;
}

/*
 * WRITE SECTORS (0x30, PIO): as reading, but after the command the host
 * waits for DRQ and then sends each sector's 256 words. FLUSH CACHE (0xE7)
 * at the end makes the drive write its cache to the medium before this
 * returns (ATA/ATAPI-6, 6.4 and 6.17).
 */
static int ata_write(block_device_t *device, uint64_t lba, uint32_t count,
                     const void *buffer)
{
    (void)device;

    const uint16_t *words = buffer;

    while (count > 0)
    {
        uint32_t batch = count > 255 ? 255 : count;

        if (ata_wait(0) != 0)
            return -1;

        outb(ATA_REG_DRIVE, ATA_DRIVE_MASTER_LBA | ((lba >> 24) & 0x0F));
        outb(ATA_REG_SECTOR_COUNT, (uint8_t)batch);
        outb(ATA_REG_LBA_LOW, (uint8_t)lba);
        outb(ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
        outb(ATA_REG_LBA_HIGH, (uint8_t)(lba >> 16));
        outb(ATA_REG_COMMAND, ATA_CMD_WRITE_SECTORS);

        for (uint32_t sector = 0; sector < batch; sector++)
        {
            ata_delay();

            if (ata_wait(1) != 0)
                return -1;

            for (int i = 0; i < ATA_SECTOR_SIZE / 2; i++)
                outw(ATA_REG_DATA, *words++);
        }

        /* The drive is busy writing the last sector; wait for it. */
        ata_delay();

        if (ata_wait(0) != 0)
            return -1;

        lba += batch;
        count -= batch;
    }

    outb(ATA_REG_COMMAND, ATA_CMD_FLUSH_CACHE);
    ata_delay();

    return ata_wait(0);
}

static const block_ops_t ata_ops = {ata_read, ata_write};

int ata_init(void)
{
    /* No interrupts: everything is polled. */
    outb(ATA_REG_CONTROL, ATA_CONTROL_NIEN);

    outb(ATA_REG_DRIVE, ATA_DRIVE_MASTER_LBA);
    ata_delay();

    /* A floating bus reads 0xFF: no controller; 0: no drive. */
    uint8_t status = inb(ATA_REG_STATUS);

    if (status == 0xFF || status == 0)
        return -1;

    outb(ATA_REG_SECTOR_COUNT, 0);
    outb(ATA_REG_LBA_LOW, 0);
    outb(ATA_REG_LBA_MID, 0);
    outb(ATA_REG_LBA_HIGH, 0);
    outb(ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    if (inb(ATA_REG_STATUS) == 0)
        return -1;

    /* ATAPI and SATA devices put a signature here instead of answering IDENTIFY. */
    for (int i = 0; i < ATA_POLL_LOOPS; i++)
    {
        if (!(inb(ATA_REG_STATUS) & ATA_STATUS_BSY))
            break;
    }

    if (inb(ATA_REG_LBA_MID) != 0 || inb(ATA_REG_LBA_HIGH) != 0)
        return -1;

    if (ata_wait(1) != 0)
        return -1;

    uint16_t identify[IDENTIFY_WORDS];

    for (int i = 0; i < IDENTIFY_WORDS; i++)
        identify[i] = inw(ATA_REG_DATA);

    uint32_t sectors = (uint32_t)identify[IDENTIFY_LBA28_SECTORS] |
                       (uint32_t)identify[IDENTIFY_LBA28_SECTORS + 1] << 16;

    if (sectors == 0)
        return -1;

    if (sectors > ATA_MAX_LBA28 + 1)
        sectors = ATA_MAX_LBA28 + 1;

    ata_device.name = "ata0";
    ata_device.block_size = ATA_SECTOR_SIZE;
    ata_device.block_count = sectors;
    ata_device.ops = &ata_ops;
    ata_device.driver_data = 0;

    return block_register(&ata_device);
}
