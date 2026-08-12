/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef EMU_M68K_MEMORY_H
#define EMU_M68K_MEMORY_H

#include <stdint.h>

#define M68K_MEMORY_SIZE (1024 * 1024)

void m68k_memory_init(void);

uint8_t m68k_memory_read8(uint32_t address);
uint16_t m68k_memory_read16(uint32_t address);
uint32_t m68k_memory_read32(uint32_t address);

void m68k_memory_write8(
    uint32_t address,
    uint8_t value);

void m68k_memory_write16(
    uint32_t address,
    uint16_t value);

void m68k_memory_write32(
    uint32_t address,
    uint32_t value);

#endif