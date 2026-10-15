/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Only built into the kernel by `make test-qemu` (-DKERNEL_SELFTEST). */
#ifdef KERNEL_SELFTEST

#include "selftest.h"

#include "kernel/m68k/guest.h"
#include "kernel/printf/printf.h"
#include "kernel/memory/heap.h"
#include "kernel/memory/pmm.h"

#include "emu/m68k/trap.h"
#include "emu/m68k/vm.h"
#include "hal/cpu.h"
#include "hal/timer.h"

#include <stdint.h>

static int selftest_failures;

void selftest_report_failure(const char *file, int line, const char *cond)
{
    selftest_failures++;
    kernel_printf("  %s:%d: assertion failed: %s\n", file, line, cond);
}

int selftest_failure_count(void)
{
    return selftest_failures;
}

static void test_heap_alloc_free(void)
{
    SELFTEST_ASSERT(kmalloc(0) == 0);

    void *a = kmalloc(16);
    SELFTEST_ASSERT(a != 0);

    void *b = kmalloc(32);
    SELFTEST_ASSERT(b != 0);
    SELFTEST_ASSERT(a != b);

    kfree(a);

    /* A freed block of the same size should be reused. */
    void *c = kmalloc(16);
    SELFTEST_ASSERT(c == a);

    kfree(b);
    kfree(c);
}

static void test_heap_region_reserved_in_pmm(void)
{
    uintptr_t start = heap_get_start();

    /* The region comes from pmm_alloc_frames, so it is frame aligned. */
    SELFTEST_ASSERT(start % PMM_FRAME_SIZE == 0);

    int unmarked = 0;

    for (uintptr_t offset = 0; offset < KERNEL_HEAP_SIZE;
         offset += PMM_FRAME_SIZE)
    {
        if (!pmm_is_frame_used(start + offset))
            unmarked++;
    }

    SELFTEST_ASSERT(unmarked == 0);
}


static void test_hal_timer_ticks(void)
{
    uint32_t start = hal_timer_ticks();

    /* Each idle wakes on the next interrupt; PIT runs at 100 Hz. */
    for (int i = 0; i < 1000 && hal_timer_ticks() == start; i++)
        hal_cpu_idle();

    SELFTEST_ASSERT(hal_timer_ticks() != start);
}

/* No keyboard input during the run, so the wake-up is the PIT tick. */
static void test_hal_cpu_idle(void)
{
    uint32_t start = hal_timer_ticks();

    hal_cpu_idle();

    SELFTEST_ASSERT(hal_timer_ticks() != start);
}

/* Runs a 68k program in the guest RAM: arithmetic plus an A-line trap. */
static void test_m68k_guest_program(void)
{
    static const uint8_t program[] = {
        0x70, 0x15,                 /* MOVEQ #21,D0 */
        0xD0, 0x80,                 /* ADD.L D0,D0 */
        0xA9, 0xF4,                 /* A-line trap $A9F4 */
        0x72, 0x07,                 /* MOVEQ #7,D1 */
        0x4E, 0x72, 0x27, 0x00,     /* STOP #$2700 */
    };
    m68k_trap_record_t record;

    SELFTEST_ASSERT(m68k_guest_init() == 0);
    SELFTEST_ASSERT(m68k_vm_load(0x1000, program, sizeof(program)) == 0);
    SELFTEST_ASSERT(m68k_vm_reset(0x1000, 0x8000) == 0);

    m68k_vm_run(10000);

    SELFTEST_ASSERT(m68k_vm_halted());
    SELFTEST_ASSERT(m68k_vm_get_reg(M68K_VM_REG_D0) == 42);
    SELFTEST_ASSERT(m68k_vm_get_reg(M68K_VM_REG_D0 + 1) == 7);
    SELFTEST_ASSERT(m68k_trap_total() == 1);
    SELFTEST_ASSERT(m68k_trap_log_get(0, &record) == 0);
    SELFTEST_ASSERT(record.opcode == 0xA9F4);
    SELFTEST_ASSERT(record.pc == 0x1004);
}

/* DIVU.L with a 64-bit dividend: Musashi calls our 64-bit division helpers. */
static void test_m68k_guest_divide_64(void)
{
    static const uint8_t program[] = {
        0x70, 0x05,                 /* MOVEQ #5,D0 */
        0x74, 0x01,                 /* MOVEQ #1,D2 */
        0x72, 0x07,                 /* MOVEQ #7,D1 */
        0x4C, 0x41, 0x04, 0x02,     /* DIVU.L D1,D2:D0 */
        0x4E, 0x72, 0x27, 0x00,     /* STOP #$2700 */
    };

    SELFTEST_ASSERT(m68k_guest_init() == 0);
    SELFTEST_ASSERT(m68k_vm_load(0x1000, program, sizeof(program)) == 0);
    SELFTEST_ASSERT(m68k_vm_reset(0x1000, 0x8000) == 0);

    m68k_vm_run(10000);

    /* (1 << 32 | 5) / 7 = 613566757, remainder 2 */
    SELFTEST_ASSERT(m68k_vm_halted());
    SELFTEST_ASSERT(m68k_vm_get_reg(M68K_VM_REG_D0) == 613566757u);
    SELFTEST_ASSERT(m68k_vm_get_reg(M68K_VM_REG_D0 + 2) == 2);
}

void selftest_abort(void)
{
    selftest_platform_exit(0);
}

void selftest_run(void)
{
    kernel_printf("[TEST] selftest start\n");

    selftest_failures = 0;

    SELFTEST_RUN(test_heap_alloc_free);
    SELFTEST_RUN(test_heap_region_reserved_in_pmm);
    SELFTEST_RUN(test_hal_timer_ticks);
    SELFTEST_RUN(test_hal_cpu_idle);
    SELFTEST_RUN(test_m68k_guest_program);
    SELFTEST_RUN(test_m68k_guest_divide_64);

    selftest_arch_run();
    selftest_platform_run();

    if (selftest_failures == 0)
    {
        kernel_printf("[TEST] all passed\n");
        selftest_platform_exit(1);
    }

    kernel_printf("[TEST] %d failed\n", selftest_failures);
    selftest_platform_exit(0);
}

#endif
