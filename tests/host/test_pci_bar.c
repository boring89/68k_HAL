/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "platform/pc/pci/pci_bar.h"

static void test_io_bar(void)
{
    hal_pci_bar_t bar;

    /* IDE bus-master BAR on a PIIX: ports 0xC040, 16 bytes. */
    pci_bar_decode(0xC041, 0, 0xFFFFFFF1, 0, &bar);
    TEST_ASSERT_EQ_INT(HAL_PCI_BAR_IO, bar.type);
    TEST_ASSERT_EQ_INT(0xC040, bar.base);
    TEST_ASSERT_EQ_INT(16, bar.size);

    /* A device that only decodes 16 bits: the high half reads zero. */
    pci_bar_decode(0xC001, 0, 0x0000FF01, 0, &bar);
    TEST_ASSERT_EQ_INT(0x100, bar.size);
}

static void test_memory_bar_32(void)
{
    hal_pci_bar_t bar;

    /* e1000 register window: 128 KiB, non-prefetchable. */
    pci_bar_decode(0xFEB80000, 0, 0xFFFE0000, 0, &bar);
    TEST_ASSERT_EQ_INT(HAL_PCI_BAR_MEM, bar.type);
    TEST_ASSERT_EQ_INT(0xFEB80000, bar.base);
    TEST_ASSERT_EQ_INT(0x20000, bar.size);
    TEST_ASSERT_EQ_INT(0, bar.prefetchable);
    TEST_ASSERT_EQ_INT(1, pci_bar_registers(0xFEB80000));

    /* VGA frame buffer: 16 MiB, prefetchable. */
    pci_bar_decode(0xFD000008, 0, 0xFF000008, 0, &bar);
    TEST_ASSERT_EQ_INT(0x1000000, bar.size);
    TEST_ASSERT_EQ_INT(1, bar.prefetchable);
}

static void test_memory_bar_64(void)
{
    hal_pci_bar_t bar;

    /* 64-bit prefetchable at 0x1_8000_0000, 256 MiB. */
    TEST_ASSERT_EQ_INT(2, pci_bar_registers(0x8000000C));
    pci_bar_decode(0x8000000C, 0x1, 0xF000000C, 0xFFFFFFFF, &bar);
    TEST_ASSERT_EQ_INT(HAL_PCI_BAR_MEM, bar.type);
    TEST_ASSERT(bar.base == 0x180000000ull);
    TEST_ASSERT(bar.size == 0x10000000ull);

    /* A 64-bit BAR of 8 GiB needs the high register to size. */
    pci_bar_decode(0x0000000C, 0, 0x0000000C, 0xFFFFFFFE, &bar);
    TEST_ASSERT(bar.size == 0x200000000ull);
}

static void test_unused_bar(void)
{
    hal_pci_bar_t bar;

    pci_bar_decode(0, 0, 0, 0, &bar);
    TEST_ASSERT_EQ_INT(HAL_PCI_BAR_NONE, bar.type);
    TEST_ASSERT_EQ_INT(0, bar.size);
}

int main(void)
{
    TEST_RUN(test_io_bar);
    TEST_RUN(test_memory_bar_32);
    TEST_RUN(test_memory_bar_64);
    TEST_RUN(test_unused_bar);

    return TEST_REPORT("pci_bar");
}
