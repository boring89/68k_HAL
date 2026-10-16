/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ARCH_X86_CPU_CPUID_H
#define ARCH_X86_CPU_CPUID_H

#include "hal/cpu.h"

#include <stdint.h>

/*
 * The CPUID leaves cpuid_decode needs, as the CPU returned them
 * (Intel SDM Vol.2A "CPUID"). A leaf the CPU does not have stays zero.
 */
typedef struct
{
    uint32_t max_leaf;   /* leaf 0: highest basic leaf */
    uint32_t vendor[3];  /* leaf 0: EBX, EDX, ECX = the vendor string */
    uint32_t l1[4];      /* leaf 1: EAX, EBX, ECX, EDX */
    uint32_t max_ext;    /* leaf 0x80000000: highest extended leaf */
    uint32_t e1[4];      /* leaf 0x80000001 */
    uint32_t e7[4];      /* leaf 0x80000007 */
    uint32_t brand[12];  /* leaves 0x80000002..4: EAX, EBX, ECX, EDX each */
} cpuid_raw_t;

/* Turn the raw leaves into a hal_cpu_info_t. Pure, no CPU access. */
void cpuid_decode(const cpuid_raw_t *raw, hal_cpu_info_t *info);

/* Read the leaves from the CPU. Returns -1 if it has no CPUID. */
int cpuid_read(cpuid_raw_t *raw);

#endif
