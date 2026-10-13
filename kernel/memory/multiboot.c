/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "multiboot.h"

static const multiboot_info_t *multiboot_info;

void multiboot_init(uint32_t address)
{
    multiboot_info = (const multiboot_info_t *)address;
}

const multiboot_info_t *multiboot_get_info(void)
{
    return multiboot_info;
}