/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PLATFORM_PC_ACPI_ACPI_H
#define PLATFORM_PC_ACPI_ACPI_H

/*
 * Find the RSDP, walk the RSDT and read the MADT. Call once, after
 * paging_init. Returns 0, or -1 if the machine has no usable ACPI
 * tables (hal_acpi_info then returns NULL).
 */
int acpi_init(void);

#endif
