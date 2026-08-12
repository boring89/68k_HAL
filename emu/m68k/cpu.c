/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cpu.h"
#include "memory.h"
#include "decoder.h"
#include "execute.h"

void m68k_cpu_init(m68k_cpu_t *cpu)
{
    int i;

    for (i = 0; i < 8; i++)
    {
        cpu->d[i] = 0;
        cpu->a[i] = 0;
    }

    cpu->pc = 0;
    cpu->sr = 0;
}

uint16_t m68k_cpu_fetch(m68k_cpu_t *cpu)
{
    uint16_t opcode =
        m68k_memory_read16(cpu->pc);

    cpu->pc += 2;

    return opcode;
}

int m68k_cpu_step(m68k_cpu_t *cpu)
{
    uint16_t opcode =
        m68k_cpu_fetch(cpu);

    m68k_instruction_t instruction;

    m68k_decode(opcode, &instruction);

    return m68k_execute(cpu, &instruction);
}