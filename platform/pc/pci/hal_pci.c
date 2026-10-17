/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal/pci.h"

#include "pci.h"
#include "pci_bar.h"

#include "platform/pc/vmap/vmap.h"

#include "arch/x86/io/io.h"

#include "hal/mmu.h"


/*
 * Configuration mechanism #1 (PCI Local Bus Specification 3.0, 3.2.2.3.2;
 * OSDev Wiki "PCI"): write the address of a 32-bit configuration
 * register to port 0xCF8, then read or write its contents at 0xCFC.
 *
 *   31  30..24   23..16  15..11  10..8   7..2        1..0
 *   [E] reserved bus     device  function register#  00
 */
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define PCI_VENDOR_ID 0x00
#define PCI_COMMAND 0x04
#define PCI_CLASS_REVISION 0x08
#define PCI_HEADER_TYPE 0x0E
#define PCI_BAR0 0x10
#define PCI_SECONDARY_BUS 0x19
#define PCI_INTERRUPT_LINE 0x3C

static hal_pci_device_t devices[HAL_PCI_MAX_DEVICES];
static unsigned int device_count;

static uint32_t address_of(uint8_t bus, uint8_t device, uint8_t function, unsigned int offset)
{
    return 0x80000000u | (uint32_t)bus << 16 | (uint32_t)device << 11 |
           (uint32_t)function << 8 | (offset & 0xFCu);
}

static uint32_t read_raw(uint8_t bus, uint8_t device, uint8_t function,
                         unsigned int offset, unsigned int size)
{
    outl(PCI_CONFIG_ADDRESS, address_of(bus, device, function, offset));

    uint32_t word = inl(PCI_CONFIG_DATA);
    unsigned int shift = (offset & 3) * 8;

    if (size == 4)
        return word;

    return (word >> shift) & (size == 2 ? 0xFFFFu : 0xFFu);
}

static void write_raw(uint8_t bus, uint8_t device, uint8_t function,
                      unsigned int offset, unsigned int size, uint32_t value)
{
    if (size == 4)
    {
        outl(PCI_CONFIG_ADDRESS, address_of(bus, device, function, offset));
        outl(PCI_CONFIG_DATA, value);

        return;
    }

    /* Read-modify-write the containing 32-bit register. */
    uint32_t mask = (size == 2 ? 0xFFFFu : 0xFFu) << ((offset & 3) * 8);
    uint32_t word = read_raw(bus, device, function, offset & ~3u, 4);

    word = (word & ~mask) | ((value << ((offset & 3) * 8)) & mask);
    outl(PCI_CONFIG_ADDRESS, address_of(bus, device, function, offset));
    outl(PCI_CONFIG_DATA, word);
}

uint32_t hal_pci_config_read(const hal_pci_device_t *device, unsigned int offset, unsigned int size)
{
    return read_raw(device->bus, device->device, device->function, offset, size);
}

void hal_pci_config_write(const hal_pci_device_t *device, unsigned int offset, unsigned int size, uint32_t value)
{
    write_raw(device->bus, device->device, device->function, offset, size, value);
}

void hal_pci_enable(const hal_pci_device_t *device, uint32_t command_bits)
{
    uint32_t command = hal_pci_config_read(device, PCI_COMMAND, 2);

    hal_pci_config_write(device, PCI_COMMAND, 2, command | command_bits);
}

/* Find the size of each BAR by writing ones to it (with decoding off meanwhile). */
static void read_bars(hal_pci_device_t *dev)
{
    uint32_t command = hal_pci_config_read(dev, PCI_COMMAND, 2);

    hal_pci_config_write(dev, PCI_COMMAND, 2,
                         command & ~(HAL_PCI_COMMAND_IO | HAL_PCI_COMMAND_MEMORY));

    for (unsigned int i = 0; i < 6;)
    {
        unsigned int reg = PCI_BAR0 + 4 * i;
        uint32_t original = hal_pci_config_read(dev, reg, 4);
        unsigned int registers = pci_bar_registers(original);
        uint32_t original_high = 0;
        uint32_t sized_high = 0;

        hal_pci_config_write(dev, reg, 4, 0xFFFFFFFFu);

        uint32_t sized = hal_pci_config_read(dev, reg, 4);

        hal_pci_config_write(dev, reg, 4, original);

        if (registers == 2 && i + 1 < 6)
        {
            original_high = hal_pci_config_read(dev, reg + 4, 4);
            hal_pci_config_write(dev, reg + 4, 4, 0xFFFFFFFFu);
            sized_high = hal_pci_config_read(dev, reg + 4, 4);
            hal_pci_config_write(dev, reg + 4, 4, original_high);
        }

        pci_bar_decode(original, original_high, sized, sized_high, &dev->bar[i]);
        i += registers;
    }

    hal_pci_config_write(dev, PCI_COMMAND, 2, command);
}

