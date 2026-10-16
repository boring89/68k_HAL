/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cpuid.h"

#include "cpu.h"

/* EFLAGS.ID, bit 21: software can toggle it iff CPUID exists (SDM Vol.1 3.4.3). */
#define EFLAGS_ID 0x00200000u

static int cpuid_supported(void)
{
    uint32_t before;
    uint32_t after;

    asm volatile(
        "pushfl\n\t"
        "pushfl\n\t"
        "xorl %[id], (%%esp)\n\t"
        "popfl\n\t"
        "pushfl\n\t"
        "popl %[after]\n\t"
        "popl %[before]\n\t"
        "pushl %[before]\n\t"
        "popfl\n\t"
        : [after] "=r"(after), [before] "=r"(before)
        : [id] "i"(EFLAGS_ID)
        : "memory", "cc");

    return ((before ^ after) & EFLAGS_ID) != 0;
}

static void cpuid(uint32_t leaf, uint32_t out[4])
{
    asm volatile(
        "cpuid"
        : "=a"(out[0]), "=b"(out[1]), "=c"(out[2]), "=d"(out[3])
        : "a"(leaf), "c"(0));
}

int cpuid_read(cpuid_raw_t *raw)
{
    uint32_t r[4];

    for (unsigned int i = 0; i < sizeof(*raw); i++)
        ((char *)raw)[i] = 0;

    if (!cpuid_supported())
        return -1;

    cpuid(0, r);
    raw->max_leaf = r[0];
    raw->vendor[0] = r[1];
    raw->vendor[1] = r[3];
    raw->vendor[2] = r[2];

    if (raw->max_leaf >= 1)
        cpuid(1, raw->l1);

    cpuid(0x80000000u, r);
    raw->max_ext = r[0] >= 0x80000000u ? r[0] : 0;

    if (raw->max_ext >= 0x80000001u)
        cpuid(0x80000001u, raw->e1);

    if (raw->max_ext >= 0x80000007u)
        cpuid(0x80000007u, raw->e7);

    if (raw->max_ext >= 0x80000004u)
    {
        for (uint32_t i = 0; i < 3; i++)
            cpuid(0x80000002u + i, raw->brand + 4 * i);
    }

    return 0;
}
