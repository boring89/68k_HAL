/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ahci.h"

#include "hal/dma.h"
#include "hal/timer.h"

#include "kernel/block/block.h"
#include "kernel/lib/k_memory.h"
#include "kernel/printf/printf.h"

/*
 * Host registers (AHCI 1.3.1, 3.1) in ABAR, which is PCI BAR 5. All 32 bits.
 */
#define HBA_CAP 0x00
#define HBA_GHC 0x04
#define HBA_IS 0x08
#define HBA_PI 0x0C /* bit n: port n is implemented */

#define GHC_AE (1u << 31) /* AHCI enable */

#define PORT_BASE 0x100
#define PORT_SIZE 0x80
#define PORT_CLB 0x00 /* command list base address (1 KiB aligned) */
#define PORT_CLBU 0x04
#define PORT_FB 0x08  /* received FIS base address (256 byte aligned) */
#define PORT_FBU 0x0C
#define PORT_IS 0x10
#define PORT_CMD 0x18
#define PORT_TFD 0x20 /* task file data: status in the low byte */
#define PORT_SIG 0x24
#define PORT_SSTS 0x28
#define PORT_SERR 0x30
#define PORT_CI 0x38 /* command issue: bit n starts command slot n */

#define CMD_ST (1u << 0)   /* start: process the command list */
#define CMD_FRE (1u << 4)  /* FIS receive enable */
#define CMD_FR (1u << 14)  /* FIS receive running */
#define CMD_CR (1u << 15)  /* command list running */

#define TFD_ERR 0x01
#define TFD_DRQ 0x08
#define TFD_BSY 0x80

#define SIG_ATA 0x00000101u /* a SATA disk (ATAPI is 0xEB140101) */
#define SSTS_DET_PRESENT 3u /* device present and communication established */
#define SSTS_IPM_ACTIVE 1u

/* ATA commands (ACS-2). */
#define ATA_READ_DMA_EXT 0x25
#define ATA_WRITE_DMA_EXT 0x35
#define ATA_FLUSH_CACHE_EXT 0xEA
#define ATA_IDENTIFY 0xEC

#define SECTOR 512
#define MAX_SECTORS 128 /* per command: the size of the bounce buffer */
#define MAX_PORTS 4

/* Command list entry (command header, 4 DWORDs of 8 in the spec's 32 bytes). */
typedef struct
{
    uint16_t flags; /* bits 4:0 command FIS length in DWORDs, bit 6 write */
    uint16_t prdtl; /* number of PRDT entries */
    uint32_t prdbc; /* bytes transferred */
    uint32_t ctba;  /* command table address (128 byte aligned) */
    uint32_t ctbau;
    uint32_t reserved[4];
} __attribute__((packed)) command_header_t;

/* Physical region descriptor: one contiguous piece of the data buffer. */
typedef struct
{
    uint32_t dba;
    uint32_t dbau;
    uint32_t reserved;
    uint32_t dbc; /* bits 21:0 byte count - 1; bit 31 interrupt on completion */
} __attribute__((packed)) prd_t;

/* Command table: the command FIS, then the PRDT at offset 0x80. */
typedef struct
{
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t reserved[48];
    prd_t prdt[1];
} __attribute__((packed)) command_table_t;

typedef struct
{
    int present;
    volatile uint8_t *registers; /* this port's registers */
    command_header_t *list;
    command_table_t *table;
    uint32_t table_phys;
    uint8_t *buffer;             /* bounce buffer for the data */
    uint32_t buffer_phys;
    char name[8];
    block_device_t device;
} port_t;

static volatile uint8_t *hba;
static port_t ports[MAX_PORTS];
static unsigned int port_count;

static uint32_t read_hba(uint32_t offset)
{
    return *(volatile uint32_t *)(hba + offset);
}

static void write_hba(uint32_t offset, uint32_t value)
{
    *(volatile uint32_t *)(hba + offset) = value;
}

static uint32_t read_port(const port_t *p, uint32_t offset)
{
    return *(volatile uint32_t *)(p->registers + offset);
}

static void write_port(const port_t *p, uint32_t offset, uint32_t value)
{
    *(volatile uint32_t *)(p->registers + offset) = value;
}

