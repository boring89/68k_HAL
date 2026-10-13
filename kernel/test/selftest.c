/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Only built into the kernel by `make test-qemu` (-DKERNEL_SELFTEST). */
#ifdef KERNEL_SELFTEST

#include "selftest.h"

#include "../printf/printf.h"
#include "../memory/heap.h"
#include "../memory/pmm.h"
#include "../timer/pit.h"
#include "../interrupt/exception.h"

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

static void test_pit_ticks(void)
{
    uint32_t start = pit_get_ticks();

    /* Each hlt wakes on the next interrupt; PIT runs at 100 Hz. */
    for (int i = 0; i < 1000 && pit_get_ticks() == start; i++)
        asm volatile("hlt");

    SELFTEST_ASSERT(pit_get_ticks() != start);
}

/* Vector 0, Divide Error (Intel SDM Vol.3 Ch.6, Table 6-1). */
#define EXCEPTION_DIVIDE_ERROR 0

/* Length of "div ecx" (F7 F1) in test_divide_error_exception. */
#define DIV_ECX_LENGTH 2

static volatile int divide_error_count;
static volatile uint32_t divide_error_code;

static int divide_error_hook(struct interrupt_frame *frame)
{
    if (frame->int_no != EXCEPTION_DIVIDE_ERROR)
        return -1;

    divide_error_count++;
    divide_error_code = frame->err_code;

    /* #DE is a fault: eip points at the div, so skip over it. */
    frame->eip += DIV_ECX_LENGTH;

    return 0;
}

static void test_divide_error_exception(void)
{
    divide_error_count = 0;
    divide_error_code = 0xFFFFFFFF;

    exception_set_test_hook(divide_error_hook);

    /* edx:eax = 1, ecx = 0; "div ecx" must be exactly DIV_ECX_LENGTH bytes. */
    asm volatile(
        "xor %%edx, %%edx\n\t"
        "mov $1, %%eax\n\t"
        "xor %%ecx, %%ecx\n\t"
        ".byte 0xF7, 0xF1\n\t" /* div ecx */
        :
        :
        : "eax", "ecx", "edx", "cc");

    exception_set_test_hook(0);

    SELFTEST_ASSERT(divide_error_count == 1);
    /* #DE pushes no error code; the stub supplies a dummy 0. */
    SELFTEST_ASSERT(divide_error_code == 0);
}

static void __attribute__((noreturn)) selftest_exit(uint8_t code)
{
    outb(QEMU_EXIT_PORT, code);

    /* Not running under QEMU with isa-debug-exit: stop here. */
    asm volatile("cli");

    while (1)
        asm volatile("hlt");
}

void selftest_abort(void)
{
    selftest_exit(QEMU_EXIT_FAIL);
}

void selftest_run(void)
{
    kernel_printf("[TEST] selftest start\n");

    selftest_failures = 0;

    SELFTEST_RUN(test_heap_alloc_free);
    SELFTEST_RUN(test_heap_region_reserved_in_pmm);
    SELFTEST_RUN(test_pit_ticks);
    SELFTEST_RUN(test_divide_error_exception);

    if (selftest_failures == 0)
    {
        kernel_printf("[TEST] all passed\n");
        selftest_exit(QEMU_EXIT_PASS);
    }

    kernel_printf("[TEST] %d failed\n", selftest_failures);
    selftest_exit(QEMU_EXIT_FAIL);
}

#endif
