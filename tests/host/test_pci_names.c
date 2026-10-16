/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "kernel/pci/pci_names.h"

static void test_classes(void)
{
    TEST_ASSERT_EQ_STR("Host bridge", pci_class_name(0x06, 0x00));
    TEST_ASSERT_EQ_STR("Ethernet controller", pci_class_name(0x02, 0x00));
    /* Unknown subclass falls back to the class. */
    TEST_ASSERT_EQ_STR("Network controller", pci_class_name(0x02, 0x55));
    TEST_ASSERT_EQ_STR("Unknown", pci_class_name(0xEE, 0));
}

static void test_vendors(void)
{
    TEST_ASSERT_EQ_STR("Intel", pci_vendor_name(0x8086));
    TEST_ASSERT_EQ_STR("QEMU/Bochs", pci_vendor_name(0x1234));
    TEST_ASSERT_EQ_STR("Unknown", pci_vendor_name(0x0001));
}

int main(void)
{
    TEST_RUN(test_classes);
    TEST_RUN(test_vendors);

    return TEST_REPORT("pci_names");
}
