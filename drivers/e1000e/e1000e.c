/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "e1000e.h"

#include "hal/dma.h"
#include "hal/timer.h"

#include "kernel/lib/k_memory.h"
#include "kernel/log/log.h"
#include "kernel/net/net.h"
#include "kernel/printf/printf.h"

/* Registers (FreeBSD sys/dev/e1000/e1000_regs.h; same as the 8254x for the ones used by both). */
#define REG_CTRL 0x0000
#define REG_STATUS 0x0008
#define REG_ICR 0x00C0
#define REG_IMC 0x00D8
#define REG_RCTL 0x0100
#define REG_TCTL 0x0400
#define REG_TIPG 0x0410
#define REG_RDBAL 0x2800
#define REG_RDBAH 0x2804
#define REG_RDLEN 0x2808
#define REG_RDH 0x2810
#define REG_RDT 0x2818
#define REG_TDBAL 0x3800
#define REG_TDBAH 0x3804
#define REG_TDLEN 0x3808
#define REG_TDH 0x3810
#define REG_TDT 0x3818
#define REG_TXDCTL 0x3828
#define REG_TARC0 0x3840
#define REG_MTA 0x5200
#define REG_RAL0 0x5400
#define REG_RAH0 0x5404
#define REG_IOSFPC 0x0F28
#define REG_FEXTNVM11 0x5BBC

#define CTRL_FD (1u << 0)
#define CTRL_ASDE (1u << 5)
#define CTRL_SLU (1u << 6)
#define CTRL_FRCSPD (1u << 11)
#define CTRL_FRCDPX (1u << 12)
#define CTRL_RST (1u << 26)

#define STATUS_LU (1u << 1)

#define RCTL_EN (1u << 1)
#define RCTL_BAM (1u << 15)
#define RCTL_SECRC (1u << 26)
#define RCTL_RDMTS_HEX 0x00010000u /* receive descriptor minimum threshold: 1/8 (FreeBSD E1000_RCTL_RDMTS_HEX) */

#define TCTL_EN (1u << 1)
#define TCTL_PSP (1u << 3)
#define TCTL_CT (0x0Fu << 4)
#define TCTL_COLD (0x3Fu << 12)
#define TCTL_RTLC (1u << 24)
#define TCTL_MULR (1u << 28)

/* Transmit descriptor control: prefetch 32 / host 1 / write-back 1, granularity = descriptors; bit 22 required on the PCH. */
#define TXDCTL_PCH ((32u) | (1u << 8) | (1u << 16) | (1u << 24) | (1u << 22))
#define TXDCTL_82574 ((32u) | (1u << 8) | (1u << 16) | (1u << 24) | (1u << 22))

/* The I219 hang workaround (FreeBSD em_flush_desc_rings): a status word in PCI configuration space. */
#define PCI_DESC_RING_STATUS 0xE4
#define FLUSH_DESC_REQUIRED 0x100u
#define FEXTNVM11_DISABLE_MULR_FIX (1u << 13)
#define TARC0_CB_MULTIQ_3_REQ (3u << 28)
#define TARC0_CB_MULTIQ_2_REQ (2u << 28)

#define RAH_AV (1u << 31)

typedef struct
{
    uint64_t address;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed)) rx_descriptor_t;

typedef struct
{
    uint64_t address;
    uint16_t length;
    uint8_t cso;
    uint8_t command;
    uint8_t status;
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
    int pch; /* an I219-style part (the PCH LAN controller), not the 82574 */
    volatile rx_descriptor_t *rx;
    volatile tx_descriptor_t *tx;
    uint8_t *rx_buffers;
    uint8_t *tx_buffers;
    unsigned int rx_next;
    unsigned int tx_next;
} card_t;

static card_t card;

static uint32_t read_register(uint32_t offset)
{
    return *(volatile uint32_t *)(card.registers + offset);
}

static void write_register(uint32_t offset, uint32_t value)
{
    *(volatile uint32_t *)(card.registers + offset) = value;
}

static void delay_us(uint32_t us)
{
    uint64_t end = hal_timer_ns() + (uint64_t)us * 1000u;

    while (hal_timer_ns() < end)
        ;
}

static int e1000e_send(net_device_t *device, const void *frame, uint32_t length)
{
    (void)device;

    if (length > BUFFER_SIZE || length == 0)
        return -1;

    volatile tx_descriptor_t *d = &card.tx[card.tx_next];
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
    write_register(REG_TDT, card.tx_next);

    return 0;
}

