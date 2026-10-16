/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "emu/mac/mac.h"

#include "emu/m68k/vm.h"
#include "kernel/lib/k_memory.h"

/*
 * The .Disk driver: a hard disk for the Mac OS that is really a disk image
 * (or, in the kernel, a block device).
 *
 * Think of the Device Manager as a receptionist: programs hand it a request
 * ("read 4 blocks from drive 8"), and it forwards the request to the driver
 * registered for that drive. The driver's four entry points (Open, Prime =
 * read/write, Control, Status) are tiny pieces of 68k code in the stub window
 * (stub.s) that each execute one emulated opcode; this file is what runs.
 *
 * Structures follow Inside Macintosh: Devices (Device Control Entry, driver
 * header, parameter blocks) and Files (drive queue). The structure of such
 * a driver follows the one in Basilisk II's disk.cpp (GPL).
 */

#define DRIVE_NUMBER 8
#define DRIVER_REFNUM (-50)
#define DRIVER_FLAGS 0x6F00         /* read, write, control, status, lock, time */
#define DRIVER_DELAY 60

/* Device Control Entry (Inside Macintosh: Devices, "Device Control Entry"). */
#define DCE_DRIVER 0
#define DCE_FLAGS 4
#define DCE_POSITION 16
#define DCE_REFNUM 24
#define DCE_DELAY 34
#define DCE_EMASK 36
#define DCE_MENU 38

/* Parameter block fields (IOParam / CntrlParam). */
#define PB_TRAP 6
#define PB_VREFNUM 22
#define PB_BUFFER 32
#define PB_REQCOUNT 36
#define PB_ACTCOUNT 40
#define PB_CSCODE 26
#define PB_CSPARAM 28

/* Drive status record (DrvSts), 22 bytes, and its queue element part. */
#define DRVSTS_WRITEPROT 2
#define DRVSTS_DISKINPLACE 3
#define DRVSTS_INSTALLED 4
#define DRVSTS_QLINK 6
#define DRVSTS_QTYPE 10
#define DRVSTS_DRIVE 12
#define DRVSTS_REFNUM 14
#define DRVSTS_FSID 16
#define DRVSTS_SIZE_LOW 18
#define DRVSTS_SIZE_HIGH 20
#define DRVSTS_BYTES 22

#define DISK_IN_PLACE_FIXED 8       /* a disk that cannot be ejected */
#define QTYPE_HAS_SIZE2 1           /* dQDrvSz2 holds the high half */

#define IOTRAP_CMD_MASK 0xFF
#define IOTRAP_WRITE 3              /* A003 = _Write, A002 = _Read */

#define BLOCK_SIZE 512

/* Device Manager and File Manager error codes (Inside Macintosh). */
#define NO_ERR 0
#define CONTROL_ERR (-17)
#define STATUS_ERR (-18)
#define READ_ERR (-19)
#define WRITE_ERR (-20)
#define PARAM_ERR (-50)
#define NS_DRV_ERR (-56)
#define W_PRERR (-44)

/* csCode values handled by Control and Status. */
#define CS_KILL_IO 1
#define CS_VERIFY 5
#define CS_FORMAT 6
#define CS_EJECT 7
#define CS_DRIVE_STATUS 8
#define CS_PARTITION_SIZE 24
#define CS_ACC_RUN 65

#define REG_D0 M68K_VM_REG_D0
#define REG_A0 M68K_VM_REG_A0


static uint32_t drive_record(void)
{
    return MAC_STUB_BASE + MAC_STUB_DRIVE_RECORD;
}


static uint32_t reg(int r)
{
    return m68k_vm_get_reg((m68k_vm_reg_t)r);
}


static void set_result(int err)
{
    m68k_vm_set_reg(REG_D0, (uint32_t)err);
}


/* Install: the DCE just created by _DrvrInstall points at our driver. */
void mac_disk_install(mac_t *mac)
{
    uint32_t dce = reg(REG_A0 + 1);

    mac_bus_write(mac, dce + DCE_DRIVER, 4, MAC_STUB_BASE + MAC_STUB_DRIVER);
    mac_bus_write(mac, dce + DCE_FLAGS, 2, DRIVER_FLAGS);
    mac_bus_write(mac, dce + DCE_DELAY, 2, DRIVER_DELAY);
    mac_bus_write(mac, dce + DCE_EMASK, 2, 0);
    mac_bus_write(mac, dce + DCE_MENU, 2, 0);
}


/*
 * Open: describe the disk in the drive record. The stub code then calls
 * _AddDrive with A0 = the queue element inside the record and D0 = drive
 * number in the high word, driver reference number in the low word.
 */
