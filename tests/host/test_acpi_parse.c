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

    /* Interrupt source overrides like QEMU's: the timer (IRQ 0) is on input 2, level/low for IRQ 9. */
    t[n] = 2;
    t[n + 1] = 10;
    t[n + 3] = 0;
    put32(t + n + 4, 2);
    n += 10;
    t[n] = 2;
    t[n + 1] = 10;
    t[n + 3] = 9;
    put32(t + n + 4, 9);
    t[n + 8] = 0x0D; /* level, active low */
    n += 10;

    /* An unknown entry type must be skipped. */
    t[n] = 0x42;
    t[n + 1] = 6;
    n += 6;

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
    TEST_ASSERT_EQ_INT(0, info.ioapic_gsi_base[0]);
    TEST_ASSERT_EQ_INT(2, info.irq_override_count);
    TEST_ASSERT_EQ_INT(0, info.irq_override[0].source);
    TEST_ASSERT_EQ_INT(2, info.irq_override[0].gsi);
    TEST_ASSERT_EQ_INT(9, info.irq_override[1].source);
    TEST_ASSERT_EQ_INT(9, info.irq_override[1].gsi);
    TEST_ASSERT_EQ_INT(0x0D, info.irq_override[1].flags);
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

static void test_fadt(void)
{
    uint8_t table[256];
    hal_acpi_info_t info;

    memset(table, 0, sizeof(table));
    memcpy(table, "FACP", 4);
    put32(table + 4, 244);
    put32(table + 40, 0x7FE1000u);   /* DSDT */
    put32(table + 48, 0xB2);         /* SMI_CMD */
    table[52] = 0xF1;                /* ACPI_ENABLE */
    table[46] = 9;                   /* SCI_INT */
    put32(table + 56, 0x600);        /* PM1a_EVT_BLK */
    put32(table + 64, 0x604);        /* PM1a_CNT_BLK */
    table[88] = 4;                   /* PM1_EVT_LEN */
    put32(table + 112, 1u << 10);    /* RESET_REG_SUP */
    table[116] = 1;                  /* I/O port */
    put32(table + 120, 0xCF9);
    table[128] = 0x06;

    memset(&info, 0, sizeof(info));
    TEST_ASSERT_EQ_INT(0, acpi_parse_fadt(table, 244, &info));
    TEST_ASSERT_EQ_INT(0x7FE1000u, info.dsdt_address);
    TEST_ASSERT_EQ_INT(0xB2, info.smi_command_port);
    TEST_ASSERT_EQ_INT(0xF1, info.acpi_enable_value);
    TEST_ASSERT_EQ_INT(0x604, info.pm1a_control_port);
    TEST_ASSERT_EQ_INT(0x600, info.pm1a_event_port);
    TEST_ASSERT_EQ_INT(9, info.sci_irq);
    TEST_ASSERT_EQ_INT(0, info.pm1b_event_port);
    TEST_ASSERT_EQ_INT(4, info.pm1_event_length);
    TEST_ASSERT(info.reset_valid);
    TEST_ASSERT_EQ_INT(1, info.reset_space);
    TEST_ASSERT_EQ_INT(0xCF9, info.reset_address);
    TEST_ASSERT_EQ_INT(6, info.reset_value);

    /* Without RESET_REG_SUP, or with a register in PCI config space or above 4 GiB, no reset register. */
    put32(table + 112, 0);
    TEST_ASSERT_EQ_INT(0, acpi_parse_fadt(table, 244, &info));
    TEST_ASSERT(!info.reset_valid);
    put32(table + 112, 1u << 10);
    table[116] = 2;
    acpi_parse_fadt(table, 244, &info);
    TEST_ASSERT(!info.reset_valid);
    table[116] = 1;
    put32(table + 124, 1);
    acpi_parse_fadt(table, 244, &info);
    TEST_ASSERT(!info.reset_valid);

    /* An old, short FADT (ACPI 1.0, 116 bytes) still gives the PM1 ports, but no reset register. */
    memset(&info, 0, sizeof(info));
    TEST_ASSERT_EQ_INT(0, acpi_parse_fadt(table, 116, &info));
    TEST_ASSERT_EQ_INT(0x604, info.pm1a_control_port);
    TEST_ASSERT(!info.reset_valid);

    TEST_ASSERT_EQ_INT(4, info.pm1_event_length);

    /* Shorter than PM1_EVT_LEN: no length. */
    TEST_ASSERT_EQ_INT(0, acpi_parse_fadt(table, 80, &info));
    TEST_ASSERT_EQ_INT(0, info.pm1_event_length);

    TEST_ASSERT_EQ_INT(-1, acpi_parse_fadt(table, 60, &info));
    table[0] = 'X';
    TEST_ASSERT_EQ_INT(-1, acpi_parse_fadt(table, 244, &info));
}

static void test_s5(void)
{
    /* QEMU's DSDT: Name (\_S5, Package (4) { 0x00, 0x00, 0x00, 0x00 }), here with the byte prefix on the second. */
    uint8_t dsdt[80];
    uint8_t a;
    uint8_t b;

    memset(dsdt, 0, sizeof(dsdt));
    memcpy(dsdt, "DSDT", 4);

    size_t at = 40;

    dsdt[at++] = 0x08;       /* NameOp */
    dsdt[at++] = '\\';
    memcpy(dsdt + at, "_S5_", 4);
    at += 4;
    dsdt[at++] = 0x12;       /* PackageOp */
    dsdt[at++] = 0x06;       /* PkgLength */
    dsdt[at++] = 0x04;       /* NumElements */
    dsdt[at++] = 0x0A;       /* BytePrefix */
    dsdt[at++] = 0x05;       /* SLP_TYPa */
    dsdt[at++] = 0x0A;
    dsdt[at++] = 0x07;       /* SLP_TYPb */

    TEST_ASSERT_EQ_INT(0, acpi_find_s5(dsdt, sizeof(dsdt), &a, &b));
    TEST_ASSERT_EQ_INT(5, a);
    TEST_ASSERT_EQ_INT(7, b);

    /* Not there, or not preceded by NameOp. */
    memset(dsdt + 40, 0, 40);
    TEST_ASSERT_EQ_INT(-1, acpi_find_s5(dsdt, sizeof(dsdt), &a, &b));
    memcpy(dsdt + 50, "_S5_", 4);
    TEST_ASSERT_EQ_INT(-1, acpi_find_s5(dsdt, sizeof(dsdt), &a, &b));
}

int main(void)
{
    TEST_RUN(test_checksum_and_rsdp);
    TEST_RUN(test_madt);
    TEST_RUN(test_madt_lapic_override);
    TEST_RUN(test_madt_rejects);
    TEST_RUN(test_fadt);
    TEST_RUN(test_s5);

    return TEST_REPORT("acpi_parse");
}
