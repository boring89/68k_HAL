/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "pci_names.h"

static const struct
{
    uint8_t class_code;
    uint8_t subclass;
    const char *name;
} classes[] = {
    {0x00, 0x00, "Non-VGA unclassified device"},
    {0x01, 0x00, "SCSI storage controller"},
    {0x01, 0x01, "IDE interface"},
    {0x01, 0x02, "Floppy disk controller"},
    {0x01, 0x05, "ATA controller"},
    {0x01, 0x06, "SATA controller"},
    {0x01, 0x08, "Non-volatile memory controller"},
    {0x01, 0x80, "Mass storage controller"},
    {0x02, 0x00, "Ethernet controller"},
    {0x02, 0x80, "Network controller"},
    {0x03, 0x00, "VGA compatible controller"},
    {0x03, 0x80, "Display controller"},
    {0x04, 0x00, "Multimedia video controller"},
    {0x04, 0x01, "Multimedia audio controller"},
    {0x04, 0x03, "Audio device"},
    {0x05, 0x00, "RAM memory"},
    {0x06, 0x00, "Host bridge"},
    {0x06, 0x01, "ISA bridge"},
    {0x06, 0x04, "PCI bridge"},
    {0x06, 0x80, "Bridge"},
    {0x07, 0x00, "Serial controller"},
    {0x08, 0x00, "PIC"},
    {0x0C, 0x03, "USB controller"},
    {0x0C, 0x05, "SMBus"},
};

static const struct
{
    uint8_t class_code;
    const char *name;
} class_only[] = {
    {0x00, "Unclassified device"},
    {0x01, "Mass storage controller"},
    {0x02, "Network controller"},
    {0x03, "Display controller"},
    {0x04, "Multimedia controller"},
    {0x05, "Memory controller"},
    {0x06, "Bridge"},
    {0x07, "Communication controller"},
    {0x08, "System peripheral"},
    {0x09, "Input device controller"},
    {0x0C, "Serial bus controller"},
};

const char *pci_class_name(uint8_t class_code, uint8_t subclass)
{
    for (unsigned int i = 0; i < sizeof(classes) / sizeof(classes[0]); i++)
    {
        if (classes[i].class_code == class_code && classes[i].subclass == subclass)
            return classes[i].name;
    }

    for (unsigned int i = 0; i < sizeof(class_only) / sizeof(class_only[0]); i++)
    {
        if (class_only[i].class_code == class_code)
            return class_only[i].name;
    }

    return "Unknown";
}

const char *pci_vendor_name(uint16_t vendor_id)
{
    switch (vendor_id)
    {
    case 0x8086:
        return "Intel";

    case 0x1234:
        return "QEMU/Bochs";

    case 0x1022:
        return "AMD";

    case 0x10DE:
        return "NVIDIA";

    case 0x10EC:
        return "Realtek";

    case 0x1AF4:
        return "Red Hat (virtio)";

    case 0x15AD:
        return "VMware";

    case 0x80EE:
        return "VirtualBox";

    case 0x1B36:
        return "Red Hat (QEMU)";

    default:
        return "Unknown";
    }
}
