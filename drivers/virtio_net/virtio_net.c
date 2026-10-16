/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "virtio_net.h"

#include "arch/x86/io/io.h"

#include "hal/dma.h"
#include "hal/irq.h"
#include "hal/timer.h"

#include "kernel/lib/k_memory.h"
#include "kernel/net/net.h"

/*
 * Legacy register block in I/O BAR 0 (VIRTIO 1.1, 4.1.4.8, Table "Legacy
 * Interfaces").
 */
#define REG_DEVICE_FEATURES 0x00 /* 32 bits */
#define REG_GUEST_FEATURES 0x04  /* 32 bits */
#define REG_QUEUE_ADDRESS 0x08   /* 32 bits: page frame number of the queue */
#define REG_QUEUE_SIZE 0x0C      /* 16 bits, read only */
#define REG_QUEUE_SELECT 0x0E    /* 16 bits */
#define REG_QUEUE_NOTIFY 0x10    /* 16 bits */
#define REG_STATUS 0x12          /* 8 bits */
#define REG_ISR 0x13             /* 8 bits: reading it acknowledges the interrupt */
#define REG_MAC 0x14             /* device configuration: 6 bytes (when VIRTIO_NET_F_MAC) */

#define STATUS_ACKNOWLEDGE 0x01 /* the guest has noticed the device */
#define STATUS_DRIVER 0x02      /* the guest knows how to drive it */
#define STATUS_DRIVER_OK 0x04   /* the driver is ready */
#define STATUS_FAILED 0x80

#define FEATURE_MAC (1u << 5) /* the device has a MAC address in its configuration */

#define QUEUE_RX 0
#define QUEUE_TX 1

#define DESC_WRITE 2 /* the device writes this buffer (receive) */

/* A split virtqueue (1.1, 2.6): descriptor table, available ring, used ring. */
typedef struct
{
    uint64_t address;
    uint32_t length;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed)) descriptor_t;

typedef struct
{
    uint16_t flags;
    uint16_t index;
    uint16_t ring[];
} __attribute__((packed)) available_t;

typedef struct
{
    uint32_t id;
    uint32_t length;
} __attribute__((packed)) used_entry_t;

typedef struct
{
    uint16_t flags;
    uint16_t index;
    used_entry_t ring[];
} __attribute__((packed)) used_t;

/* The header in front of every frame (5.1.6; no merged receive buffers, 10 bytes). */
#define HEADER_SIZE 10
#define BUFFER_SIZE 2048

#define RX_BUFFERS 32
#define TX_BUFFERS 8

typedef struct
{
    uint16_t size;       /* entries, a power of two */
    descriptor_t *descriptors;
    available_t *available;
    used_t *used;
    uint16_t last_used;  /* how far the driver has read the used ring */
} queue_t;

typedef struct
{
    net_device_t net;
    uint16_t io;
    queue_t rx;
    queue_t tx;
    uint8_t *rx_buffers;
    uint32_t rx_buffers_phys;
    uint8_t *tx_buffers;
    uint32_t tx_buffers_phys;
    unsigned int tx_next;
    unsigned int tx_inflight;
} virtio_net_t;

static virtio_net_t card;
static int present;

/* The size of the rings of a queue with n entries, in bytes: used ring on its own 4 KiB boundary. */
static uint32_t queue_bytes(uint16_t n, uint32_t *used_offset)
{
    uint32_t available_end = 16u * n + 6u + 2u * n;
    uint32_t offset = (available_end + 4095) & ~4095u;

    *used_offset = offset;

    return offset + 6u + 8u * n;
}

static int setup_queue(queue_t *q, uint16_t index)
{
    outw(card.io + REG_QUEUE_SELECT, index);

    uint16_t size = inw(card.io + REG_QUEUE_SIZE);

    if (size == 0)
        return -1;

    uint32_t used_offset;
    uint32_t bytes = queue_bytes(size, &used_offset);
    uint32_t phys;
    uint8_t *memory = hal_dma_alloc(bytes, &phys);

    if (memory == NULL)
        return -1;

    q->size = size;
    q->descriptors = (descriptor_t *)memory;
    q->available = (available_t *)(memory + 16u * size);
    q->used = (used_t *)(memory + used_offset);
    q->last_used = 0;

    /* The device wants the page frame number of the whole structure. */
    outl(card.io + REG_QUEUE_ADDRESS, phys >> 12);

    return 0;
}

static void notify(uint16_t queue)
{
    outw(card.io + REG_QUEUE_NOTIFY, queue);
}

/* Give receive buffer i (descriptor i) to the device. */
static void post_receive(unsigned int i)
{
    queue_t *q = &card.rx;

    q->descriptors[i].address = card.rx_buffers_phys + i * BUFFER_SIZE;
    q->descriptors[i].length = BUFFER_SIZE;
    q->descriptors[i].flags = DESC_WRITE;
    q->descriptors[i].next = 0;

    q->available->ring[q->available->index % q->size] = (uint16_t)i;
    asm volatile("" ::: "memory");
    q->available->index++;
}

