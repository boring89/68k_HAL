/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Only built into the kernel by `make test-qemu` (-DKERNEL_SELFTEST). */
#ifdef KERNEL_SELFTEST

#include "selftest.h"

#include "../printf/printf.h"
#include "../memory/heap.h"
#include "../memory/paging.h"
#include "../memory/pmm.h"
#include "../timer/pit.h"
#include "../interrupt/exception.h"

#include "../../arch/x86/cpu/cpu.h"
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

static void test_cpu_read_cr0(void)
{
    /* The bootloader enters the kernel in protected mode. */
    SELFTEST_ASSERT(cpu_read_cr0() & CPU_CR0_PE);
}

static void test_paging_enabled(void)
{
    SELFTEST_ASSERT(cpu_read_cr0() & CPU_CR0_PG);
}

static void test_paging_recursive_entry(void)
{
    /* Seen through itself, directory[1023] must point back to CR3. */
    paging_entry_t *directory = (paging_entry_t *)PAGING_DIRECTORY_VIRT;
    paging_entry_t self = directory[PAGING_RECURSIVE_INDEX];

    SELFTEST_ASSERT(paging_entry_is_present(self));
    SELFTEST_ASSERT(paging_entry_get_phys(self) == cpu_read_cr3());

    /* The table frames came from the PMM and must stay allocated. */
    SELFTEST_ASSERT(pmm_is_frame_used(cpu_read_cr3()));
    SELFTEST_ASSERT(pmm_is_frame_used(paging_entry_get_phys(directory[0])));
}

static void test_paging_identity_map(void)
{
    paging_entry_t *directory = (paging_entry_t *)PAGING_DIRECTORY_VIRT;
    paging_entry_t *table = (paging_entry_t *)paging_table_virt(0);
    uintptr_t local = (uintptr_t)&selftest_failures;

    SELFTEST_ASSERT(paging_entry_is_present(directory[0]));
    SELFTEST_ASSERT(!paging_entry_is_present(directory[1]));

    /* Page 0 is left unmapped to catch null pointers. */
    SELFTEST_ASSERT(!paging_entry_is_present(table[0]));

    SELFTEST_ASSERT(paging_entry_is_present(table[paging_table_index(0xB8000)]));
    SELFTEST_ASSERT(paging_entry_get_phys(table[paging_table_index(0xB8000)])
                    == 0xB8000);

    /* A kernel variable: its page maps to the same physical page. */
    SELFTEST_ASSERT(paging_entry_get_phys(table[paging_table_index(local)])
                    == (local & ~(uintptr_t)(PMM_FRAME_SIZE - 1)));

    SELFTEST_ASSERT(paging_entry_get_phys(table[PAGING_ENTRIES_PER_TABLE - 1])
                    == PAGING_IDENTITY_END - PMM_FRAME_SIZE);
}

static void test_paging_get_phys_identity(void)
{
    uintptr_t phys = 0;
    uintptr_t local = (uintptr_t)&selftest_failures;

    SELFTEST_ASSERT(paging_get_phys(0xB8000, &phys) == 0);
    SELFTEST_ASSERT(phys == 0xB8000);

    /* The offset within the page is carried over. */
    SELFTEST_ASSERT(paging_get_phys(0xB8123, &phys) == 0);
    SELFTEST_ASSERT(phys == 0xB8123);

    SELFTEST_ASSERT(paging_get_phys(local, &phys) == 0);
    SELFTEST_ASSERT(phys == local);

    /* First and last bytes of the identity mapped region. */
    SELFTEST_ASSERT(paging_get_phys(0x1000, &phys) == 0);
    SELFTEST_ASSERT(phys == 0x1000);
    SELFTEST_ASSERT(paging_get_phys(PAGING_IDENTITY_END - 1, &phys) == 0);
    SELFTEST_ASSERT(phys == PAGING_IDENTITY_END - 1);
}

