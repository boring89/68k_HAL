/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Only built into the kernel by `make test-qemu` (-DKERNEL_SELFTEST). */
#ifdef KERNEL_SELFTEST

#include "selftest.h"

#include "kernel/printf/printf.h"
#include "kernel/memory/heap.h"
#include "kernel/memory/pmm.h"

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
