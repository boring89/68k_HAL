/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_MEMORY_MEMORY_H
#define KERNEL_MEMORY_MEMORY_H

#include <stdint.h>

#include "heap.h"

void memory_init(void);

uint32_t memory_get_kernel_end(void);

#endif