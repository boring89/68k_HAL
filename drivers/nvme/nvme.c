/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "nvme.h"

#include "hal/dma.h"
#include "hal/timer.h"

#include "kernel/block/block.h"
#include "kernel/lib/k_memory.h"
#include "kernel/log/log.h"
#include "kernel/printf/printf.h"

/*
 * How it works, with a post office again: the drive has a pair of mailboxes per queue, a
 * submission queue (SQ: we put commands in, 64 bytes each) and a completion queue (CQ: it puts
 * answers in, 16 bytes each). After putting a command in we ring the SQ's doorbell (a register)
 * with the new tail position; the drive does the work and writes a completion into the CQ,
 * flipping a "phase" bit each lap around the ring so we can tell new entries from old ones. When
 * we have read an answer we ring the CQ's doorbell with the new head. Queue 0 is the admin
 * queue (create queues, identify); queue 1, made by us, carries the reads and writes.
 */

/* Controller registers (NVMe 1.4, 3.1). */
#define REG_CAP 0x00 /* 64 bits */
#define REG_CC 0x14
#define REG_CSTS 0x1C
#define REG_AQA 0x24
#define REG_ASQ 0x28 /* 64 bits */
#define REG_ACQ 0x30 /* 64 bits */
#define REG_DOORBELLS 0x1000

#define CC_ENABLE (1u << 0)
#define CC_IO_SQ_ENTRY_SIZE(log2) ((uint32_t)(log2) << 16) /* 64 bytes: 6 */
#define CC_IO_CQ_ENTRY_SIZE(log2) ((uint32_t)(log2) << 20) /* 16 bytes: 4 */
#define CSTS_READY (1u << 0)
#define CSTS_FATAL (1u << 1)

/* Admin and NVM command set opcodes (NVMe 1.4, figures 138 and 346). */
#define ADMIN_CREATE_IO_SQ 0x01
#define ADMIN_CREATE_IO_CQ 0x05
#define ADMIN_IDENTIFY 0x06
#define ADMIN_SET_FEATURES 0x09
#define NVM_FLUSH 0x00
#define NVM_WRITE 0x01
#define NVM_READ 0x02

#define QUEUE_ENTRIES 64
#define BOUNCE_SIZE (128u * 1024)
#define PAGE 4096u
#define MAX_CONTROLLERS 2

typedef struct
{
    uint32_t dword[16];
} submission_t;

typedef struct
{
    uint32_t result;
    uint32_t reserved;
    uint32_t sq_head_and_id;
    uint32_t status; /* bits 15:0 command id, bit 16 phase, bits 31:17 status */
} completion_t;

typedef struct
{
    submission_t *submissions;
    completion_t *completions;
    uint32_t submissions_phys;
    uint32_t completions_phys;
    unsigned int tail;
    unsigned int head;
    unsigned int phase;
    unsigned int id;                 /* queue id: 0 admin, 1 I/O */
} queue_t;

typedef struct
{
    volatile uint8_t *registers;
    unsigned int doorbell_stride;
    uint32_t timeout_ms;
    queue_t admin;
    queue_t io;
    uint16_t next_command;
    uint8_t *bounce;
    uint32_t bounce_phys;
    uint32_t *prp_list;
    uint32_t prp_list_phys;
    uint32_t max_blocks;
    uint32_t namespace_id;
    char name[8];
    block_device_t device;
    int present;
} controller_t;

static controller_t controllers[MAX_CONTROLLERS];
static unsigned int controller_count;

static uint32_t read32(const controller_t *c, unsigned int offset)
{
    return *(volatile uint32_t *)(c->registers + offset);
}

static void write32(const controller_t *c, unsigned int offset, uint32_t value)
{
    *(volatile uint32_t *)(c->registers + offset) = value;
}

static void write64(const controller_t *c, unsigned int offset, uint32_t value)
{
    write32(c, offset, value);
    write32(c, offset + 4, 0);
}

