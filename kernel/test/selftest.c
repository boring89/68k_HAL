/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Only built into the kernel by `make test-qemu` (-DKERNEL_SELFTEST). */
#ifdef KERNEL_SELFTEST

#include "selftest.h"

#include "kernel/printf/printf.h"
#include "kernel/memory/heap.h"
#include "kernel/memory/multiboot.h"
#include "arch/x86/mmu/paging.h"
#include "kernel/memory/pmm.h"
#include "arch/x86/interrupt/exception.h"
#include "arch/x86/interrupt/irq_stub.h"

#include "hal/cpu.h"
#include "hal/irq.h"
#include "hal/timer.h"
#include "hal/mmu.h"

#include "arch/x86/cpu/cpu.h"
#include "arch/x86/io/io.h"

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

static void test_hal_timer_frequency(void)
{
    /* platform/pc/hal_init.c starts the PIT at 100 Hz. */
    SELFTEST_ASSERT(hal_timer_frequency() == 100);
}

static void test_hal_timer_ticks(void)
{
    uint32_t start = hal_timer_ticks();

    /* Each idle wakes on the next interrupt; PIT runs at 100 Hz. */
    for (int i = 0; i < 1000 && hal_timer_ticks() == start; i++)
        hal_cpu_idle();

    SELFTEST_ASSERT(hal_timer_ticks() != start);
}

/* EFLAGS.IF, bit 9 (Intel SDM Vol.1 3.4.3, Figure 3-8). */
#define EFLAGS_IF 0x00000200u

static uint32_t read_eflags(void)
{
    uint32_t value;

    asm volatile(
        "pushf\n\t"
        "pop %0"
        : "=r"(value)
        :
        : "memory");

    return value;
}

static void test_hal_cpu_interrupts(void)
{
    hal_cpu_disable_interrupts();
    SELFTEST_ASSERT((read_eflags() & EFLAGS_IF) == 0);

    /* selftest_run() is entered with interrupts on: restore that. */
    hal_cpu_enable_interrupts();
    SELFTEST_ASSERT((read_eflags() & EFLAGS_IF) != 0);
}

