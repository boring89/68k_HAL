/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal/pci.h"

/*
 * Message signalled interrupts: instead of a wire, the device writes a small message (data) to
 * an address in memory; on x86 that address is the local APIC and the data is the interrupt
 * vector. No wire means no sharing, no need to know how the board routes INTx (which only
 * ACPI's AML code can tell), and it works the same with 8 or 80 devices.
 *
 * Capability layout (PCI Local Bus Specification 3.0, 6.8.1):
 *
 *   +0  id (5)       +1 next pointer     +2 message control (16 bits)
 *   +4  message address (low 32 bits)
 *   64-bit capable:   +8 address high     +12 message data (16 bits)    +16 mask bits
 *   otherwise:        +8 message data     +12 mask bits
 *
 * Message control: bit 0 enable, bits 3:1 how many vectors the device may use (a power of
 * two, written by software: 0 = one), bit 7 64-bit address, bit 8 per-vector masking.
 */
#define PCI_STATUS 0x06
#define PCI_STATUS_CAPABILITIES 0x10
#define PCI_CAPABILITY_POINTER 0x34

#define MSI_CONTROL_ENABLE 0x0001u
#define MSI_CONTROL_MULTIPLE_ENABLE 0x0070u
#define MSI_CONTROL_64BIT 0x0080u
#define MSI_CONTROL_MASKING 0x0100u

unsigned int hal_pci_find_capability(const hal_pci_device_t *device, uint8_t id)
{
    if (!(hal_pci_config_read(device, PCI_STATUS, 2) & PCI_STATUS_CAPABILITIES))
        return 0;

    unsigned int offset = hal_pci_config_read(device, PCI_CAPABILITY_POINTER, 1) & ~3u;

    /* The list is at most 48 entries long (it lives in 192 bytes); a loop in it is a broken device. */
    for (int guard = 0; offset >= 0x40 && guard < 48; guard++)
    {
        if (hal_pci_config_read(device, offset, 1) == id)
            return offset;

        offset = hal_pci_config_read(device, offset + 1, 1) & ~3u;
    }

    return 0;
}

/*
 * MSI-X (PCI Local Bus Specification 3.0, 6.8.2): the messages are in a table in one of the
 * device's memory BARs, 16 bytes per vector {address low, address high, data, vector control
 * (bit 0 = masked)}. Capability: +2 message control (bit 15 enable, bit 14 mask all, bits
 * 10:0 table size - 1), +4 table location (bits 2:0 which BAR, the rest the offset in it).
 * Only vector 0 is used here.
 */
#define MSIX_CONTROL_ENABLE 0x8000u
#define MSIX_CONTROL_FUNCTION_MASK 0x4000u

static int enable_msix(const hal_pci_device_t *device, unsigned int cap, hal_irq_handler_t handler)
{
    uint32_t location = hal_pci_config_read(device, cap + 4, 4);
    unsigned int bar = location & 7u;

    if (bar >= 6 || device->bar[bar].type != HAL_PCI_BAR_MEM)
        return -1;

    volatile uint32_t *base = hal_pci_map_bar(device, bar);

    if (base == 0)
        return -1;

    uint32_t address;
    uint32_t data;
    int line = hal_irq_msi_alloc(handler, &address, &data);

    if (line < 0)
        return -1;

    volatile uint32_t *entry = base + (location & ~7u) / 4;

    /* Mask the vector while the message is written, then unmask it. */
    entry[3] = 1;
    entry[0] = address;
    entry[1] = 0;
    entry[2] = data;
    entry[3] = 0;

    uint32_t control = hal_pci_config_read(device, cap + 2, 2);

    control |= MSIX_CONTROL_ENABLE;
    control &= ~MSIX_CONTROL_FUNCTION_MASK;
    hal_pci_config_write(device, cap + 2, 2, control);

    return line;
}

int hal_pci_enable_msi(const hal_pci_device_t *device, hal_irq_handler_t handler)
{
    unsigned int cap = hal_pci_find_capability(device, HAL_PCI_CAP_MSI);

    if (cap == 0)
    {
        unsigned int msix = hal_pci_find_capability(device, HAL_PCI_CAP_MSIX);

        return msix != 0 ? enable_msix(device, msix, handler) : -1;
    }

    uint32_t address;
    uint32_t data;
    int line = hal_irq_msi_alloc(handler, &address, &data);

    if (line < 0)
        return -1;

    uint32_t control = hal_pci_config_read(device, cap + 2, 2);
    unsigned int data_offset = cap + 8;

    hal_pci_config_write(device, cap + 4, 4, address);

    if (control & MSI_CONTROL_64BIT)
    {
        hal_pci_config_write(device, cap + 8, 4, 0);
        data_offset = cap + 12;
    }

    hal_pci_config_write(device, data_offset, 2, data);

    /* All vectors unmasked, and exactly one vector (multiple message enable = 0). */
    if (control & MSI_CONTROL_MASKING)
        hal_pci_config_write(device, data_offset + 4, 4, 0);

    control &= ~MSI_CONTROL_MULTIPLE_ENABLE;
    control |= MSI_CONTROL_ENABLE;
    hal_pci_config_write(device, cap + 2, 2, control);

    return line;
}
