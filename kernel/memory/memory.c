/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "memory.h"

extern uint32_t _kernel_start;
extern uint32_t _kernel_end;

static uint32_t kernel_start_address;
static uint32_t kernel_end_address;

void memory_init(void)
{
    kernel_start_address = (uint32_t)&_kernel_start;
    kernel_end_address = (uint32_t)&_kernel_end;
}

uint32_t memory_get_kernel_start(void)
{
    return kernel_start_address;
}

uint32_t memory_get_kernel_end(void)
{
    return kernel_end_address;
}