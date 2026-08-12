/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef M68K_EXECUTE_H
#define M68K_EXECUTE_H

#include "cpu.h"
#include "decoder.h"

int m68k_execute(
    m68k_cpu_t *cpu,
    const m68k_instruction_t *instruction);

int m68k_execute_move(
    m68k_cpu_t *cpu,
    const m68k_instruction_t *instruction);

int m68k_execute_add(
    m68k_cpu_t *cpu,
    const m68k_instruction_t *instruction);

#endif