static int virtio_send(net_device_t *device, const void *frame, uint32_t length)
{
    (void)device;

    if (length > BUFFER_SIZE - HEADER_SIZE || length == 0)
        return -1;

    queue_t *q = &card.tx;
    unsigned int i = card.tx_next;

    /* Recycle finished buffers; if all are in use, wait for the device (up to 100 ms). */
    uint64_t end = hal_timer_ns() + 100000000u;

    for (;;)
    {
        while (q->last_used != q->used->index)
        {
            q->last_used++;
            card.tx_inflight--;
        }

        if (card.tx_inflight < TX_BUFFERS)
            break;

        if (hal_timer_ns() > end)
            return -1;
    }

    uint8_t *buffer = card.tx_buffers + i * BUFFER_SIZE;

    k_memset(buffer, 0, HEADER_SIZE); /* no checksum offload, no segmentation offload */
    k_memcpy(buffer + HEADER_SIZE, frame, length);

    q->descriptors[i].address = card.tx_buffers_phys + i * BUFFER_SIZE;
    q->descriptors[i].length = HEADER_SIZE + length;
    q->descriptors[i].flags = 0;
    q->descriptors[i].next = 0;

    q->available->ring[q->available->index % q->size] = (uint16_t)i;
    asm volatile("" ::: "memory");
    q->available->index++;
    card.tx_inflight++;
    card.tx_next = (i + 1) % TX_BUFFERS;
    notify(QUEUE_TX);

    return 0;
}

static void virtio_poll(net_device_t *device)
{
    queue_t *q = &card.rx;

    while (q->last_used != q->used->index)
    {
        used_entry_t entry = q->used->ring[q->last_used % q->size];

        q->last_used++;

        if (entry.length > HEADER_SIZE && entry.id < RX_BUFFERS)
            net_receive(device, card.rx_buffers + entry.id * BUFFER_SIZE + HEADER_SIZE,
                        entry.length - HEADER_SIZE);

        post_receive(entry.id);
        notify(QUEUE_RX);
    }
}

static void virtio_interrupt(void)
{
    inb(card.io + REG_ISR); /* reading acknowledges; poll does the work */
}

static int virtio_probe(const hal_pci_device_t *pci)
{
    if (present || pci->bar[0].type != HAL_PCI_BAR_IO)
        return -1;

    hal_pci_enable(pci, HAL_PCI_COMMAND_IO | HAL_PCI_COMMAND_BUS_MASTER);
    card.io = (uint16_t)pci->bar[0].base;

    /* The initialisation sequence of 3.1: reset, acknowledge, driver, features, queues, driver ok. */
    outb(card.io + REG_STATUS, 0);
    outb(card.io + REG_STATUS, STATUS_ACKNOWLEDGE);
    outb(card.io + REG_STATUS, STATUS_ACKNOWLEDGE | STATUS_DRIVER);

    uint32_t features = inl(card.io + REG_DEVICE_FEATURES);

    if (!(features & FEATURE_MAC))
    {
        outb(card.io + REG_STATUS, STATUS_FAILED);

        return -1;
    }

    outl(card.io + REG_GUEST_FEATURES, FEATURE_MAC); /* the only feature we use */

    for (int i = 0; i < 6; i++)
        card.net.mac[i] = inb(card.io + REG_MAC + i);

    if (setup_queue(&card.rx, QUEUE_RX) != 0 || setup_queue(&card.tx, QUEUE_TX) != 0)
    {
        outb(card.io + REG_STATUS, STATUS_FAILED);

        return -1;
    }

    card.rx_buffers = hal_dma_alloc(RX_BUFFERS * BUFFER_SIZE, &card.rx_buffers_phys);
    card.tx_buffers = hal_dma_alloc(TX_BUFFERS * BUFFER_SIZE, &card.tx_buffers_phys);

    if (card.rx_buffers == NULL || card.tx_buffers == NULL)
    {
        outb(card.io + REG_STATUS, STATUS_FAILED);

        return -1;
    }

    for (unsigned int i = 0; i < RX_BUFFERS; i++)
        post_receive(i);

    card.net.name = "virtio-net";
    card.net.send = virtio_send;
    card.net.poll = virtio_poll;
    card.net.priv = &card;

    if (pci->irq_pin != 0 && pci->irq_line < 16 && hal_irq_register(pci->irq_line, virtio_interrupt) == 0)
        hal_irq_unmask(pci->irq_line);

    outb(card.io + REG_STATUS, STATUS_ACKNOWLEDGE | STATUS_DRIVER | STATUS_DRIVER_OK);
    notify(QUEUE_RX);

    present = 1;

    return net_register(&card.net);
}

const hal_pci_driver_t virtio_net_pci_driver = {
    .name = "virtio-net",
    .vendor_id = 0x1AF4,
    .device_id = 0x1000,
    .probe = virtio_probe,
};