/* Returns 0 if the function was added, -1 if the table is full. */
static int add_function(uint8_t bus, uint8_t device, uint8_t function)
{
    if (device_count == HAL_PCI_MAX_DEVICES)
        return -1;

    hal_pci_device_t *dev = &devices[device_count++];
    uint32_t ids = read_raw(bus, device, function, PCI_VENDOR_ID, 4);
    uint32_t class_rev = read_raw(bus, device, function, PCI_CLASS_REVISION, 4);

    for (unsigned int i = 0; i < sizeof(*dev); i++)
        ((uint8_t *)dev)[i] = 0;

    dev->bus = bus;
    dev->device = device;
    dev->function = function;
    dev->vendor_id = (uint16_t)ids;
    dev->device_id = (uint16_t)(ids >> 16);
    dev->revision = (uint8_t)class_rev;
    dev->prog_if = (uint8_t)(class_rev >> 8);
    dev->subclass = (uint8_t)(class_rev >> 16);
    dev->class_code = (uint8_t)(class_rev >> 24);
    dev->header_type = (uint8_t)(read_raw(bus, device, function, PCI_HEADER_TYPE, 1) & 0x7F);
    dev->irq_line = (uint8_t)read_raw(bus, device, function, PCI_INTERRUPT_LINE, 1);
    dev->irq_pin = (uint8_t)read_raw(bus, device, function, PCI_INTERRUPT_LINE + 1, 1);

    if (dev->header_type == 0)
        read_bars(dev);

    return 0;
}

static void scan_bus(uint8_t bus, unsigned int depth);

/* Buses that were scanned (a bridge's secondary bus is scanned through the bridge, not again as a root). */
static uint8_t bus_scanned[32];

static void scan_device(uint8_t bus, uint8_t device, unsigned int depth)
{
    if ((read_raw(bus, device, 0, PCI_VENDOR_ID, 2)) == 0xFFFF)
        return;

    unsigned int functions = (read_raw(bus, device, 0, PCI_HEADER_TYPE, 1) & 0x80) ? 8 : 1;

    for (unsigned int function = 0; function < functions; function++)
    {
        if (read_raw(bus, device, (uint8_t)function, PCI_VENDOR_ID, 2) == 0xFFFF)
            continue;

        if (add_function(bus, device, (uint8_t)function) != 0)
            return;

        const hal_pci_device_t *added = &devices[device_count - 1];

        /* PCI-to-PCI bridge: the devices behind it are on the secondary bus. */
        if (added->class_code == 0x06 && added->subclass == 0x04 && added->header_type == 1 &&
            depth < 8)
        {
            scan_bus((uint8_t)read_raw(bus, device, (uint8_t)function, PCI_SECONDARY_BUS, 1),
                     depth + 1);
        }
    }
}

static void scan_bus(uint8_t bus, unsigned int depth)
{
    if (bus_scanned[bus / 8] & (1u << (bus % 8)))
        return;

    bus_scanned[bus / 8] |= (uint8_t)(1u << (bus % 8));

    for (uint8_t device = 0; device < 32; device++)
        scan_device(bus, device, depth);
}

unsigned int pci_init(void)
{
    /* Mechanism #1 exists if writing the enable bit to 0xCF8 reads back (OSDev Wiki "PCI"). */
    outl(PCI_CONFIG_ADDRESS, 0x80000000u);

    if (inl(PCI_CONFIG_ADDRESS) != 0x80000000u)
        return 0;

    device_count = 0;

    for (unsigned int i = 0; i < sizeof(bus_scanned); i++)
        bus_scanned[i] = 0;

    scan_bus(0, 0);

    /*
     * Bus 0 only leads to the buses behind its own bridges. A machine with several host bridges (a Xeon
     * workstation has one root bus per uncore stack: the NVMe drive and the graphics card are on
     * buses 0x17 and 0x65 of the P520) has more root buses. Look at every remaining bus: a bus that does
     * not exist answers 0xFFFF everywhere, which costs 32 reads. Buses behind a bridge were scanned
     * through it (the bridge's bus number is always lower, so it has been visited by now).
     */
    for (unsigned int bus = 1; bus < 256; bus++)
        scan_bus((uint8_t)bus, 0);

    return device_count;
}

unsigned int hal_pci_device_count(void)
{
    return device_count;
}

int hal_pci_device(unsigned int index, hal_pci_device_t *device)
{
    if (index >= device_count || device == NULL)
        return -1;

    *device = devices[index];

    return 0;
}

int hal_pci_find(uint16_t vendor_id, uint16_t device_id, unsigned int start)
{
    for (unsigned int i = start; i < device_count; i++)
    {
        if (devices[i].vendor_id == vendor_id && devices[i].device_id == device_id)
            return (int)i;
    }

    return -1;
}

int hal_pci_find_class(uint8_t class_code, uint8_t subclass, unsigned int start)
{
    for (unsigned int i = start; i < device_count; i++)
    {
        if (devices[i].class_code == class_code && devices[i].subclass == subclass)
            return (int)i;
    }

    return -1;
}

void *hal_pci_map_bar(const hal_pci_device_t *device, unsigned int bar)
{
    if (bar >= 6 || device->bar[bar].type != HAL_PCI_BAR_MEM)
        return NULL;

#ifndef HAL_PHYS_BASE
    if (device->bar[bar].base + device->bar[bar].size > 0x100000000ull)
        return NULL; /* the 32-bit kernel cannot address it */
#endif

    return vmap(device->bar[bar].base, (size_t)device->bar[bar].size,
                HAL_MMU_WRITABLE | HAL_MMU_UNCACHED);
}