/* No keyboard input during the run, so the wake-up is the PIT tick. */
static void test_hal_cpu_idle(void)
{
    uint32_t start = hal_timer_ticks();

    hal_cpu_idle();

    SELFTEST_ASSERT(hal_timer_ticks() != start);
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

/* Pages for the hal_mmu tests, after the ones used by paging_map tests. */
#define TEST_HAL_MMU_VIRT (TEST_MAP_VIRT_A + 16 * PMM_FRAME_SIZE)

static paging_entry_t test_get_pte(uintptr_t virt)
{
    paging_entry_t *table = (paging_entry_t *)paging_table_virt(paging_dir_index(virt));

    return table[paging_table_index(virt)];
}

static void test_hal_mmu_map_flags(void)
{
    uintptr_t frame = pmm_alloc_frame();
    uintptr_t phys = 0;

    SELFTEST_ASSERT(hal_mmu_map(TEST_HAL_MMU_VIRT, frame, 0) == 0);
    SELFTEST_ASSERT(test_get_pte(TEST_HAL_MMU_VIRT) == (frame | PAGING_PRESENT));

    SELFTEST_ASSERT(hal_mmu_map(TEST_HAL_MMU_VIRT + PMM_FRAME_SIZE, frame,
                                HAL_MMU_WRITABLE | HAL_MMU_USER) == 0);
    SELFTEST_ASSERT(test_get_pte(TEST_HAL_MMU_VIRT + PMM_FRAME_SIZE)
                    == (frame | PAGING_PRESENT | PAGING_WRITABLE | PAGING_USER));

    SELFTEST_ASSERT(hal_mmu_get_phys(TEST_HAL_MMU_VIRT + 8, &phys) == 0);
    SELFTEST_ASSERT(phys == frame + 8);

    SELFTEST_ASSERT(hal_mmu_unmap(TEST_HAL_MMU_VIRT) == 0);
    SELFTEST_ASSERT(hal_mmu_unmap(TEST_HAL_MMU_VIRT + PMM_FRAME_SIZE) == 0);
    SELFTEST_ASSERT(hal_mmu_get_phys(TEST_HAL_MMU_VIRT, &phys) == -1);

    pmm_free_frame(frame);
}

static void test_hal_mmu_map_rejects_unknown_flags(void)
{
    uintptr_t frame = pmm_alloc_frame();
    uintptr_t phys = 0;

    SELFTEST_ASSERT(hal_mmu_map(TEST_HAL_MMU_VIRT, frame, 0x4) == -1);
    SELFTEST_ASSERT(hal_mmu_get_phys(TEST_HAL_MMU_VIRT, &phys) == -1);

    pmm_free_frame(frame);
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

/* "mov ecx, [eax]" (8B 08) and "mov [eax], ecx" (89 08) are both 2 bytes. */
#define MOV_EAX_LENGTH 2

static volatile int page_fault_count;
static volatile uint32_t page_fault_cr2;
static volatile uint32_t page_fault_code;

static int page_fault_hook(struct interrupt_frame *frame)
{
    if (frame->int_no != EXCEPTION_PAGE_FAULT)
        return -1;

    page_fault_count++;
    page_fault_cr2 = cpu_read_cr2();
    page_fault_code = frame->err_code;

    /*
     * #PF is a fault: returning would retry the same access forever,
     * so skip the fixed-length mov instead.
     */
    frame->eip += MOV_EAX_LENGTH;

    return 0;
}

/* Reset the counters and install the hook before a faulting access. */
static void page_fault_expect(void)
{
    page_fault_count = 0;
    page_fault_cr2 = 0xFFFFFFFF;
    page_fault_code = 0xFFFFFFFF;
    exception_set_test_hook(page_fault_hook);
}

static void test_read(uintptr_t address)
{
    asm volatile(
        ".byte 0x8B, 0x08" /* mov ecx, [eax] */
        :
        : "a"(address)
        : "ecx", "memory");
}

static void test_write(uintptr_t address)
{
    asm volatile(
        ".byte 0x89, 0x08" /* mov [eax], ecx */
        :
        : "a"(address), "c"(0xDEADBEEF)
        : "memory");
}

static void test_paging_unmap(void)
{
    uintptr_t frame = pmm_alloc_frame();
    uintptr_t virt = TEST_MAP_VIRT_A + 4 * PMM_FRAME_SIZE;
    uintptr_t phys = 0;

    SELFTEST_ASSERT(paging_map(virt, frame, PAGING_WRITABLE) == 0);

    /* Touch the page so its translation is in the TLB. */
    *(volatile uint32_t *)virt = 1;

    SELFTEST_ASSERT(paging_unmap(virt) == 0);
    SELFTEST_ASSERT(paging_get_phys(virt, &phys) == -1);

    /* The frame still belongs to the caller. */
    SELFTEST_ASSERT(pmm_is_frame_used(frame));

    /* Stale TLB entry would make this read succeed instead of fault. */
    page_fault_expect();
    test_read(virt);
    exception_set_test_hook(0);

    SELFTEST_ASSERT(page_fault_count == 1);
    SELFTEST_ASSERT(page_fault_cr2 == virt);
    /* Not present, read, supervisor. */
    SELFTEST_ASSERT(page_fault_code == 0);

    /* The page can be mapped again after unmapping. */
    SELFTEST_ASSERT(paging_map(virt, frame, PAGING_WRITABLE) == 0);
    SELFTEST_ASSERT(*(volatile uint32_t *)virt == 1);
    SELFTEST_ASSERT(paging_unmap(virt) == 0);

    pmm_free_frame(frame);
}

static void test_paging_unmap_rejects(void)
{
    /* Misaligned, never mapped, PDE not present, already unmapped. */
    SELFTEST_ASSERT(paging_unmap(TEST_MAP_VIRT_A + 1) == -1);
    SELFTEST_ASSERT(paging_unmap(0) == -1);
    SELFTEST_ASSERT(paging_unmap(0xC0000000) == -1);
    SELFTEST_ASSERT(paging_unmap(TEST_MAP_VIRT_A + 4 * PMM_FRAME_SIZE) == -1);

    /* The recursive region stays mapped. */
    SELFTEST_ASSERT(paging_unmap(PAGING_TABLES_VIRT) == -1);
    SELFTEST_ASSERT(paging_unmap(PAGING_DIRECTORY_VIRT) == -1);
}

static void test_paging_fault_write_unmapped(void)
{
    /* Directory entry 512 has no page table: fault at the PDE level. */
    page_fault_expect();
    test_write(0x80000000);
    exception_set_test_hook(0);

    SELFTEST_ASSERT(page_fault_count == 1);
    SELFTEST_ASSERT(page_fault_cr2 == 0x80000000);
    SELFTEST_ASSERT(page_fault_code == EXCEPTION_PF_WRITE);
}

static void test_paging_fault_null_pointer(void)
{
    /* Page 0 is unmapped, so a null write faults instead of hitting the IVT. */
    page_fault_expect();
    test_write(0);
    exception_set_test_hook(0);

    SELFTEST_ASSERT(page_fault_count == 1);
    SELFTEST_ASSERT(page_fault_cr2 == 0);
    SELFTEST_ASSERT(page_fault_code == EXCEPTION_PF_WRITE);
}

static void test_paging_fault_read_only(void)
{
    /* TEST_MAP_VIRT_A + 2 pages was mapped without PAGING_WRITABLE. */
    uintptr_t virt = TEST_MAP_VIRT_A + 2 * PMM_FRAME_SIZE;

    /* paging_init sets CR0.WP so ring 0 honours R/W. */
    SELFTEST_ASSERT(cpu_read_cr0() & CPU_CR0_WP);

    /* Reading is allowed. */
    page_fault_expect();
    test_read(virt);
    exception_set_test_hook(0);

    SELFTEST_ASSERT(page_fault_count == 0);

    page_fault_expect();
    test_write(virt);
    exception_set_test_hook(0);

    SELFTEST_ASSERT(page_fault_count == 1);
    SELFTEST_ASSERT(page_fault_cr2 == virt);
    /* Protection violation, write, supervisor. */
    SELFTEST_ASSERT(page_fault_code == (EXCEPTION_PF_PRESENT | EXCEPTION_PF_WRITE));
}

static void test_paging_multiboot_mapped(void)
{
    const multiboot_info_t *mbi = multiboot_get_info();
    uintptr_t address = (uintptr_t)mbi;
    uintptr_t phys = 0;

    SELFTEST_ASSERT(paging_get_phys(address, &phys) == 0);
    SELFTEST_ASSERT(phys == address);
    SELFTEST_ASSERT(paging_get_phys(address + sizeof(*mbi) - 1, &phys) == 0);

    SELFTEST_ASSERT(mbi->flags & MULTIBOOT_INFO_MEM_MAP);

    uintptr_t mmap_end = (uintptr_t)mbi->mmap_addr + mbi->mmap_length;

    SELFTEST_ASSERT(paging_get_phys(mbi->mmap_addr, &phys) == 0);
    SELFTEST_ASSERT(phys == mbi->mmap_addr);
    SELFTEST_ASSERT(paging_get_phys(mmap_end - 1, &phys) == 0);

    /* Walk the map like the shell's mmap command does. */
    int entries = 0;

    for (const multiboot_mmap_entry_t *entry = multiboot_mmap_first();
         entry != 0; entry = multiboot_mmap_next(entry))
        entries++;

    SELFTEST_ASSERT(entries > 0);
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

/* IDT gate descriptor (Intel SDM Vol.3A 6.11, Figure 6-2). */
struct idt_gate
{
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_register
{
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static void test_idt_irq_gates(void)
{
    struct idt_register idtr;

    asm volatile("sidt %0" : "=m"(idtr));

    const struct idt_gate *idt = (const struct idt_gate *)idtr.base;

    for (int i = 0; i < IRQ_STUB_COUNT; i++)
    {
        const struct idt_gate *gate = &idt[IRQ_STUB_VECTOR_BASE + i];
        uint32_t offset = gate->offset_low | ((uint32_t)gate->offset_high << 16);

        SELFTEST_ASSERT(offset == irq_stub_table[i]);
        SELFTEST_ASSERT(gate->selector == 0x08);
        /* P=1, DPL=0, 32-bit interrupt gate. */
        SELFTEST_ASSERT(gate->type_attr == 0x8E);
    }
}

static volatile int irq5_count;

static void irq5_test_handler(void)
{
    irq5_count++;
}

/*
 * IRQ 5 is masked at the PIC, so only this software INT reaches the
 * stub. The handler stays registered afterwards; the line is unused.
 */
static void test_irq_stub_line_number(void)
{
    irq5_count = 0;
    SELFTEST_ASSERT(hal_irq_register(5, irq5_test_handler) == 0);

    asm volatile("int $0x25" : : : "memory");

    SELFTEST_ASSERT(irq5_count == 1);
}

static void test_hal_irq_register_rejects(void)
{
    SELFTEST_ASSERT(hal_irq_register(16, irq5_test_handler) == -1);
    SELFTEST_ASSERT(hal_irq_register(0xFFFFFFFFu, irq5_test_handler) == -1);
    SELFTEST_ASSERT(hal_irq_register(6, NULL) == -1);

    /* Line 0 belongs to the PIT, line 5 to test_irq_stub_line_number. */
    SELFTEST_ASSERT(hal_irq_register(0, irq5_test_handler) == -1);
    SELFTEST_ASSERT(hal_irq_register(5, irq5_test_handler) == -1);

    /* The PIT handler must still be the one running. */
    uint32_t start = hal_timer_ticks();
    hal_cpu_idle();
    SELFTEST_ASSERT(hal_timer_ticks() != start);
}

/* 8259A data ports; reading them returns the IMR (OSDev Wiki "8259 PIC"). */
#define PIC_MASTER_DATA 0x21
#define PIC_SLAVE_DATA 0xA1

/* pic_init masks everything; only the PIT (0) and keyboard (1) unmask. */
static void test_pic_boot_mask(void)
{
    SELFTEST_ASSERT(inb(PIC_MASTER_DATA) == 0xFC);
    SELFTEST_ASSERT(inb(PIC_SLAVE_DATA) == 0xFF);
}

static void test_hal_irq_unmask_rejects(void)
{
    SELFTEST_ASSERT(hal_irq_unmask(16) == -1);
    SELFTEST_ASSERT(hal_irq_unmask(0xFFFFFFFFu) == -1);

    SELFTEST_ASSERT(inb(PIC_MASTER_DATA) == 0xFC);
    SELFTEST_ASSERT(inb(PIC_SLAVE_DATA) == 0xFF);
}

static void test_hal_irq_unmask_master(void)
{
    uint8_t master = inb(PIC_MASTER_DATA);
    uint8_t slave = inb(PIC_SLAVE_DATA);

    /* Nothing drives IRQ 5; interrupts stay off while it is unmasked. */
    hal_cpu_disable_interrupts();

    SELFTEST_ASSERT(hal_irq_unmask(5) == 0);
    SELFTEST_ASSERT(inb(PIC_MASTER_DATA) == (master & ~(1u << 5)));
    SELFTEST_ASSERT(inb(PIC_SLAVE_DATA) == slave);

    outb(PIC_MASTER_DATA, master);
    hal_cpu_enable_interrupts();
}

/* A slave line also needs the cascade input (master line 2) unmasked. */
static void test_hal_irq_unmask_slave(void)
{
    uint8_t master = inb(PIC_MASTER_DATA);
    uint8_t slave = inb(PIC_SLAVE_DATA);

    hal_cpu_disable_interrupts();

    SELFTEST_ASSERT(hal_irq_unmask(8) == 0);
    SELFTEST_ASSERT(inb(PIC_MASTER_DATA) == (master & ~(1u << 2)));
    SELFTEST_ASSERT(inb(PIC_SLAVE_DATA) == (slave & ~(1u << 0)));

    outb(PIC_SLAVE_DATA, slave);
    outb(PIC_MASTER_DATA, master);
    hal_cpu_enable_interrupts();
}

static void __attribute__((noreturn)) selftest_exit(uint8_t code)
{
    outb(QEMU_EXIT_PORT, code);

    /* Not running under QEMU with isa-debug-exit: stop here. */
    hal_cpu_halt();
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
    SELFTEST_RUN(test_hal_timer_ticks);
    SELFTEST_RUN(test_hal_timer_frequency);
    SELFTEST_RUN(test_divide_error_exception);
    SELFTEST_RUN(test_idt_irq_gates);
    SELFTEST_RUN(test_irq_stub_line_number);
    SELFTEST_RUN(test_hal_irq_register_rejects);
    SELFTEST_RUN(test_pic_boot_mask);
    SELFTEST_RUN(test_hal_irq_unmask_rejects);
    SELFTEST_RUN(test_hal_irq_unmask_master);
    SELFTEST_RUN(test_hal_irq_unmask_slave);
    SELFTEST_RUN(test_cpu_read_cr0);
    SELFTEST_RUN(test_hal_cpu_interrupts);
    SELFTEST_RUN(test_hal_cpu_idle);
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
    SELFTEST_RUN(test_hal_mmu_map_flags);
    SELFTEST_RUN(test_hal_mmu_map_rejects_unknown_flags);
    SELFTEST_RUN(test_paging_unmap);
    SELFTEST_RUN(test_paging_unmap_rejects);
    SELFTEST_RUN(test_paging_fault_write_unmapped);
    SELFTEST_RUN(test_paging_fault_null_pointer);
    SELFTEST_RUN(test_paging_fault_read_only);
    SELFTEST_RUN(test_paging_multiboot_mapped);
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