static int wait_ready(const controller_t *c, uint32_t want)
{
    uint64_t end = hal_timer_ns() + (uint64_t)c->timeout_ms * 1000000u;

    while ((read32(c, REG_CSTS) & CSTS_READY) != want)
    {
        if ((read32(c, REG_CSTS) & CSTS_FATAL) || hal_timer_ns() > end)
            return -1;

        asm volatile("pause");
    }

    return 0;
}

static int queue_init(queue_t *queue, unsigned int id)
{
    queue->submissions = hal_dma_alloc(QUEUE_ENTRIES * sizeof(submission_t), &queue->submissions_phys);
    queue->completions = hal_dma_alloc(QUEUE_ENTRIES * sizeof(completion_t), &queue->completions_phys);

    if (queue->submissions == 0 || queue->completions == 0)
        return -1;

    queue->tail = 0;
    queue->head = 0;
    queue->phase = 1;
    queue->id = id;

    return 0;
}

/*
 * Put one command on a queue, ring its doorbell and wait for the answer. dword[] are the 16
 * command words from word 0 on (the command id is filled in here). Returns the status field of
 * the completion (0 = success), or -1 on a timeout.
 */
static int run_command(controller_t *c, queue_t *queue, const uint32_t dword[16], uint32_t *result)
{
    submission_t *slot = &queue->submissions[queue->tail];
    uint16_t id = ++c->next_command;

    for (int i = 0; i < 16; i++)
        slot->dword[i] = dword[i];

    slot->dword[0] = (dword[0] & 0xFFFFu) | ((uint32_t)id << 16);

    queue->tail = (queue->tail + 1) % QUEUE_ENTRIES;
    asm volatile("" : : : "memory");
    write32(c, REG_DOORBELLS + (2 * queue->id) * c->doorbell_stride, queue->tail);

    uint64_t end = hal_timer_ns() + (uint64_t)c->timeout_ms * 1000000u;
    volatile completion_t *entry = &queue->completions[queue->head];

    while (((entry->status >> 16) & 1u) != queue->phase)
    {
        if (hal_timer_ns() > end)
            return -1;

        asm volatile("pause");
    }

    uint32_t status = entry->status;

    if (result != 0)
        *result = entry->result;

    queue->head = (queue->head + 1) % QUEUE_ENTRIES;

    if (queue->head == 0)
        queue->phase ^= 1;

    write32(c, REG_DOORBELLS + (2 * queue->id + 1) * c->doorbell_stride, queue->head);

    return (int)(status >> 17); /* status code (bits 24:17) and type (27:25) */
}

static int identify(controller_t *c, uint32_t cns, uint32_t namespace_id, uint32_t data_phys)
{
    uint32_t command[16] = {0};

    command[0] = ADMIN_IDENTIFY;
    command[1] = namespace_id;
    command[6] = data_phys; /* PRP1 */
    command[10] = cns;

    return run_command(c, &c->admin, command, 0);
}

/* The PRP entries (NVMe 1.4, 4.3) for a transfer of `bytes` bytes from the start of the bounce buffer. */
static void set_data_pointers(controller_t *c, uint32_t command[16], uint32_t bytes)
{
    unsigned int pages = (bytes + PAGE - 1) / PAGE;

    command[6] = c->bounce_phys;

    if (pages == 2)
    {
        command[8] = c->bounce_phys + PAGE;
    }
    else if (pages > 2)
    {
        /* PRP2 points at a list with the address of every page after the first. */
        for (unsigned int i = 1; i < pages; i++)
        {
            c->prp_list[2 * (i - 1)] = c->bounce_phys + i * PAGE; /* entries are 64 bits: low word, high word */
            c->prp_list[2 * (i - 1) + 1] = 0;
        }

        command[8] = c->prp_list_phys;
    }
}

static int transfer(controller_t *c, uint32_t opcode, uint64_t lba, uint32_t blocks)
{
    uint32_t command[16] = {0};

    command[0] = opcode;
    command[1] = c->namespace_id;
    set_data_pointers(c, command, blocks * c->device.block_size);
    command[10] = (uint32_t)lba;
    command[11] = (uint32_t)(lba >> 32);
    command[12] = blocks - 1; /* number of blocks, zero based */

    return run_command(c, &c->io, command, 0);
}