static void test_paging_get_phys_unmapped(void)
{
    uintptr_t phys = 0x12345678;

    /* Page 0: PDE present, PTE not present. */
    SELFTEST_ASSERT(paging_get_phys(0, &phys) == -1);
    SELFTEST_ASSERT(paging_get_phys(0xFFF, &phys) == -1);

    /* Above 4 MiB: PDE not present, so the table is never touched. */
    SELFTEST_ASSERT(paging_get_phys(PAGING_IDENTITY_END, &phys) == -1);
    SELFTEST_ASSERT(paging_get_phys(0xC0000000, &phys) == -1);

    /* *phys is left unchanged on failure. */
    SELFTEST_ASSERT(phys == 0x12345678);

    SELFTEST_ASSERT(paging_get_phys(0xB8000, 0) == -1);
}

static void test_paging_get_phys_recursive(void)
{
    uintptr_t phys = 0;
    paging_entry_t *directory = (paging_entry_t *)PAGING_DIRECTORY_VIRT;

    /* The recursive region translates to the table frames themselves. */
    SELFTEST_ASSERT(paging_get_phys(PAGING_DIRECTORY_VIRT, &phys) == 0);
    SELFTEST_ASSERT(phys == cpu_read_cr3());

    SELFTEST_ASSERT(paging_get_phys(paging_table_virt(0), &phys) == 0);
    SELFTEST_ASSERT(phys == paging_entry_get_phys(directory[0]));
}

/* Virtual pages for the paging_map tests: unused, outside the identity map. */
#define TEST_MAP_VIRT_A 0x40000000u /* directory entry 256 */
#define TEST_MAP_VIRT_B 0x40001000u /* same page table as A */
#define TEST_MAP_VIRT_NEW_TABLE PAGING_IDENTITY_END /* directory entry 1 */

static void test_paging_map_alias(void)
{
    uintptr_t frame = pmm_alloc_frame();
    uintptr_t phys = 0;

    SELFTEST_ASSERT(frame != 0);
    SELFTEST_ASSERT(paging_map(TEST_MAP_VIRT_A, frame, PAGING_WRITABLE) == 0);
    SELFTEST_ASSERT(paging_map(TEST_MAP_VIRT_B, frame, PAGING_WRITABLE) == 0);

    SELFTEST_ASSERT(paging_get_phys(TEST_MAP_VIRT_A + 4, &phys) == 0);
    SELFTEST_ASSERT(phys == frame + 4);

    /* Two virtual addresses, one frame: a write through A shows up at B. */
    volatile uint32_t *a = (volatile uint32_t *)TEST_MAP_VIRT_A;
    volatile uint32_t *b = (volatile uint32_t *)TEST_MAP_VIRT_B;

    *a = 0xCAFEBABE;
    SELFTEST_ASSERT(*b == 0xCAFEBABE);

    /* The frame is below 4 MiB, so it is also visible at its identity address. */
    if (frame < PAGING_IDENTITY_END)
        SELFTEST_ASSERT(*(volatile uint32_t *)frame == 0xCAFEBABE);
}

static void test_paging_map_creates_table(void)
{
    paging_entry_t *directory = (paging_entry_t *)PAGING_DIRECTORY_VIRT;
    uint32_t dir_index = paging_dir_index(TEST_MAP_VIRT_NEW_TABLE);

    SELFTEST_ASSERT(!paging_entry_is_present(directory[dir_index]));

    uintptr_t frame = pmm_alloc_frame();
    size_t free_before = pmm_get_free_frames();

    SELFTEST_ASSERT(paging_map(TEST_MAP_VIRT_NEW_TABLE, frame, PAGING_WRITABLE) == 0);

    /* Exactly one frame was taken for the new page table. */
    SELFTEST_ASSERT(pmm_get_free_frames() == free_before - 1);
    SELFTEST_ASSERT(paging_entry_is_present(directory[dir_index]));
    SELFTEST_ASSERT(pmm_is_frame_used(paging_entry_get_phys(directory[dir_index])));

    /* The new table was zeroed: only the entry just written is present. */
    paging_entry_t *table = (paging_entry_t *)paging_table_virt(dir_index);
    int present = 0;

    for (uint32_t i = 0; i < PAGING_ENTRIES_PER_TABLE; i++)
    {
        if (paging_entry_is_present(table[i]))
            present++;
    }

    SELFTEST_ASSERT(present == 1);

    /* A second page in the same 4 MiB reuses the table. */
    free_before = pmm_get_free_frames();
    SELFTEST_ASSERT(paging_map(TEST_MAP_VIRT_NEW_TABLE + PMM_FRAME_SIZE,
                               frame, PAGING_WRITABLE) == 0);
    SELFTEST_ASSERT(pmm_get_free_frames() == free_before);

    volatile uint32_t *p = (volatile uint32_t *)TEST_MAP_VIRT_NEW_TABLE;

    *p = 0x12345678;
    SELFTEST_ASSERT(*p == 0x12345678);
}