static void e1000e_poll(net_device_t *device)
{
    for (;;)
    {
        volatile rx_descriptor_t *d = &card.rx[card.rx_next];

        if (!(d->status & STATUS_DD))
            break;

        uint32_t length = d->length;

        if ((d->status & STATUS_EOP) && d->errors == 0 && length <= BUFFER_SIZE)
            net_receive(device, card.rx_buffers + card.rx_next * BUFFER_SIZE, length);

        d->status = 0;
        write_register(REG_RDT, card.rx_next);
        card.rx_next = (card.rx_next + 1) % RX_COUNT;
    }
}

/*
 * I219 only (FreeBSD em_flush_desc_rings): if the chip is stuck waiting for descriptors that
 * the firmware's driver left in its rings, a reset hangs it until the next PCI reset. Pushing
 * one dummy descriptor through the transmit ring (ours, already programmed) un-sticks it.
 */
static void flush_descriptor_rings(const hal_pci_device_t *pci, uint32_t tx_ring_phys)
{
    write_register(REG_FEXTNVM11, read_register(REG_FEXTNVM11) | FEXTNVM11_DISABLE_MULR_FIX);

    if (!(hal_pci_config_read(pci, PCI_DESC_RING_STATUS, 2) & FLUSH_DESC_REQUIRED))
        return;

    /* Our transmit ring is set up (base, length) and its first descriptor is a dummy 512-byte frame (the ring itself as data). */
    volatile tx_descriptor_t *d = &card.tx[0];

    write_register(REG_TCTL, read_register(REG_TCTL) | TCTL_EN);
    d->address = tx_ring_phys;
    d->length = 512;
    d->command = 0x02; /* IFCS, as FreeBSD's flush does */
    d->status = 0;
    write_register(REG_TDT, 0);
    delay_us(250);

    /* Still required: the receive side. Enable it momentarily with the thresholds FreeBSD uses. */
    if (hal_pci_config_read(pci, PCI_DESC_RING_STATUS, 2) & FLUSH_DESC_REQUIRED)
    {
        uint32_t rctl = read_register(REG_RCTL);

        write_register(REG_RCTL, rctl & ~RCTL_EN);
        delay_us(150);
        write_register(0x2828, (read_register(0x2828) & 0xFFFFC000u) | 0x1F | (1u << 8) | (1u << 24));
        write_register(REG_RCTL, rctl | RCTL_EN);
        delay_us(150);
        write_register(REG_RCTL, rctl & ~RCTL_EN);
    }

    d->command = 0;
}

