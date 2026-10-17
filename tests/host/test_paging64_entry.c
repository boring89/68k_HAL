/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include <stdint.h>

#include "arch/x86_64/mmu/paging_entry.h"
#include "hal/mmu.h"

static void test_index_levels(void)
{
    /* 0x0000_0000_0010_0000: the kernel's physical start: all upper indices 0, PT index 0x100. */
    TEST_ASSERT_EQ_INT(0, paging64_index(0x100000, 4));
    TEST_ASSERT_EQ_INT(0, paging64_index(0x100000, 3));
    TEST_ASSERT_EQ_INT(0, paging64_index(0x100000, 2));
    TEST_ASSERT_EQ_INT(0x100, paging64_index(0x100000, 1));

    /* One 2 MiB page up: PD index 1. One GiB up: PDPT index 1. 512 GiB up: PML4 index 1. */
    TEST_ASSERT_EQ_INT(1, paging64_index(0x200000, 2));
    TEST_ASSERT_EQ_INT(1, paging64_index(0x40000000ull, 3));
    TEST_ASSERT_EQ_INT(1, paging64_index(0x8000000000ull, 4));

    /* The offset never leaks into an index. */
    TEST_ASSERT_EQ_INT(0, paging64_index(0xFFF, 1));
    TEST_ASSERT_EQ_INT(1, paging64_index(0x1FFF, 1));
}

static void test_index_kernel_layout(void)
{
    /* The three upper-half PML4 slots of arch/x86_64/mmu/paging.h. */
    TEST_ASSERT_EQ_INT(256, paging64_index(0xFFFF800000000000ull, 4));
    TEST_ASSERT_EQ_INT(384, paging64_index(0xFFFFC00000000000ull, 4));
    TEST_ASSERT_EQ_INT(511, paging64_index(0xFFFFFFFF80000000ull, 4));

    /* The kernel image: PDPT 510 (the second to last GiB), PD 0. */
    TEST_ASSERT_EQ_INT(510, paging64_index(0xFFFFFFFF80000000ull, 3));
    TEST_ASSERT_EQ_INT(0, paging64_index(0xFFFFFFFF80000000ull, 2));
    TEST_ASSERT_EQ_INT(0x100, paging64_index(0xFFFFFFFF80100000ull, 1));

    /* The direct map: physical 5 GiB is PDPT entry 5. */
    TEST_ASSERT_EQ_INT(5, paging64_index(0xFFFF800000000000ull + 5 * 0x40000000ull, 3));
}

static void test_is_kernel_address(void)
{
    TEST_ASSERT(!paging64_is_kernel_address(0));
    TEST_ASSERT(!paging64_is_kernel_address(0x7FFFFFFFFFFFull));
    TEST_ASSERT(paging64_is_kernel_address(0xFFFF800000000000ull));
    TEST_ASSERT(paging64_is_kernel_address(0xFFFFFFFFFFFFF000ull));
}

static void test_entry_make(void)
{
    TEST_ASSERT_EQ_INT(0x000B8003, paging64_entry_make(0xB8000, PAGING64_PRESENT | PAGING64_WRITABLE));

    /* Above 4 GiB (P520: xHCI at 0x43ffff10000). */
    TEST_ASSERT(paging64_entry_make(0x43ffff10000ull, PAGING64_PRESENT) == (0x43ffff10000ull | 1));

    /* A misaligned phys must not turn into flags; flag bits must not change the frame. */
    TEST_ASSERT_EQ_INT(0x000B8001, paging64_entry_make(0xB8FFE, PAGING64_PRESENT));
    TEST_ASSERT(paging64_entry_make(0xB8000, PAGING64_PRESENT | PAGING64_NO_EXECUTE) ==
                (0xB8001ull | (1ull << 63)));

    /* Bits 63:52 of phys are not part of the address. */
    TEST_ASSERT_EQ_INT(0, paging64_entry_make(0xFFF0000000000000ull, 0));
}

static void test_entry_phys(void)
{
    TEST_ASSERT_EQ_INT(0xB8000, paging64_entry_phys(0x000B8003));
    /* Accessed, dirty and the no-execute bit are not part of it. */
    TEST_ASSERT_EQ_INT(0xB8000, paging64_entry_phys(0x80000000000B8063ull));
    TEST_ASSERT(paging64_entry_phys(0x43ffff10003ull) == 0x43ffff10000ull);
}

static void test_flags_from_hal(void)
{
    uint64_t flags = 0x1234;

    TEST_ASSERT_EQ_INT(0, paging64_flags_from_hal(0, false, &flags));
    TEST_ASSERT_EQ_INT(0, flags);

    /* A kernel mapping is not executable if the CPU supports that. */
    TEST_ASSERT_EQ_INT(0, paging64_flags_from_hal(HAL_MMU_WRITABLE, true, &flags));
    TEST_ASSERT(flags == (PAGING64_WRITABLE | PAGING64_NO_EXECUTE));

    /* A user mapping stays executable ("readable = executable", docs/userland.md). */
    TEST_ASSERT_EQ_INT(0, paging64_flags_from_hal(HAL_MMU_WRITABLE | HAL_MMU_USER, true, &flags));
    TEST_ASSERT(flags == (PAGING64_WRITABLE | PAGING64_USER));

    TEST_ASSERT_EQ_INT(0, paging64_flags_from_hal(HAL_MMU_WRITABLE | HAL_MMU_UNCACHED, false, &flags));
    TEST_ASSERT(flags == (PAGING64_WRITABLE | PAGING64_WRITE_THROUGH | PAGING64_CACHE_DISABLE));

    /* Unknown bits: refused, *flags untouched. */
    flags = 0x77;
    TEST_ASSERT_EQ_INT(-1, paging64_flags_from_hal(0x8, false, &flags));
    TEST_ASSERT_EQ_INT(0x77, flags);
}

static void test_flags_to_hal(void)
{
    TEST_ASSERT_EQ_INT(0, paging64_flags_to_hal(PAGING64_PRESENT));
    TEST_ASSERT_EQ_INT(HAL_MMU_WRITABLE | HAL_MMU_USER,
                       paging64_flags_to_hal(PAGING64_PRESENT | PAGING64_WRITABLE | PAGING64_USER | PAGING64_NO_EXECUTE));
    TEST_ASSERT_EQ_INT(HAL_MMU_UNCACHED, paging64_flags_to_hal(PAGING64_PRESENT | PAGING64_CACHE_DISABLE | PAGING64_WRITE_THROUGH));
}

int main(void)
{
    TEST_RUN(test_index_levels);
    TEST_RUN(test_index_kernel_layout);
    TEST_RUN(test_is_kernel_address);
    TEST_RUN(test_entry_make);
    TEST_RUN(test_entry_phys);
    TEST_RUN(test_flags_from_hal);
    TEST_RUN(test_flags_to_hal);

    return TEST_REPORT("paging64_entry");
}