/* Wait until the bits selected by mask in the port register are all clear. 0 or -1 after 500 ms. */
static int wait_clear(const port_t *p, uint32_t offset, uint32_t mask)
{
    uint64_t end = hal_timer_ns() + 500000000u;

    while (read_port(p, offset) & mask)
    {
        if (hal_timer_ns() > end)
            return -1;
    }

    return 0;
}

static int stop_port(const port_t *p)
{
    write_port(p, PORT_CMD, read_port(p, PORT_CMD) & ~(CMD_ST | CMD_FRE));

    return wait_clear(p, PORT_CMD, CMD_CR | CMD_FR);
}

/*
 * Run one command on slot 0: build the H2D register FIS (type 0x27, "command"
 * bit set), describe the data buffer, set the command-issue bit and wait for
 * it to clear. Returns 0, or -1 on an error or after 2 s.
 */
static int run_command(port_t *p, uint8_t command, uint64_t lba, uint32_t sectors, int write, uint32_t bytes)
{
    command_table_t *t = p->table;

    k_memset(t, 0, sizeof(*t));
    t->cfis[0] = 0x27;
    t->cfis[1] = 0x80;
    t->cfis[2] = command;
    t->cfis[4] = (uint8_t)lba;
    t->cfis[5] = (uint8_t)(lba >> 8);
    t->cfis[6] = (uint8_t)(lba >> 16);
    t->cfis[7] = 0x40; /* device register: LBA mode */
    t->cfis[8] = (uint8_t)(lba >> 24);
    t->cfis[9] = (uint8_t)(lba >> 32);
    t->cfis[10] = (uint8_t)(lba >> 40);
    t->cfis[12] = (uint8_t)sectors;
    t->cfis[13] = (uint8_t)(sectors >> 8);

    if (bytes > 0)
    {
        t->prdt[0].dba = p->buffer_phys;
        t->prdt[0].dbc = bytes - 1;
    }

    p->list[0].flags = (uint16_t)(5 | (write ? 0x40 : 0)); /* the FIS is 5 DWORDs */
    p->list[0].prdtl = bytes > 0 ? 1 : 0;
    p->list[0].prdbc = 0;
    p->list[0].ctba = p->table_phys;
    p->list[0].ctbau = 0;

    if (wait_clear(p, PORT_TFD, TFD_BSY | TFD_DRQ) != 0)
        return -1;

    write_port(p, PORT_IS, 0xFFFFFFFFu);
    write_port(p, PORT_CI, 1);

    uint64_t end = hal_timer_ns() + 2000000000u;

    while (read_port(p, PORT_CI) & 1)
    {
        if (read_port(p, PORT_TFD) & TFD_ERR)
            return -1;

        if (hal_timer_ns() > end)
            return -1;
    }

    return (read_port(p, PORT_TFD) & TFD_ERR) ? -1 : 0;
}

static int ahci_read(block_device_t *device, uint64_t lba, uint32_t count, void *buffer)
{
    port_t *p = device->driver_data;
    uint8_t *out = buffer;

    while (count > 0)
    {
        uint32_t n = count > MAX_SECTORS ? MAX_SECTORS : count;

        if (run_command(p, ATA_READ_DMA_EXT, lba, n, 0, n * SECTOR) != 0)
            return -1;

        k_memcpy(out, p->buffer, (size_t)n * SECTOR);
        out += (size_t)n * SECTOR;
        lba += n;
        count -= n;
    }

    return 0;
}

static int ahci_write(block_device_t *device, uint64_t lba, uint32_t count, const void *buffer)
{
    port_t *p = device->driver_data;
    const uint8_t *in = buffer;

    while (count > 0)
    {
        uint32_t n = count > MAX_SECTORS ? MAX_SECTORS : count;

        k_memcpy(p->buffer, in, (size_t)n * SECTOR);

        if (run_command(p, ATA_WRITE_DMA_EXT, lba, n, 1, n * SECTOR) != 0)
            return -1;

        in += (size_t)n * SECTOR;
        lba += n;
        count -= n;
    }

    /* Make the disk write its cache out before reporting success. */
    return run_command(p, ATA_FLUSH_CACHE_EXT, 0, 0, 0, 0);
}

