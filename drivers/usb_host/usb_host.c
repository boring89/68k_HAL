/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "usb_host.h"

#include "kernel/usb/usb.h"

/* Programming interface of a USB controller (PCI Code and ID Assignment Specification, class 0x0C 0x03). */
#define PROG_IF_UHCI 0x00
#define PROG_IF_OHCI 0x10
#define PROG_IF_EHCI 0x20
#define PROG_IF_XHCI 0x30

/*
 * Configuration register 0x60, SBRN ("serial bus release number"), holds the
 * USB version the controller implements in BCD (0x10 = 1.0, 0x20 = 2.0,
 * 0x30 = 3.0); UHCI, OHCI, EHCI and xHCI all have it.
 */
#define PCI_SBRN 0x60

static int usb_host_probe(const hal_pci_device_t *pci)
{
    usb_host_info_t host = {0};

    host.bus = pci->bus;
    host.device = pci->device;
    host.function = pci->function;

    switch (pci->prog_if)
    {
    case PROG_IF_UHCI:
        host.type = USB_HOST_UHCI;
        host.ports = 2; /* UHCI has two root ports (UHCI design guide 1.1, 2.1.2) */
        break;

    case PROG_IF_OHCI:
        host.type = USB_HOST_OHCI;

        /* HcRhDescriptorA (offset 0x48), bits 7:0 = number of downstream ports. */
        if (pci->bar[0].type == HAL_PCI_BAR_MEM)
        {
            volatile uint32_t *registers = hal_pci_map_bar(pci, 0);

            if (registers != 0)
                host.ports = (uint8_t)(registers[0x48 / 4] & 0xFF);
        }
        break;

    case PROG_IF_EHCI:
    case PROG_IF_XHCI:
    {
        host.type = pci->prog_if == PROG_IF_EHCI ? USB_HOST_EHCI : USB_HOST_XHCI;

        if (pci->bar[0].type != HAL_PCI_BAR_MEM)
            return -1;

        volatile uint8_t *registers = hal_pci_map_bar(pci, 0);

        if (registers == 0)
            return -1;

        /*
         * Capability registers: the first 32-bit word holds CAPLENGTH (bits 7:0) and
         * HCIVERSION (bits 31:16), then HCSPARAMS1 at 4. Whole words only: QEMU's
         * controllers answer partial reads of the capability area with zeros.
         */
        host.version = (uint16_t)(*(volatile uint32_t *)registers >> 16);

        uint32_t params = *(volatile uint32_t *)(registers + 4);

        if (host.type == USB_HOST_XHCI)
        {
            host.slots = (uint8_t)(params & 0xFF);     /* MaxSlots */
            host.ports = (uint8_t)(params >> 24);      /* MaxPorts */
        }
        else
        {
            host.ports = (uint8_t)(params & 0xF);      /* N_PORTS */
        }

        break;
    }

    default:
        return -1;
    }

    if (host.version == 0)
        host.version = (uint16_t)(hal_pci_config_read(pci, PCI_SBRN, 1) << 4); /* 0x20 -> 0x0200 */

    return usb_host_register(&host);
}

const hal_pci_driver_t usb_host_pci_driver = {
    .name = "usb-host",
    .vendor_id = 0,
    .class_code = 0x0C,
    .subclass = 0x03,
    .probe = usb_host_probe,
};
