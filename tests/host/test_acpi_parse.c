/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "platform/pc/acpi/acpi_parse.h"

#include <string.h>

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* A MADT like QEMU's: 4 CPUs (one disabled), one I/O APIC, 8259 present. */
static size_t build_madt(uint8_t *t)
{
    size_t n = 44;

    memset(t, 0, 256);
    memcpy(t, "APIC", 4);
    put32(t + 36, 0xFEE00000u);
    put32(t + 40, 1);

    for (int i = 0; i < 4; i++)
    {
        t[n] = 0;
        t[n + 1] = 8;
        t[n + 2] = (uint8_t)i;
        t[n + 3] = (uint8_t)i;
        put32(t + n + 4, i == 3 ? 0 : 1);
        n += 8;
    }

    t[n] = 1;
    t[n + 1] = 12;
    t[n + 2] = 4;
    put32(t + n + 4, 0xFEC00000u);
    n += 12;

    /* An unknown entry type must be skipped. */
    t[n] = 2;
    t[n + 1] = 10;
    n += 10;

    put32(t + 4, (uint32_t)n);

    return n;
}

static void test_checksum_and_rsdp(void)
{
    acpi_rsdp_t rsdp;

    memset(&rsdp, 0, sizeof(rsdp));
    memcpy(rsdp.signature, "RSD PTR ", 8);
    rsdp.checksum = (uint8_t)(0 - acpi_checksum(&rsdp, sizeof(rsdp)));
    TEST_ASSERT(acpi_rsdp_valid(&rsdp));

    rsdp.rsdt_address = 1;
    TEST_ASSERT(!acpi_rsdp_valid(&rsdp));

    rsdp.checksum = (uint8_t)(rsdp.checksum - 1);
    TEST_ASSERT(acpi_rsdp_valid(&rsdp));

    rsdp.signature[0] = 'X';
    TEST_ASSERT(!acpi_rsdp_valid(&rsdp));
}

static void test_madt(void)
{
    uint8_t table[256];
    size_t length = build_madt(table);
    hal_acpi_info_t info;

    memset(&info, 0, sizeof(info));
    TEST_ASSERT_EQ_INT(0, acpi_parse_madt(table, length, &info));
    TEST_ASSERT_EQ_INT(3, info.cpu_count);
    TEST_ASSERT_EQ_INT(2, info.apic_ids[2]);
    TEST_ASSERT_EQ_INT(1, info.ioapic_count);
    TEST_ASSERT_EQ_INT(4, info.ioapic_id[0]);
    TEST_ASSERT_EQ_INT(0xFEC00000u, info.ioapic_base[0]);
    TEST_ASSERT_EQ_INT(0xFEE00000u, info.lapic_base);
    TEST_ASSERT(info.dual_8259);
}

static void test_madt_lapic_override(void)
{
    uint8_t table[256];
    size_t length = build_madt(table);
    hal_acpi_info_t info;

    table[length] = 5;
    table[length + 1] = 12;
    put32(table + length + 4, 0xFEE01000u);
    length += 12;
    put32(table + 4, (uint32_t)length);

    memset(&info, 0, sizeof(info));
    TEST_ASSERT_EQ_INT(0, acpi_parse_madt(table, length, &info));
    TEST_ASSERT_EQ_INT(0xFEE01000u, info.lapic_base);

    /* An address above 4 GiB cannot be used by this 32-bit kernel. */
    put32(table + length - 4, 1);
    memset(&info, 0, sizeof(info));
    TEST_ASSERT_EQ_INT(0, acpi_parse_madt(table, length, &info));
    TEST_ASSERT_EQ_INT(0xFEE00000u, info.lapic_base);
}

static void test_madt_rejects(void)
{
    uint8_t table[256];
    size_t length = build_madt(table);
    hal_acpi_info_t info;

    memset(&info, 0, sizeof(info));
    TEST_ASSERT_EQ_INT(-1, acpi_parse_madt(table, length - 1, &info));
    TEST_ASSERT_EQ_INT(-1, acpi_parse_madt(table, 10, &info));

    /* An entry that claims to be longer than the table. */
    table[44 + 1] = 200;
    TEST_ASSERT_EQ_INT(-1, acpi_parse_madt(table, length, &info));

    /* A zero-length entry would loop forever. */
    table[44 + 1] = 0;
    TEST_ASSERT_EQ_INT(-1, acpi_parse_madt(table, length, &info));

    build_madt(table);
    table[0] = 'X';
    TEST_ASSERT_EQ_INT(-1, acpi_parse_madt(table, length, &info));
}

int main(void)
{
    TEST_RUN(test_checksum_and_rsdp);
    TEST_RUN(test_madt);
    TEST_RUN(test_madt_lapic_override);
    TEST_RUN(test_madt_rejects);

    return TEST_REPORT("acpi_parse");
}