static int e1000e_probe(const hal_pci_device_t *pci)
{
    if (card.registers != NULL)
        return -1;

    hal_pci_enable(pci, HAL_PCI_COMMAND_MEMORY | HAL_PCI_COMMAND_BUS_MASTER);

    card.registers = hal_pci_map_bar(pci, 0);

    if (card.registers == NULL)
        return -1;

    card.pch = pci->device_id != 0x10D3;

    /* The rings first: the I219 flush workaround needs them before the reset. */
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

    uint32_t tx_phys = rings_phys + RX_COUNT * sizeof(rx_descriptor_t);
    uint32_t tx_buffers_phys = buffers_phys + RX_COUNT * BUFFER_SIZE;

    for (int i = 0; i < RX_COUNT; i++)
        card.rx[i].address = buffers_phys + (uint32_t)i * BUFFER_SIZE;

    for (int i = 0; i < TX_COUNT; i++)
        card.tx[i].address = tx_buffers_phys + (uint32_t)i * BUFFER_SIZE;

    if (card.pch)
    {
        write_register(REG_TDBAL, tx_phys);
        write_register(REG_TDBAH, 0);
        write_register(REG_TDLEN, TX_COUNT * sizeof(tx_descriptor_t));
        flush_descriptor_rings(pci, tx_phys);
    }

    /* Interrupts off, receive and transmit off, then reset the MAC (not the PHY: it keeps the link the firmware had). */
    write_register(REG_IMC, 0xFFFFFFFFu);
    write_register(REG_RCTL, 0);
    write_register(REG_TCTL, TCTL_PSP);
    read_register(REG_STATUS);
    delay_us(10000);
    write_register(REG_CTRL, read_register(REG_CTRL) | CTRL_RST);
    delay_us(20000);
    write_register(REG_IMC, 0xFFFFFFFFu);
    read_register(REG_ICR);

    /* Link: let the PHY's autonegotiation decide speed and duplex. */
    uint32_t ctrl = read_register(REG_CTRL);

    ctrl &= ~(CTRL_FRCSPD | CTRL_FRCDPX);
    write_register(REG_CTRL, ctrl | CTRL_SLU | CTRL_ASDE);

    uint32_t low = read_register(REG_RAL0);
    uint32_t high = read_register(REG_RAH0);

    if (!(high & RAH_AV))
    {
        kernel_log(LOG_WARN, "e1000e: no MAC address");
        card.registers = NULL;

        return -1;
    }

    for (int i = 0; i < 4; i++)
        card.net.mac[i] = (uint8_t)(low >> (8 * i));

    card.net.mac[4] = (uint8_t)high;
    card.net.mac[5] = (uint8_t)(high >> 8);

    for (int i = 0; i < 128; i++)
        write_register(REG_MTA + 4 * i, 0);

    write_register(REG_RDBAL, rings_phys);
    write_register(REG_RDBAH, 0);
    write_register(REG_RDLEN, RX_COUNT * sizeof(rx_descriptor_t));
    write_register(REG_RDH, 0);
    write_register(REG_RDT, RX_COUNT - 1);

    write_register(REG_TDBAL, tx_phys);
    write_register(REG_TDBAH, 0);
    write_register(REG_TDLEN, TX_COUNT * sizeof(tx_descriptor_t));
    write_register(REG_TDH, 0);
    write_register(REG_TDT, 0);

    write_register(REG_TXDCTL, card.pch ? TXDCTL_PCH : TXDCTL_82574);
    write_register(REG_TIPG, 0x00602008u); /* IPGT 8 (copper), IPGR1 8, IPGR2 6 */

    uint32_t tctl = TCTL_PSP | TCTL_RTLC | TCTL_EN | TCTL_CT | TCTL_COLD | TCTL_MULR;

    write_register(REG_TCTL, tctl);

    if (card.pch)
    {
        /* I219 (SPT) errata, i218-i219 specification update 1.5.4.5, as in FreeBSD. */
        write_register(REG_IOSFPC, read_register(REG_IOSFPC) | RCTL_RDMTS_HEX);

        uint32_t tarc = read_register(REG_TARC0);

        tarc &= ~TARC0_CB_MULTIQ_3_REQ;
        tarc |= TARC0_CB_MULTIQ_2_REQ;
        write_register(REG_TARC0, tarc);
    }

    write_register(REG_RCTL, RCTL_EN | RCTL_BAM | RCTL_SECRC);

    card.net.name = "e1000e";
    card.net.send = e1000e_send;
    card.net.poll = e1000e_poll;
    card.net.priv = &card;

    /* The link comes up a moment after the reset (autonegotiation takes a second or so). */
    uint64_t end = hal_timer_ns() + 3000000000ull;

    while (!(read_register(REG_STATUS) & STATUS_LU) && hal_timer_ns() < end)
        delay_us(1000);

    kernel_printf("e1000e: %s, link %s\n", card.pch ? "PCH LAN (I219 family)" : "82574L",
                  (read_register(REG_STATUS) & STATUS_LU) ? "up" : "down");

    return net_register(&card.net);
}

/* One entry per device ID (a hal_pci_driver_t matches one ID): 82574L, then the I219 variants. */
const hal_pci_driver_t e1000e_pci_drivers[E1000E_ID_COUNT] = {
    {.name = "e1000e", .vendor_id = 0x8086, .device_id = 0x10D3, .probe = e1000e_probe},
    {.name = "e1000e", .vendor_id = 0x8086, .device_id = 0x15B7, .probe = e1000e_probe}, /* I219-LM (the P520) */
    {.name = "e1000e", .vendor_id = 0x8086, .device_id = 0x15B8, .probe = e1000e_probe}, /* I219-V */
    {.name = "e1000e", .vendor_id = 0x8086, .device_id = 0x15B9, .probe = e1000e_probe}, /* I219-LM3 */
    {.name = "e1000e", .vendor_id = 0x8086, .device_id = 0x156F, .probe = e1000e_probe}, /* I219-LM */
    {.name = "e1000e", .vendor_id = 0x8086, .device_id = 0x1570, .probe = e1000e_probe}, /* I219-V */
    {.name = "e1000e", .vendor_id = 0x8086, .device_id = 0x15D7, .probe = e1000e_probe}, /* I219-LM8 */
    {.name = "e1000e", .vendor_id = 0x8086, .device_id = 0x15D8, .probe = e1000e_probe}, /* I219-V8 */
    {.name = "e1000e", .vendor_id = 0x8086, .device_id = 0x15E3, .probe = e1000e_probe}, /* I219-LM9 */
};