static const block_ops_t ahci_ops = {ahci_read, ahci_write};

/* Set up one port that has a disk and register it. Returns 0 if a block device was added. */
static int start_port(port_t *p, unsigned int number)
{
    p->registers = hba + PORT_BASE + number * PORT_SIZE;

    uint32_t status = read_port(p, PORT_SSTS);

    if ((status & 0xF) != SSTS_DET_PRESENT || ((status >> 8) & 0xF) != SSTS_IPM_ACTIVE)
        return -1;

    if (read_port(p, PORT_SIG) != SIG_ATA)
        return -1;

    if (stop_port(p) != 0)
        return -1;

    uint32_t list_phys;
    uint32_t fis_phys;
    uint32_t buffer_phys;

    /* One page: the command list (1 KiB, 32 headers) and the received-FIS area (256 B) and the command table. */
    uint8_t *memory = hal_dma_alloc(4096, &list_phys);

    p->buffer = hal_dma_alloc(MAX_SECTORS * SECTOR, &buffer_phys);

    if (memory == NULL || p->buffer == NULL)
        return -1;

    p->list = (command_header_t *)memory;
    fis_phys = list_phys + 1024;
    p->table = (command_table_t *)(memory + 2048);
    p->table_phys = list_phys + 2048;
    p->buffer_phys = buffer_phys;

    write_port(p, PORT_CLB, list_phys);
    write_port(p, PORT_CLBU, 0);
    write_port(p, PORT_FB, fis_phys);
    write_port(p, PORT_FBU, 0);
    write_port(p, PORT_SERR, 0xFFFFFFFFu);
    write_port(p, PORT_IS, 0xFFFFFFFFu);

    write_port(p, PORT_CMD, read_port(p, PORT_CMD) | CMD_FRE);
    write_port(p, PORT_CMD, read_port(p, PORT_CMD) | CMD_ST);

    /* IDENTIFY DEVICE: words 100-103 hold the number of sectors (48-bit LBA). */
    if (run_command(p, ATA_IDENTIFY, 0, 0, 0, SECTOR) != 0)
        return -1;

    const uint16_t *identify = (const uint16_t *)p->buffer;
    uint64_t sectors = (uint64_t)identify[100] | (uint64_t)identify[101] << 16 |
                       (uint64_t)identify[102] << 32 | (uint64_t)identify[103] << 48;

    if (sectors == 0)
        sectors = (uint64_t)identify[60] | (uint64_t)identify[61] << 16; /* LBA28 size */

    if (sectors == 0)
        return -1;

    p->name[0] = 's';
    p->name[1] = 'a';
    p->name[2] = 't';
    p->name[3] = 'a';
    p->name[4] = (char)('0' + port_count);
    p->name[5] = '\0';

    p->device.name = p->name;
    p->device.block_size = SECTOR;
    p->device.block_count = sectors;
    p->device.ops = &ahci_ops;
    p->device.driver_data = p;
    p->present = 1;

    return block_register(&p->device);
}

static int ahci_probe(const hal_pci_device_t *pci)
{
    if (hba != NULL || pci->prog_if != 0x01)
        return -1;

    hal_pci_enable(pci, HAL_PCI_COMMAND_MEMORY | HAL_PCI_COMMAND_BUS_MASTER);

    hba = hal_pci_map_bar(pci, 5);

    if (hba == NULL)
        return -1;

    write_hba(HBA_GHC, read_hba(HBA_GHC) | GHC_AE);

    uint32_t implemented = read_hba(HBA_PI);

    for (unsigned int n = 0; n < 32 && port_count < MAX_PORTS; n++)
    {
        if (!(implemented & (1u << n)))
            continue;

        if (start_port(&ports[port_count], n) == 0)
            port_count++;
    }

    if (port_count == 0)
    {
        hba = NULL;

        return -1;
    }

    return 0;
}

const hal_pci_driver_t ahci_pci_driver = {
    .name = "ahci",
    .vendor_id = 0,
    .class_code = 0x01,
    .subclass = 0x06,
    .probe = ahci_probe,
};