static int nvme_read(block_device_t *device, uint64_t lba, uint32_t count, void *buffer)
{
    controller_t *c = device->driver_data;
    uint8_t *out = buffer;

    while (count > 0)
    {
        uint32_t n = count > c->max_blocks ? c->max_blocks : count;

        if (transfer(c, NVM_READ, lba, n) != 0)
            return -1;

        k_memcpy(out, c->bounce, (size_t)n * device->block_size);
        out += (size_t)n * device->block_size;
        lba += n;
        count -= n;
    }

    return 0;
}

static int nvme_write(block_device_t *device, uint64_t lba, uint32_t count, const void *buffer)
{
    controller_t *c = device->driver_data;
    const uint8_t *in = buffer;

    while (count > 0)
    {
        uint32_t n = count > c->max_blocks ? c->max_blocks : count;

        k_memcpy(c->bounce, in, (size_t)n * device->block_size);

        if (transfer(c, NVM_WRITE, lba, n) != 0)
            return -1;

        in += (size_t)n * device->block_size;
        lba += n;
        count -= n;
    }

    /* Flush: the drive may keep written data in a volatile cache. */
    uint32_t flush[16] = {0};

    flush[0] = NVM_FLUSH;
    flush[1] = c->namespace_id;

    return run_command(c, &c->io, flush, 0) == 0 ? 0 : -1;
}

static const block_ops_t nvme_ops = {nvme_read, nvme_write};

