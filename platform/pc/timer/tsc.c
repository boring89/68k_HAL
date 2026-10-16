/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tsc.h"

#include "tsc_math.h"

#include "arch/x86/cpu/cpu.h"
#include "arch/x86/io/io.h"

#include "hal/cpu.h"

/*
 * Calibration with PIT channel 2 (OSDev Wiki "Programmable Interval
 * Timer", "Time Stamp Counter"): channel 2 counts down once in mode 0
 * and its OUT pin shows up in bit 5 of port 0x61 when it reaches zero.
 * Unlike channel 0 it needs no interrupt, so this works with
 * interrupts disabled.
 *
 *   port 0x61 bit 0 = channel 2 gate, bit 1 = PC speaker (keep off)
 *   command 0xB0    = channel 2, lobyte/hibyte, mode 0, binary
 */
#define PIT_CHANNEL2 0x42
#define PIT_COMMAND 0x43
#define PORT_B 0x61
#define PORT_B_GATE 0x01
#define PORT_B_SPEAKER 0x02
#define PORT_B_OUT2 0x20

/* 20 ms of PIT clock. */
#define CALIBRATE_COUNT 23864u

static uint32_t measured_khz;
static uint32_t ns_mult;
static uint64_t tsc_start;

static uint64_t measure_once(void)
{
    uint8_t port_b = inb(PORT_B);

    /* Gate low while loading the count so counting starts together. */
    outb(PORT_B, (uint8_t)(port_b & ~(PORT_B_GATE | PORT_B_SPEAKER)));
    outb(PIT_COMMAND, 0xB0);
    outb(PIT_CHANNEL2, CALIBRATE_COUNT & 0xFF);
    outb(PIT_CHANNEL2, CALIBRATE_COUNT >> 8);

    uint64_t begin = cpu_rdtsc();

    outb(PORT_B, (uint8_t)((port_b & ~PORT_B_SPEAKER) | PORT_B_GATE));

    while (!(inb(PORT_B) & PORT_B_OUT2))
        ;

    uint64_t end = cpu_rdtsc();

    outb(PORT_B, port_b);

    return end - begin;
}

int tsc_init(void)
{
    if (!(hal_cpu_info()->features & HAL_CPU_FEATURE_TSC))
        return -1;

    /* The shortest of a few runs is the least disturbed one (SMIs, a hypervisor). */
    uint64_t best = measure_once();

    for (int i = 0; i < 2; i++)
    {
        uint64_t cycles = measure_once();

        if (cycles < best)
            best = cycles;
    }

    measured_khz = tsc_khz_from_sample(best, CALIBRATE_COUNT);
    ns_mult = tsc_ns_multiplier(measured_khz);

    if (ns_mult == 0)
    {
        measured_khz = 0;
        return -1;
    }

    tsc_start = cpu_rdtsc();

    return 0;
}

uint32_t tsc_khz(void)
{
    return measured_khz;
}

uint64_t tsc_now_ns(void)
{
    if (ns_mult == 0)
        return 0;

    return tsc_cycles_to_ns(cpu_rdtsc() - tsc_start, ns_mult);
}
