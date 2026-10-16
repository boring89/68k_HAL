/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "pci_bar.h"

#define BAR_IO 0x1u
#define BAR_MEM_TYPE_MASK 0x6u
#define BAR_MEM_64 0x4u
#define BAR_PREFETCH 0x8u

unsigned int pci_bar_registers(uint32_t original)
{
    if (original & BAR_IO)
        return 1;

    return (original & BAR_MEM_TYPE_MASK) == BAR_MEM_64 ? 2 : 1;
}

void pci_bar_decode(uint32_t original, uint32_t original_high,
                    uint32_t sized, uint32_t sized_high, hal_pci_bar_t *bar)
{
    bar->type = HAL_PCI_BAR_NONE;
    bar->prefetchable = 0;
    bar->base = 0;
    bar->size = 0;

    if (original == 0 && sized == 0)
        return;

    if (original & BAR_IO)
    {
        uint32_t mask = sized & ~3u;

        if (mask == 0)
            return;

        bar->type = HAL_PCI_BAR_IO;
        bar->base = original & ~3u;
        /* The upper 16 bits of an I/O BAR may read as zero: ports are 16 bits wide. */
        bar->size = (uint16_t)(~mask + 1);

        if (bar->size == 0)
            bar->size = 0x10000;

        return;
    }

    uint64_t mask = sized & ~0xFu;
    uint64_t base = original & ~0xFu;

    if (pci_bar_registers(original) == 2)
    {
        mask |= (uint64_t)sized_high << 32;
        base |= (uint64_t)original_high << 32;
    }
    else
    {
        mask |= 0xFFFFFFFF00000000ull; /* 32-bit BAR: the high bits are not decoded */
    }

    if ((sized & ~0xFu) == 0 && !(pci_bar_registers(original) == 2 && sized_high != 0))
        return;

    bar->type = HAL_PCI_BAR_MEM;
    bar->prefetchable = (original & BAR_PREFETCH) != 0;
    bar->base = base;
    bar->size = ~mask + 1;
}
