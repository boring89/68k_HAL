/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "execute.h"
#include "memory.h"

int m68k_execute_move(
    m68k_cpu_t *cpu,
    const m68k_instruction_t *instruction)
{
    uint32_t value;

    if (!cpu || !instruction)
        return -1;

    value = m68k_memory_read32(cpu->pc);

    cpu->pc += 4;

    cpu->d[0] = value;

    return 0;
}

int m68k_execute_add(
    m68k_cpu_t *cpu,
    const m68k_instruction_t *instruction)
{
    uint32_t value;

    if (!cpu || !instruction)
        return -1;

    value = m68k_memory_read32(cpu->pc);

    cpu->pc += 4;

    cpu->d[0] += value;

    return 0;
}

int m68k_execute(
    m68k_cpu_t *cpu,
    const m68k_instruction_t *instruction)
{
    if (!cpu || !instruction)
        return -1;

    switch (instruction->type)
    {
        case M68K_INSTRUCTION_MOVE:
            return m68k_execute_move(
                cpu,
                instruction);

        case M68K_INSTRUCTION_ADD:
            return m68k_execute_add(
                cpu,
                instruction);

        default:
            return -1;
    }
}