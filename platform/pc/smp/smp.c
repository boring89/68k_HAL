/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "smp.h"

#include "hal/acpi.h"
#include "hal/cpu.h"
#include "hal/mmu.h"

#include "platform/pc/interrupt/lapic.h"
#include "platform/pc/timer/tsc.h"

#include "arch/x86/cpu/cpu.h"

#include "kernel/lib/k_memory.h"
#include "kernel/log/log.h"

#include <stddef.h>

/*
 * Local APIC registers (Intel SDM Vol.3A 10.4, Table 10-1; offsets from
 * the APIC base) and ICR bits (10.6.1, Figure 10-12).
 */
#define ICR_DELIVERY_PENDING (1u << 12)
#define ICR_LEVEL_ASSERT (1u << 14)
#define ICR_INIT (5u << 8)
#define ICR_STARTUP (6u << 8)

/* Must match TRAMP_BASE in trampoline.asm: SIPI vector = base / 4096. */
#define TRAMP_BASE 0x7000u
#define AP_STACK_SIZE 4096
#define AP_MAX (HAL_ACPI_MAX_CPUS - 1)

extern const char ap_tramp_start[];
extern const char ap_tramp_end[];
extern const char ap_tramp_stack[];
extern const char ap_tramp_entry[];

static uint8_t ap_stacks[AP_MAX][AP_STACK_SIZE] __attribute__((aligned(16)));

/* Written by the APs (one lock-prefixed increment each), read by the boot CPU. */
static volatile uint32_t ap_online;
static unsigned int cpus_online = 1;

static void wait_ns(uint64_t ns)
{
    uint64_t end = tsc_now_ns() + ns;

    while (tsc_now_ns() < end)
        asm volatile("pause");
}

static int icr_wait_idle(void)
{
    uint64_t end = tsc_now_ns() + 10000000u;

    while (lapic_read(LAPIC_ICR_LOW) & ICR_DELIVERY_PENDING)
    {
        if (tsc_now_ns() > end)
            return -1;

        asm volatile("pause");
    }

    return 0;
}

static void icr_send(uint32_t apic_id, uint32_t command)
{
    lapic_write(LAPIC_ICR_HIGH, apic_id << 24);
    lapic_write(LAPIC_ICR_LOW, command);
    icr_wait_idle();
}

/* Runs on the AP, on its own stack, in protected mode without paging. */
void smp_ap_entry(void);

void smp_ap_entry(void)
{
    asm volatile("lock incl %0" : "+m"(ap_online) : : "memory");

    for (;;)
        asm volatile("cli; hlt");
}

/* Start one AP; returns 0 once it has reported, -1 on timeout. */
static int start_ap(uint32_t apic_id, unsigned int index)
{
    uint32_t before = ap_online;

    /* The data words sit at the same offsets in the copy. */
    uint8_t *copy = (uint8_t *)TRAMP_BASE;
    uint32_t *stack = (uint32_t *)(copy + (ap_tramp_stack - ap_tramp_start));
    uint32_t *entry = (uint32_t *)(copy + (ap_tramp_entry - ap_tramp_start));

    *stack = (uint32_t)(uintptr_t)(ap_stacks[index] + AP_STACK_SIZE);
    *entry = (uint32_t)(uintptr_t)smp_ap_entry;

    /* INIT, wait 10 ms, then two SIPIs 200 us apart (MP Initialization Protocol). */
    icr_send(apic_id, ICR_LEVEL_ASSERT | ICR_INIT);
    wait_ns(10000000u);

    for (int i = 0; i < 2; i++)
    {
        icr_send(apic_id, ICR_STARTUP | (TRAMP_BASE >> 12));
        wait_ns(200000u);
    }

    uint64_t end = tsc_now_ns() + 100000000u;

    while (ap_online == before)
    {
        if (tsc_now_ns() > end)
            return -1;

        asm volatile("pause");
    }

    return 0;
}

unsigned int smp_start(void)
{
    const hal_acpi_info_t *acpi = hal_acpi_info();

    if (acpi == NULL || acpi->cpu_count <= 1 || tsc_khz() == 0)
        return cpus_online;

    /* Already done when the interrupts run through the APIC; maps and enables it otherwise. */
    if (!lapic_ready() && lapic_init(acpi->lapic_base) != 0)
        return cpus_online;

    uint32_t bsp_id = lapic_id();

    /* TRAMP_BASE is in the first 1 MiB, which the kernel identity maps. */
    k_memcpy((void *)TRAMP_BASE, ap_tramp_start, (size_t)(ap_tramp_end - ap_tramp_start));

    unsigned int index = 0;

    for (uint32_t i = 0; i < acpi->cpu_count && index < AP_MAX; i++)
    {
        if (acpi->apic_ids[i] == bsp_id)
            continue;

        if (start_ap(acpi->apic_ids[i], index) == 0)
            cpus_online++;
        else
            kernel_log(LOG_WARN, "SMP: a processor did not start");

        index++;
    }

    return cpus_online;
}

unsigned int hal_cpu_online_count(void)
{
    return cpus_online;
}
