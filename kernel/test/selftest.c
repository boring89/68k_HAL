/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "selftest.h"

#include "../printf/printf.h"
#include "../memory/heap.h"
#include "../timer/pit.h"

#include "../../arch/x86/io/io.h"

#include <stdint.h>

/*
 * QEMU isa-debug-exit device (-device isa-debug-exit,iobase=0xf4).
 * Writing value v makes QEMU exit with status (v << 1) | 1, so
 * PASS exits with 33 and FAIL exits with 35.
 */
#define QEMU_EXIT_PORT 0xF4
#define QEMU_EXIT_PASS 0x10
#define QEMU_EXIT_FAIL 0x11

static int selftest_failures;

#define SELFTEST_ASSERT(cond)                                   \
    do                                                          \
    {                                                           \
        if (!(cond))                                            \
        {                                                       \
            selftest_failures++;                                \
            kernel_printf("  %s:%d: assertion failed: %s\n",    \
                          __FILE__, __LINE__, #cond);           \
        }                                                       \
    } while (0)

#define SELFTEST_RUN(fn)                                        \
    do                                                          \
    {                                                           \
        int before = selftest_failures;                         \
        fn();                                                   \
        kernel_printf("[TEST] %s %s\n", #fn,                    \
                      selftest_failures == before ? "PASS" : "FAIL"); \
    } while (0)

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

static void test_pit_ticks(void)
{
    uint32_t start = pit_get_ticks();

    /* Each hlt wakes on the next interrupt; PIT runs at 100 Hz. */
    for (int i = 0; i < 1000 && pit_get_ticks() == start; i++)
        asm volatile("hlt");

    SELFTEST_ASSERT(pit_get_ticks() != start);
}

static void selftest_exit(uint8_t code)
{
    outb(QEMU_EXIT_PORT, code);

    /* Not running under QEMU with isa-debug-exit: stop here. */
    asm volatile("cli");

    while (1)
        asm volatile("hlt");
}

void selftest_run(void)
{
    kernel_printf("[TEST] selftest start\n");

    selftest_failures = 0;

    SELFTEST_RUN(test_heap_alloc_free);
    SELFTEST_RUN(test_pit_ticks);

    if (selftest_failures == 0)
    {
        kernel_printf("[TEST] all passed\n");
        selftest_exit(QEMU_EXIT_PASS);
    }

    kernel_printf("[TEST] %d failed\n", selftest_failures);
    selftest_exit(QEMU_EXIT_FAIL);
}