void mac_disk_open(mac_t *mac)
{
    uint32_t dce = reg(REG_A0 + 1);
    uint32_t rec = drive_record();

    mac_bus_write(mac, dce + DCE_POSITION, 4, 0);
    for (uint32_t i = 0; i < DRVSTS_BYTES; i += 2)
    {
        mac_bus_write(mac, rec + i, 2, 0);
    }
    mac_bus_write(mac, rec + DRVSTS_WRITEPROT, 1, mac->disk.read_only ? 0xFF : 0);
    mac_bus_write(mac, rec + DRVSTS_DISKINPLACE, 1,
                  mac->disk.blocks != 0 ? DISK_IN_PLACE_FIXED : 0);
    mac_bus_write(mac, rec + DRVSTS_INSTALLED, 1, 1);
    mac_bus_write(mac, rec + DRVSTS_QTYPE, 2, QTYPE_HAS_SIZE2);
    mac_bus_write(mac, rec + DRVSTS_DRIVE, 2, DRIVE_NUMBER);
    mac_bus_write(mac, rec + DRVSTS_REFNUM, 2, DRIVER_REFNUM & 0xFFFF);
    mac_bus_write(mac, rec + DRVSTS_SIZE_LOW, 2, mac->disk.blocks & 0xFFFF);
    mac_bus_write(mac, rec + DRVSTS_SIZE_HIGH, 2, mac->disk.blocks >> 16);

    m68k_vm_set_reg((m68k_vm_reg_t)REG_A0, rec + DRVSTS_QLINK);
    m68k_vm_set_reg(REG_D0, ((uint32_t)DRIVE_NUMBER << 16) |
                                ((uint32_t)DRIVER_REFNUM & 0xFFFF));
}


/* Copy between guest memory and a host buffer, 512 bytes at a time. */
static int transfer(mac_t *mac, bool write, uint32_t guest, uint32_t offset,
                    uint32_t len)
{
    uint8_t block[BLOCK_SIZE];

    for (uint32_t done = 0; done < len; done += BLOCK_SIZE)
    {
        if (write)
        {
            for (uint32_t i = 0; i < BLOCK_SIZE; i++)
            {
                block[i] = (uint8_t)mac_bus_read(mac, guest + done + i, 1);
            }
            if (mac->disk.write(mac->disk.ctx, offset + done, block,
                                BLOCK_SIZE) != 0)
            {
                return -1;
            }
        }
        else
        {
            if (mac->disk.read(mac->disk.ctx, offset + done, block,
                               BLOCK_SIZE) != 0)
            {
                return -1;
            }
            for (uint32_t i = 0; i < BLOCK_SIZE; i++)
            {
                mac_bus_write(mac, guest + done + i, 1, block[i]);
            }
        }
    }
    return 0;
}


/* Prime: read or write blocks at the position kept in the DCE. */
void mac_disk_prime(mac_t *mac)
{
    uint32_t pb = reg(REG_A0);
    uint32_t dce = reg(REG_A0 + 1);
    uint32_t buffer = mac_bus_read(mac, pb + PB_BUFFER, 4);
    uint32_t count = mac_bus_read(mac, pb + PB_REQCOUNT, 4);
    uint32_t pos = mac_bus_read(mac, dce + DCE_POSITION, 4);
    bool write = (mac_bus_read(mac, pb + PB_TRAP, 2) & IOTRAP_CMD_MASK) ==
                 IOTRAP_WRITE;
    int err = NO_ERR;

    mac_bus_write(mac, pb + PB_ACTCOUNT, 4, 0);
    if (mac_bus_read(mac, pb + PB_VREFNUM, 2) != DRIVE_NUMBER)
    {
        err = NS_DRV_ERR;
    }
    else if (mac->disk.blocks == 0)
    {
        err = NS_DRV_ERR;
    }
    else if (count % BLOCK_SIZE != 0 || pos % BLOCK_SIZE != 0)
    {
        err = PARAM_ERR;
    }
    else if (write && mac->disk.read_only)
    {
        err = W_PRERR;
    }
    else if ((uint64_t)pos + count > (uint64_t)mac->disk.blocks * BLOCK_SIZE ||
             transfer(mac, write, buffer, pos, count) != 0)
    {
        err = write ? WRITE_ERR : READ_ERR;
    }
    else
    {
        mac_bus_write(mac, pb + PB_ACTCOUNT, 4, count);
        mac_bus_write(mac, dce + DCE_POSITION, 4, pos + count);
    }
    set_result(err);
}


/* Control: only the requests the file system and the startup code make. */
void mac_disk_control(mac_t *mac)
{
    uint32_t pb = reg(REG_A0);
    uint32_t code = mac_bus_read(mac, pb + PB_CSCODE, 2);
    int err = NO_ERR;

    switch (code)
    {
    case CS_KILL_IO:
    case CS_ACC_RUN:
    case CS_VERIFY:
    case CS_EJECT:
        break;
    case CS_FORMAT:
        err = mac->disk.read_only ? W_PRERR : NO_ERR;
        break;
    case CS_PARTITION_SIZE:
        mac_bus_write(mac, pb + PB_CSPARAM, 4, mac->disk.blocks);
        break;
    default:
        err = CONTROL_ERR;
        break;
    }
    set_result(err);
}


/* Status: the drive status record. */
void mac_disk_status(mac_t *mac)
{
    uint32_t pb = reg(REG_A0);
    uint32_t code = mac_bus_read(mac, pb + PB_CSCODE, 2);
    int err = NO_ERR;

    if (code == CS_DRIVE_STATUS)
    {
        for (uint32_t i = 0; i < DRVSTS_BYTES; i++)
        {
            mac_bus_write(mac, pb + PB_CSPARAM + i, 1,
                          mac_bus_read(mac, drive_record() + i, 1));
        }
    }
    else
    {
        err = STATUS_ERR;
    }
    set_result(err);
}
