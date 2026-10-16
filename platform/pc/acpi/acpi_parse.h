/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PLATFORM_PC_ACPI_ACPI_PARSE_H
#define PLATFORM_PC_ACPI_ACPI_PARSE_H

#include "hal/acpi.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Pure parsing of ACPI structures (ACPI Specification 6.x, section 5.2),
 * given as bytes in memory. No hardware access, so it runs in the host
 * tests too.
 */

/* RSDP, ACPI 1.0 part (20 bytes) - section 5.2.5.3. */
typedef struct __attribute__((packed))
{
    char signature[8]; /* "RSD PTR " */
    uint8_t checksum;  /* bytes 0..19 sum to 0 */
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} acpi_rsdp_t;

/* Header shared by all system description tables - section 5.2.6. */
typedef struct __attribute__((packed))
{
    char signature[4];
    uint32_t length;   /* whole table, header included */
    uint8_t revision;
    uint8_t checksum;  /* the whole table sums to 0 */
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} acpi_table_header_t;

/* Sum of length bytes modulo 256; 0 for a valid table. */
uint8_t acpi_checksum(const void *data, size_t length);

/* 1 if p points at a valid RSDP (signature and checksum). */
int acpi_rsdp_valid(const acpi_rsdp_t *rsdp);

/*
 * Read the MADT ("APIC" table, section 5.2.12) of length bytes into
 * info: enabled processors, the local APIC address (with a 64-bit
 * override entry applied if its address fits in 32 bits), I/O APICs
 * and the PCAT_COMPAT flag. Returns -1 if the table is malformed.
 */
int acpi_parse_madt(const uint8_t *madt, size_t length, hal_acpi_info_t *info);

#endif
