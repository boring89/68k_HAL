/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "e1000.h"

#include "hal/dma.h"
#include "hal/irq.h"
#include "hal/timer.h"

#include "kernel/lib/k_memory.h"
#include "kernel/net/net.h"
#include "kernel/printf/printf.h"

/*
 * Register offsets (Intel 8254x manual, chapter 13 "Register
 * Descriptions"). All registers are 32 bits, accessed as memory.
 */
#define REG_CTRL 0x0000   /* device control */
#define REG_STATUS 0x0008
#define REG_ICR 0x00C0    /* interrupt cause, read to acknowledge */
#define REG_IMS 0x00D0    /* interrupt mask set */
#define REG_IMC 0x00D8    /* interrupt mask clear */
#define REG_RCTL 0x0100   /* receive control */
#define REG_TCTL 0x0400   /* transmit control */
#define REG_TIPG 0x0410   /* transmit inter-packet gap */
#define REG_RDBAL 0x2800  /* receive descriptor ring: base low/high, length, head, tail */
#define REG_RDBAH 0x2804
#define REG_RDLEN 0x2808
#define REG_RDH 0x2810
#define REG_RDT 0x2818
#define REG_TDBAL 0x3800
#define REG_TDBAH 0x3804
#define REG_TDLEN 0x3808
#define REG_TDH 0x3810
#define REG_TDT 0x3818
#define REG_MTA 0x5200    /* multicast table array, 128 entries */
#define REG_RAL0 0x5400   /* receive address 0: MAC low */
#define REG_RAH0 0x5404   /* MAC high, bit 31 = address valid */

#define CTRL_FD (1u << 0)
#define CTRL_ASDE (1u << 5)  /* auto speed detection */
#define CTRL_SLU (1u << 6)   /* set link up */
#define CTRL_RST (1u << 26)  /* device reset */

#define RCTL_EN (1u << 1)
#define RCTL_BAM (1u << 15)    /* accept broadcast */
#define RCTL_SECRC (1u << 26)  /* strip the Ethernet CRC */
/* BSIZE = 00 with BSEX = 0: 2048-byte buffers. */

#define TCTL_EN (1u << 1)
#define TCTL_PSP (1u << 3)     /* pad short packets */
#define TCTL_CT (0x0Fu << 4)   /* collision threshold */
#define TCTL_COLD (0x40u << 12) /* collision distance, full duplex */

#define INT_TXDW (1u << 0)
#define INT_LSC (1u << 2)
#define INT_RXDMT0 (1u << 4)
#define INT_RXO (1u << 6)
#define INT_RXT0 (1u << 7)

#define RAH_AV (1u << 31)

/* Legacy receive descriptor (manual 3.2.3). */
typedef struct
{
    uint64_t address;
    uint16_t length;
    uint16_t checksum;
    uint8_t status; /* bit 0 DD (done), bit 1 EOP (end of packet) */
    uint8_t errors;
    uint16_t special;
} __attribute__((packed)) rx_descriptor_t;

/* Legacy transmit descriptor (manual 3.3.3). */
typedef struct
{
    uint64_t address;
    uint16_t length;
    uint8_t cso;
    uint8_t command; /* bit 0 EOP, bit 1 IFCS (add CRC), bit 3 RS (report status) */
    uint8_t status;  /* bit 0 DD */
    uint8_t css;
    uint16_t special;
} __attribute__((packed)) tx_descriptor_t;

#define STATUS_DD 0x01
#define STATUS_EOP 0x02
#define TX_CMD (0x01 | 0x02 | 0x08)

#define RX_COUNT 32
#define TX_COUNT 8
#define BUFFER_SIZE 2048

typedef struct
{
    net_device_t net;
    volatile uint8_t *registers;
    volatile rx_descriptor_t *rx;
    volatile tx_descriptor_t *tx;
    uint8_t *rx_buffers;
    uint8_t *tx_buffers;
    uint32_t tx_buffers_phys;
    unsigned int rx_next; /* next descriptor the driver reads */
    unsigned int tx_next;
    volatile uint32_t interrupts; /* counted by the handler */
} e1000_t;

static e1000_t card;

static uint32_t read_register(uint32_t offset)
{
    return *(volatile uint32_t *)(card.registers + offset);
}

static void write_register(uint32_t offset, uint32_t value)
{
    *(volatile uint32_t *)(card.registers + offset) = value;
}

static void e1000_interrupt(void)
{
    /* Reading ICR acknowledges the interrupt; the work is done by poll. */
    if (read_register(REG_ICR) != 0)
        card.interrupts++;
}

static int e1000_send(net_device_t *device, const void *frame, uint32_t length)
{
    (void)device;

    if (length > BUFFER_SIZE || length == 0)
        return -1;

    volatile tx_descriptor_t *d = &card.tx[card.tx_next];

    /* The descriptor is free if it never was used or the card finished with it. */
    uint64_t end = hal_timer_ns() + 100000000u;

    while (d->command != 0 && !(d->status & STATUS_DD))
    {
        if (hal_timer_ns() > end)
            return -1;
    }

    k_memcpy(card.tx_buffers + card.tx_next * BUFFER_SIZE, frame, length);
    d->length = (uint16_t)length;
    d->cso = 0;
    d->css = 0;
    d->special = 0;
    d->status = 0;
    d->command = TX_CMD;

    card.tx_next = (card.tx_next + 1) % TX_COUNT;
    write_register(REG_TDT, card.tx_next); /* hand the descriptor to the card */

    return 0;
}

