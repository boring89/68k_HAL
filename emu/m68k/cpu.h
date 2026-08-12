/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef EMU_M68K_CPU_H
#define EMU_M68K_CPU_H

#include "stdint.h"

typedef struct
{
    uint32_t d[8];
    uint32_t a[8];

    uint32_t pc;
    uint16_t sr;

} m68k_cpu_t;

void m68k_cpu_init(m68k_cpu_t *cpu);

uint16_t m68k_cpu_fetch(m68k_cpu_t *cpu);

int m68k_cpu_step(m68k_cpu_t *cpu);

#endif