static int start_controller(controller_t *c, const hal_pci_device_t *pci)
{
    c->registers = hal_pci_map_bar(pci, 0);

    if (c->registers == 0)
    {
        /* A 64-bit BAR above 4 GiB cannot be reached by this 32-bit kernel (BIOS: turn "Above 4G Decoding" off). */
        kernel_log(LOG_WARN, "NVMe: its registers are not reachable (BAR above 4 GiB?)");

        return -1;
    }

    hal_pci_enable(pci, HAL_PCI_COMMAND_MEMORY | HAL_PCI_COMMAND_BUS_MASTER);

    /* Capabilities (3.1.1): 64 bits, read as two words. */
    uint32_t cap_low = read32(c, REG_CAP);
    uint32_t cap_high = read32(c, REG_CAP + 4);

    c->timeout_ms = ((cap_low >> 24) & 0xFF) * 500u + 500u;
    c->doorbell_stride = 4u << (cap_high & 0xF);

    if (((cap_high >> 16) & 0xF) != 0) /* minimum page size above 4 KiB */
        return -1;

    /* Stop the controller if the firmware left it running, then set up the admin queues. */
    if (read32(c, REG_CC) & CC_ENABLE)
    {
        write32(c, REG_CC, 0);

        if (wait_ready(c, 0) != 0)
            return -1;
    }

    if (queue_init(&c->admin, 0) != 0 || queue_init(&c->io, 1) != 0)
        return -1;

    write32(c, REG_AQA, ((QUEUE_ENTRIES - 1) << 16) | (QUEUE_ENTRIES - 1));
    write64(c, REG_ASQ, c->admin.submissions_phys);
    write64(c, REG_ACQ, c->admin.completions_phys);

    /* NVM command set, 4 KiB pages, round-robin, 64-byte submission and 16-byte completion entries. */
    write32(c, REG_CC, CC_ENABLE | CC_IO_SQ_ENTRY_SIZE(6) | CC_IO_CQ_ENTRY_SIZE(4));

    if (wait_ready(c, CSTS_READY) != 0)
        return -1;

    c->bounce = hal_dma_alloc(BOUNCE_SIZE, &c->bounce_phys);
    c->prp_list = hal_dma_alloc(PAGE, &c->prp_list_phys);

    if (c->bounce == 0 || c->prp_list == 0)
        return -1;

    /* Identify the controller (CNS 1) for the transfer size limit, then namespace 1 (CNS 0) for the size. */
    uint32_t data_phys;
    uint8_t *data = hal_dma_alloc(PAGE, &data_phys);

    if (data == 0)
        return -1;

    int failed = identify(c, 1, 0, data_phys);
    uint32_t max_bytes = BOUNCE_SIZE;

    if (failed == 0)
    {
        /* MDTS (byte 77): the largest transfer is 2^MDTS pages; 0 = no limit. */
        if (data[77] != 0 && data[77] < 20 && (PAGE << data[77]) < max_bytes)
            max_bytes = PAGE << data[77];

        kernel_printf("nvme: %c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c\n", data[24], data[25], data[26], data[27],
                      data[28], data[29], data[30], data[31], data[32], data[33], data[34], data[35], data[36],
                      data[37], data[38], data[39], data[40], data[41], data[42], data[43]);
    }

    c->namespace_id = 1;

    if (failed == 0)
        failed = identify(c, 0, c->namespace_id, data_phys);

    uint64_t blocks = 0;
    uint32_t block_size = 0;

    if (failed == 0)
    {
        blocks = (uint64_t)data[0] | (uint64_t)data[1] << 8 | (uint64_t)data[2] << 16 | (uint64_t)data[3] << 24 |
                 (uint64_t)data[4] << 32 | (uint64_t)data[5] << 40 | (uint64_t)data[6] << 48 | (uint64_t)data[7] << 56;

        /* FLBAS (byte 26) bits 3:0 pick the LBA format; its data size is 2^LBADS (byte 2 of the 4-byte entry at 128). */
        unsigned int format = data[26] & 0xF;
        unsigned int shift = data[128 + 4 * format + 2];

        if (shift >= 9 && shift <= 16)
            block_size = 1u << shift;
    }

    hal_dma_free(data, PAGE);

    if (failed != 0 || blocks == 0 || block_size == 0)
        return -1;

    c->max_blocks = max_bytes / block_size;

    if (c->max_blocks == 0)
        return -1;

    /* Number of Queues (feature 7): ask for one I/O queue pair (zero based). Real drives want this before queues are made. */
    uint32_t command[16] = {0};

    command[0] = ADMIN_SET_FEATURES;
    command[10] = 7;
    run_command(c, &c->admin, command, 0);

    /* The I/O queue pair: the completion queue first (the submission queue names it). */
    k_memset(command, 0, sizeof(command));

    command[0] = ADMIN_CREATE_IO_CQ;
    command[6] = c->io.completions_phys;
    command[10] = ((QUEUE_ENTRIES - 1) << 16) | 1; /* size - 1, queue id 1 */
    command[11] = 1;                               /* physically contiguous, interrupts off */

    if (run_command(c, &c->admin, command, 0) != 0)
        return -1;

    k_memset(command, 0, sizeof(command));
    command[0] = ADMIN_CREATE_IO_SQ;
    command[6] = c->io.submissions_phys;
    command[10] = ((QUEUE_ENTRIES - 1) << 16) | 1;
    command[11] = (1u << 16) | 1; /* completion queue 1, physically contiguous */

    if (run_command(c, &c->admin, command, 0) != 0)
        return -1;

    c->name[0] = 'n';
    c->name[1] = 'v';
    c->name[2] = 'm';
    c->name[3] = 'e';
    c->name[4] = (char)('0' + controller_count);
    c->name[5] = '\0';

    c->device.name = c->name;
    c->device.block_size = block_size;
    c->device.block_count = blocks;
    c->device.ops = &nvme_ops;
    c->device.driver_data = c;
    c->present = 1;

    return block_register(&c->device);
}

static int nvme_probe(const hal_pci_device_t *pci)
{
    if (pci->prog_if != 0x02 || controller_count >= MAX_CONTROLLERS || pci->bar[0].type != HAL_PCI_BAR_MEM)
        return -1;

    controller_t *c = &controllers[controller_count];

    k_memset(c, 0, sizeof(*c));

    if (start_controller(c, pci) != 0)
    {
        kernel_log(LOG_WARN, "NVMe: cannot start the controller");

        return -1;
    }

    controller_count++;

    return 0;
}

const hal_pci_driver_t nvme_pci_driver = {
    .name = "nvme",
    .vendor_id = 0,
    .class_code = 0x01,
    .subclass = 0x08,
    .probe = nvme_probe,
};
