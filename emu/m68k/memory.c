/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "memory.h"

static uint8_t memory[M68K_MEMORY_SIZE];

void m68k_memory_init(void)
{
    for (uint32_t i = 0; i < M68K_MEMORY_SIZE; i++)
    {
        memory[i] = 0;
    }
}

uint8_t m68k_memory_read8(uint32_t address)
{
    if (address >= M68K_MEMORY_SIZE)
        return 0;

    return memory[address];
}

uint16_t m68k_memory_read16(uint32_t address)
{
    return ((uint16_t)m68k_memory_read8(address) << 8) |
           ((uint16_t)m68k_memory_read8(address + 1));
}

uint32_t m68k_memory_read32(uint32_t address)
{
    return ((uint32_t)m68k_memory_read8(address) << 24) |
           ((uint32_t)m68k_memory_read8(address + 1) << 16) |
           ((uint32_t)m68k_memory_read8(address + 2) << 8) |
           ((uint32_t)m68k_memory_read8(address + 3));
}

void m68k_memory_write8(
    uint32_t address,
    uint8_t value)
{
    if (address >= M68K_MEMORY_SIZE)
        return;

    memory[address] = value;
}

void m68k_memory_write16(
    uint32_t address,
    uint16_t value)
{
    m68k_memory_write8(
        address,
        (uint8_t)(value >> 8));

    m68k_memory_write8(
        address + 1,
        (uint8_t)value);
}

void m68k_memory_write32(
    uint32_t address,
    uint32_t value)
{
    m68k_memory_write8(
        address,
        (uint8_t)(value >> 24));

    m68k_memory_write8(
        address + 1,
        (uint8_t)(value >> 16));

    m68k_memory_write8(
        address + 2,
        (uint8_t)(value >> 8));

    m68k_memory_write8(
        address + 3,
        (uint8_t)value);
}