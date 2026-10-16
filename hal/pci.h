/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef HAL_PCI_H
#define HAL_PCI_H

#include <stddef.h>
#include <stdint.h>

/*
 * The PCI bus: how a machine's add-in devices (network cards, sound
 * cards, graphics cards) are found and reached. A device is identified
 * by (bus, device, function), has a vendor and device number, a class,
 * and up to six "base address registers" (BARs) that say where its
 * registers live: in I/O ports or in memory. Implemented once per
 * platform (platform/<platform>/pci/hal_pci.c).
 */

#define HAL_PCI_MAX_DEVICES 64

#define HAL_PCI_BAR_NONE 0
#define HAL_PCI_BAR_IO 1  /* I/O port range */
#define HAL_PCI_BAR_MEM 2 /* memory-mapped registers or memory */

typedef struct
{
    uint8_t type;         /* HAL_PCI_BAR_* */
    uint8_t prefetchable; /* memory that may be cached/combined (frame buffers) */
    uint64_t base;        /* port number or physical address */
    uint64_t size;        /* bytes */
} hal_pci_bar_t;

typedef struct
{
    uint8_t bus;
    uint8_t device;   /* 0-31 */
    uint8_t function; /* 0-7 */
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code; /* e.g. 0x02 = network, 0x03 = display, 0x04 = multimedia */
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t header_type;  /* 0 = endpoint, 1 = PCI-to-PCI bridge */
    uint8_t irq_line;     /* the legacy interrupt (8259) line the firmware chose, 0xFF = none */
    uint8_t irq_pin;      /* 0 = none, 1 = INTA ... 4 = INTD */
    hal_pci_bar_t bar[6]; /* only filled for header type 0 */
} hal_pci_device_t;

/* Command register bits (PCI Local Bus Specification 3.0, 6.2.2). */
#define HAL_PCI_COMMAND_IO 0x1u          /* respond to I/O port accesses */
#define HAL_PCI_COMMAND_MEMORY 0x2u      /* respond to memory accesses */
#define HAL_PCI_COMMAND_BUS_MASTER 0x4u  /* may start DMA */

/* Number of devices found (functions count as separate devices). */
unsigned int hal_pci_device_count(void);

/* Device number index in the order found. Returns 0, or -1 if out of range. */
int hal_pci_device(unsigned int index, hal_pci_device_t *device);

/* First device with this vendor and device number, from index start on. Returns its index or -1. */
int hal_pci_find(uint16_t vendor_id, uint16_t device_id, unsigned int start);

/* First device of this class and subclass, from index start on. Returns its index or -1. */
int hal_pci_find_class(uint8_t class_code, uint8_t subclass, unsigned int start);

/* Configuration space access (offset must be aligned to size; size is 1, 2 or 4). */
uint32_t hal_pci_config_read(const hal_pci_device_t *device, unsigned int offset, unsigned int size);
void hal_pci_config_write(const hal_pci_device_t *device, unsigned int offset, unsigned int size, uint32_t value);

/* Turn on the HAL_PCI_COMMAND_* bits given (others are kept). */
void hal_pci_enable(const hal_pci_device_t *device, uint32_t command_bits);

/*
 * Map memory BAR number bar so the kernel can use it, uncached. Returns
 * a pointer to its first byte, or NULL if the BAR is not a memory BAR or
 * mapping fails. The mapping is never removed.
 */
void *hal_pci_map_bar(const hal_pci_device_t *device, unsigned int bar);

/*
 * A driver for PCI devices. Drivers list the devices they handle by
 * vendor/device number (or by class if vendor_id is 0) and are called
 * by hal_pci_probe_drivers() for each match.
 */
typedef struct
{
    const char *name;
    uint16_t vendor_id; /* 0 = match on class_code/subclass */
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    /* Set the device up. Returns 0 on success, -1 if this driver cannot use it. */
    int (*probe)(const hal_pci_device_t *device);
} hal_pci_driver_t;

/* Give each device to the first driver of the platform's driver list that matches and accepts it. Returns how many were taken. */
unsigned int hal_pci_probe_drivers(void);

#endif
