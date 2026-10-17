/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cpuid.h"

/*
 * Bit positions: Intel SDM Vol.2A "CPUID", Table 3-10 (leaf 1 ECX),
 * Table 3-11 (leaf 1 EDX), Table 3-8 (leaf 0x80000001) and
 * "INPUT EAX = 80000007H" (invariant TSC, EDX bit 8).
 */

static void put_word(char *dst, uint32_t word)
{
    for (int i = 0; i < 4; i++)
        dst[i] = (char)(word >> (8 * i));
}

static void set_if(hal_cpu_info_t *info, uint32_t reg, unsigned int bit,
                   uint32_t feature)
{
    if (reg & (1u << bit))
        info->features |= feature;
}

void cpuid_decode(const cpuid_raw_t *raw, hal_cpu_info_t *info)
{
    for (unsigned int i = 0; i < sizeof(*info); i++)
        ((char *)info)[i] = 0;

    put_word(info->vendor, raw->vendor[0]);
    put_word(info->vendor + 4, raw->vendor[1]);
    put_word(info->vendor + 8, raw->vendor[2]);

    if (raw->max_leaf >= 1)
    {
        uint32_t eax = raw->l1[0];
        uint32_t ecx = raw->l1[2];
        uint32_t edx = raw->l1[3];
        uint32_t family = (eax >> 8) & 0xF;
        uint32_t model = (eax >> 4) & 0xF;

        /* Intel SDM Vol.2A "CPUID", Figure 3-6 and the DisplayFamily rule. */
        if (family == 0xF)
            family += (eax >> 20) & 0xFF;

        if (((eax >> 8) & 0xF) == 0x6 || ((eax >> 8) & 0xF) == 0xF)
            model |= ((eax >> 16) & 0xF) << 4;

        info->family = family;
        info->model = model;
        info->stepping = eax & 0xF;

        set_if(info, edx, 0, HAL_CPU_FEATURE_FPU);
        set_if(info, edx, 4, HAL_CPU_FEATURE_TSC);
        set_if(info, edx, 5, HAL_CPU_FEATURE_MSR);
        set_if(info, edx, 6, HAL_CPU_FEATURE_PAE);
        set_if(info, edx, 9, HAL_CPU_FEATURE_APIC);
        set_if(info, edx, 13, HAL_CPU_FEATURE_PGE);
        set_if(info, edx, 15, HAL_CPU_FEATURE_CMOV);
        set_if(info, edx, 16, HAL_CPU_FEATURE_PAT);
        set_if(info, edx, 24, HAL_CPU_FEATURE_FXSR);
        set_if(info, edx, 25, HAL_CPU_FEATURE_SSE);
        set_if(info, edx, 26, HAL_CPU_FEATURE_SSE2);

        set_if(info, ecx, 0, HAL_CPU_FEATURE_SSE3);
        set_if(info, ecx, 9, HAL_CPU_FEATURE_SSSE3);
        set_if(info, ecx, 19, HAL_CPU_FEATURE_SSE41);
        set_if(info, ecx, 20, HAL_CPU_FEATURE_SSE42);
        set_if(info, ecx, 21, HAL_CPU_FEATURE_X2APIC);
        set_if(info, ecx, 23, HAL_CPU_FEATURE_POPCNT);
        set_if(info, ecx, 26, HAL_CPU_FEATURE_XSAVE);
        set_if(info, ecx, 28, HAL_CPU_FEATURE_AVX);
        set_if(info, ecx, 31, HAL_CPU_FEATURE_HYPERVISOR);
    }

    if (raw->max_ext >= 0x80000001u)
        set_if(info, raw->e1[3], 20, HAL_CPU_FEATURE_NX);

    if (raw->max_ext >= 0x80000007u)
        set_if(info, raw->e7[3], 8, HAL_CPU_FEATURE_INVARIANT_TSC);

    if (raw->max_ext >= 0x80000004u)
    {
        for (int i = 0; i < 12; i++)
            put_word(info->brand + 4 * i, raw->brand[i]);

        /* The brand string is padded with spaces on the left. */
        const char *src = info->brand;

        while (*src == ' ')
            src++;

        int n = 0;

        while (src[n] != '\0')
        {
            info->brand[n] = src[n];
            n++;
        }

        info->brand[n] = '\0';
        info->brand[48] = '\0';
    }
}