static void e1000_poll(net_device_t *device)
{
    for (;;)
    {
        volatile rx_descriptor_t *d = &card.rx[card.rx_next];

        if (!(d->status & STATUS_DD))
            break;

        uint32_t length = d->length;

        if ((d->status & STATUS_EOP) && d->errors == 0 && length <= BUFFER_SIZE)
            net_receive(device, card.rx_buffers + card.rx_next * BUFFER_SIZE, length);

        /* Give the descriptor back to the card: it owns everything up to the tail. */
        d->status = 0;
        write_register(REG_RDT, card.rx_next);
        card.rx_next = (card.rx_next + 1) % RX_COUNT;
    }
}

/* Spin for roughly the given microseconds. */
static void delay_us(uint32_t us)
{
    uint64_t end = hal_timer_ns() + (uint64_t)us * 1000u;

    while (hal_timer_ns() < end)
        ;
}

static int e1000_probe(const hal_pci_device_t *pci)
{
    if (card.registers != NULL)
        return -1; /* one card */

    hal_pci_enable(pci, HAL_PCI_COMMAND_MEMORY | HAL_PCI_COMMAND_BUS_MASTER);

    card.registers = hal_pci_map_bar(pci, 0);

    if (card.registers == NULL)
        return -1;

    /* Reset, then switch all interrupts off until the rings are ready. */
    write_register(REG_IMC, 0xFFFFFFFFu);
    write_register(REG_CTRL, read_register(REG_CTRL) | CTRL_RST);
    delay_us(20000);
    write_register(REG_IMC, 0xFFFFFFFFu);
    read_register(REG_ICR);

    write_register(REG_CTRL, read_register(REG_CTRL) | CTRL_SLU | CTRL_ASDE | CTRL_FD);

    /* The MAC address was loaded from the EEPROM into receive address 0. */
    uint32_t low = read_register(REG_RAL0);
    uint32_t high = read_register(REG_RAH0);

    if (!(high & RAH_AV))
    {
        card.registers = NULL;

        return -1;
    }

    for (int i = 0; i < 4; i++)
        card.net.mac[i] = (uint8_t)(low >> (8 * i));

    card.net.mac[4] = (uint8_t)high;
    card.net.mac[5] = (uint8_t)(high >> 8);

    for (int i = 0; i < 128; i++)
        write_register(REG_MTA + 4 * i, 0);

    /* One DMA block: the receive ring, the transmit ring, then the buffers. */
    uint32_t rings_phys;
    uint32_t buffers_phys;
    void *rings = hal_dma_alloc(RX_COUNT * sizeof(rx_descriptor_t) + TX_COUNT * sizeof(tx_descriptor_t), &rings_phys);
    uint8_t *buffers = hal_dma_alloc((RX_COUNT + TX_COUNT) * BUFFER_SIZE, &buffers_phys);

    if (rings == NULL || buffers == NULL)
    {
        card.registers = NULL;

        return -1;
    }

    card.rx = rings;
    card.tx = (volatile tx_descriptor_t *)((uint8_t *)rings + RX_COUNT * sizeof(rx_descriptor_t));
    card.rx_buffers = buffers;
    card.tx_buffers = buffers + RX_COUNT * BUFFER_SIZE;
    card.tx_buffers_phys = buffers_phys + RX_COUNT * BUFFER_SIZE;

    for (int i = 0; i < RX_COUNT; i++)
        card.rx[i].address = buffers_phys + (uint32_t)i * BUFFER_SIZE;

    for (int i = 0; i < TX_COUNT; i++)
        card.tx[i].address = card.tx_buffers_phys + (uint32_t)i * BUFFER_SIZE;

    write_register(REG_RDBAL, rings_phys);
    write_register(REG_RDBAH, 0);
    write_register(REG_RDLEN, RX_COUNT * sizeof(rx_descriptor_t));
    write_register(REG_RDH, 0);
    write_register(REG_RDT, RX_COUNT - 1); /* the card may fill descriptors 0 .. RX_COUNT-2 */

    write_register(REG_TDBAL, rings_phys + RX_COUNT * sizeof(rx_descriptor_t));
    write_register(REG_TDBAH, 0);
    write_register(REG_TDLEN, TX_COUNT * sizeof(tx_descriptor_t));
    write_register(REG_TDH, 0);
    write_register(REG_TDT, 0);

    write_register(REG_TIPG, 0x0060200Au); /* IPGT 10, IPGR1 8, IPGR2 6 (manual 13.4.34) */
    write_register(REG_TCTL, TCTL_EN | TCTL_PSP | TCTL_CT | TCTL_COLD);
    write_register(REG_RCTL, RCTL_EN | RCTL_BAM | RCTL_SECRC);

    card.net.name = "e1000";
    card.net.send = e1000_send;
    card.net.poll = e1000_poll;
    card.net.priv = &card;

    if (pci->irq_pin != 0 && pci->irq_line < 16 &&
        hal_irq_register(pci->irq_line, e1000_interrupt) == 0)
    {
        hal_irq_unmask(pci->irq_line);
        write_register(REG_IMS, INT_RXT0 | INT_RXO | INT_RXDMT0 | INT_LSC);
    }

    return net_register(&card.net);
}

const hal_pci_driver_t e1000_pci_driver = {
    .name = "e1000",
    .vendor_id = 0x8086,
    .device_id = E1000_DEVICE_82540EM,
    .probe = e1000_probe,
};