static void test_paging_map_flags(void)
{
    uintptr_t frame = pmm_alloc_frame();
    uintptr_t virt = TEST_MAP_VIRT_A + 2 * PMM_FRAME_SIZE;

    /* No flags at all: PRESENT is added, the page is read-only. */
    SELFTEST_ASSERT(paging_map(virt, frame, 0) == 0);

    paging_entry_t *table = (paging_entry_t *)paging_table_virt(paging_dir_index(virt));
    paging_entry_t entry = table[paging_table_index(virt)];

    SELFTEST_ASSERT(entry == (frame | PAGING_PRESENT));
}

static void test_paging_map_rejects(void)
{
    uintptr_t frame = pmm_alloc_frame();
    uintptr_t phys = 0;
    uintptr_t virt = TEST_MAP_VIRT_A + 3 * PMM_FRAME_SIZE;

    /* Misaligned addresses. */
    SELFTEST_ASSERT(paging_map(virt + 1, frame, PAGING_WRITABLE) == -1);
    SELFTEST_ASSERT(paging_map(virt, frame + 1, PAGING_WRITABLE) == -1);
    SELFTEST_ASSERT(paging_get_phys(virt, &phys) == -1);

    /* Already mapped: the existing mapping is left alone. */
    SELFTEST_ASSERT(paging_map(TEST_MAP_VIRT_A, frame, PAGING_WRITABLE) == -1);
    SELFTEST_ASSERT(paging_get_phys(TEST_MAP_VIRT_A, &phys) == 0);
    SELFTEST_ASSERT(phys != frame);
    SELFTEST_ASSERT(paging_map(0xB8000, frame, PAGING_WRITABLE) == -1);

    /* The recursive region must never be mapped over. */
    SELFTEST_ASSERT(paging_map(PAGING_TABLES_VIRT, frame, PAGING_WRITABLE) == -1);
    SELFTEST_ASSERT(paging_map(PAGING_DIRECTORY_VIRT, frame, PAGING_WRITABLE) == -1);
    SELFTEST_ASSERT(paging_get_phys(PAGING_DIRECTORY_VIRT, &phys) == 0);
    SELFTEST_ASSERT(phys == cpu_read_cr3());

    pmm_free_frame(frame);
}

static void test_cpu_invlpg(void)
{
    /* Only checks that it executes; its effect is tested with paging. */
    cpu_invlpg(0xB8000);
    SELFTEST_ASSERT(1);
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
    SELFTEST_RUN(test_cpu_read_cr0);
    SELFTEST_RUN(test_paging_enabled);
    SELFTEST_RUN(test_paging_recursive_entry);
    SELFTEST_RUN(test_paging_identity_map);
    SELFTEST_RUN(test_paging_get_phys_identity);
    SELFTEST_RUN(test_paging_get_phys_unmapped);
    SELFTEST_RUN(test_paging_get_phys_recursive);
    SELFTEST_RUN(test_paging_map_alias);
    SELFTEST_RUN(test_paging_map_creates_table);
    SELFTEST_RUN(test_paging_map_flags);
    SELFTEST_RUN(test_paging_map_rejects);
    SELFTEST_RUN(test_cpu_invlpg);

    if (selftest_failures == 0)
    {
        kernel_printf("[TEST] all passed\n");
        selftest_exit(QEMU_EXIT_PASS);
    }

    kernel_printf("[TEST] %d failed\n", selftest_failures);
    selftest_exit(QEMU_EXIT_FAIL);
}

#endif
