/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "procfiles.h"

#include "hal/cpu.h"
#include "hal/timer.h"

#include "kernel/lib/k_snprintf.h"
#include "kernel/memory/heap.h"
#include "kernel/memory/pmm.h"
#include "kernel/vfs/procfs.h"

#include "sched.h"

static size_t version_text(char *buffer, size_t size)
{
    return k_snprintf(buffer, size, "68k_HAL 0.1 (i386)\n");
}

/* "seconds.hundredths idle" as on Linux (there is no idle time to report). */
static size_t uptime_text(char *buffer, size_t size)
{
    unsigned long long ms = hal_timer_ns() / 1000000u;

    return k_snprintf(buffer, size, "%llu.%02u 0.00\n", ms / 1000, (unsigned int)(ms % 1000 / 10));
}

static size_t meminfo_text(char *buffer, size_t size)
{
    unsigned long long frame_kb = PMM_FRAME_SIZE / 1024;

    return k_snprintf(buffer, size,
                      "MemTotal:  %8llu kB\nMemFree:   %8llu kB\nHeapUsed:  %8llu kB\nHeapTotal: %8llu kB\n",
                      pmm_get_total_frames() * frame_kb, pmm_get_free_frames() * frame_kb,
                      (unsigned long long)heap_get_used() / 1024, (unsigned long long)heap_get_reserved() / 1024);
}

static size_t cpuinfo_text(char *buffer, size_t size)
{
    static const struct
    {
        unsigned int mask;
        const char *name;
    } flags[] = {
        {HAL_CPU_FEATURE_FPU, "fpu"}, {HAL_CPU_FEATURE_TSC, "tsc"}, {HAL_CPU_FEATURE_MSR, "msr"},
        {HAL_CPU_FEATURE_PAE, "pae"}, {HAL_CPU_FEATURE_APIC, "apic"}, {HAL_CPU_FEATURE_CMOV, "cmov"},
        {HAL_CPU_FEATURE_PGE, "pge"}, {HAL_CPU_FEATURE_FXSR, "fxsr"}, {HAL_CPU_FEATURE_SSE, "sse"},
        {HAL_CPU_FEATURE_SSE2, "sse2"}, {HAL_CPU_FEATURE_SSE3, "sse3"}, {HAL_CPU_FEATURE_SSSE3, "ssse3"},
        {HAL_CPU_FEATURE_SSE41, "sse4_1"}, {HAL_CPU_FEATURE_SSE42, "sse4_2"}, {HAL_CPU_FEATURE_POPCNT, "popcnt"},
        {HAL_CPU_FEATURE_AVX, "avx"}, {HAL_CPU_FEATURE_NX, "nx"}, {HAL_CPU_FEATURE_HYPERVISOR, "hypervisor"}};
    const hal_cpu_info_t *cpu = hal_cpu_info();
    size_t length = 0;

    /* One block per running processor (they are all the same model). */
    for (unsigned int n = 0; n < hal_cpu_online_count() && length < size; n++)
    {
        length += k_snprintf(buffer + length, size - length,
                             "processor\t: %u\nvendor_id\t: %s\ncpu family\t: %u\nmodel\t\t: %u\n"
                             "model name\t: %s\nstepping\t: %u\nflags\t\t:",
                             n, cpu->vendor, cpu->family, cpu->model, cpu->brand, cpu->stepping);

        for (unsigned int i = 0; i < sizeof(flags) / sizeof(flags[0]) && length < size; i++)
        {
            if (cpu->features & flags[i].mask)
                length += k_snprintf(buffer + length, size - length, " %s", flags[i].name);
        }

        if (length < size)
            length += k_snprintf(buffer + length, size - length, "\n\n");
    }

    return length < size ? length : size;
}

/* One line per process: pid, parent, group, state (R runnable, S waiting, Z zombie), name. */
static size_t processes_text(char *buffer, size_t size)
{
    size_t length = k_snprintf(buffer, size, "PID PPID PGID S NAME\n");

    for (unsigned int i = 1; i <= USER_MAX_PROCS && length < size; i++)
    {
        const user_process_t *p = sched_slot(i);

        if (p == NULL)
            continue;

        char state = p->state == PROC_ZOMBIE ? 'Z' : p->state == PROC_WAITING ? 'S' : 'R';

        length += k_snprintf(buffer + length, size - length, "%u %u %u %c %s\n", (unsigned int)p->pid,
                             (unsigned int)p->ppid, (unsigned int)p->pgid, state, p->name);
    }

    return length < size ? length : size;
}

vfs_fs_t *procfiles_create(void)
{
    vfs_fs_t *fs = procfs_create();

    if (fs == NULL)
        return NULL;

    procfs_add(fs, "version", version_text);
    procfs_add(fs, "uptime", uptime_text);
    procfs_add(fs, "meminfo", meminfo_text);
    procfs_add(fs, "cpuinfo", cpuinfo_text);
    procfs_add(fs, "processes", processes_text);

    return fs;
}
