/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include <stdint.h>

#include "kernel/memory/paging_entry.h"

static void test_index_vga_buffer(void)
{
    /* 0xB8000 = dir 0, table 0xB8 (184), offset 0. */
    TEST_ASSERT_EQ_INT(0, paging_dir_index(0xB8000));
    TEST_ASSERT_EQ_INT(184, paging_table_index(0xB8000));
}

static void test_index_kernel_start(void)
{
    /* linker.ld places the kernel at 1 MiB: dir 0, table 0x100 (256). */
    TEST_ASSERT_EQ_INT(0, paging_dir_index(0x00100000));
    TEST_ASSERT_EQ_INT(256, paging_table_index(0x00100000));
}

static void test_index_ignores_offset(void)
{
    /* Offset 0xA10 must not leak into either index. */
    TEST_ASSERT_EQ_INT(0, paging_dir_index(0x00103A10));
    TEST_ASSERT_EQ_INT(0x103, paging_table_index(0x00103A10));
}

static void test_index_4mib_boundary(void)
{
    /* Last page of the first 4 MiB, then the first page of the next. */
    TEST_ASSERT_EQ_INT(0, paging_dir_index(0x003FF000));
    TEST_ASSERT_EQ_INT(1023, paging_table_index(0x003FF000));
    TEST_ASSERT_EQ_INT(1, paging_dir_index(0x00400000));
    TEST_ASSERT_EQ_INT(0, paging_table_index(0x00400000));
}

static void test_index_extremes(void)
{
    TEST_ASSERT_EQ_INT(0, paging_dir_index(0));
    TEST_ASSERT_EQ_INT(0, paging_table_index(0));
    TEST_ASSERT_EQ_INT(768, paging_dir_index(0xC0000000));
    TEST_ASSERT_EQ_INT(1023, paging_dir_index(0xFFFFFFFF));
    TEST_ASSERT_EQ_INT(1023, paging_table_index(0xFFFFFFFF));
}

static void test_entry_make(void)
{
    /* VGA page, present and writable: 0xB8000 | 0x003. */
    TEST_ASSERT_EQ_INT(0x000B8003,
                       paging_entry_make(0xB8000, PAGING_PRESENT | PAGING_WRITABLE));
    TEST_ASSERT_EQ_INT(0x00100001, paging_entry_make(0x00100000, PAGING_PRESENT));
    TEST_ASSERT_EQ_INT(0xFFFFF007,
                       paging_entry_make(0xFFFFF000,
                                         PAGING_PRESENT | PAGING_WRITABLE | PAGING_USER));
    TEST_ASSERT_EQ_INT(0, paging_entry_make(0, 0));
}

static void test_entry_make_masks_fields(void)
{
    /* Low bits of a misaligned phys must not turn into flags. */
    TEST_ASSERT_EQ_INT(0x000B8001, paging_entry_make(0xB8FFE, PAGING_PRESENT));
    /* Flag bits above 11 must not change the frame address. */
    TEST_ASSERT_EQ_INT(0x000B8003, paging_entry_make(0xB8000, 0xFFFFF003));
}

static void test_entry_get_phys(void)
{
    TEST_ASSERT_EQ_INT(0xB8000, paging_entry_get_phys(0x000B8003));
    /* Accessed (bit 5) and dirty (bit 6) set by the CPU are ignored. */
    TEST_ASSERT_EQ_INT(0xB8000, paging_entry_get_phys(0x000B8063));
    TEST_ASSERT_EQ_INT(0xFFFFF000, paging_entry_get_phys(0xFFFFFFFF));
    TEST_ASSERT_EQ_INT(0, paging_entry_get_phys(0x00000FFF));
}

static void test_entry_is_present(void)
{
    TEST_ASSERT(paging_entry_is_present(0x000B8003));
    TEST_ASSERT(paging_entry_is_present(0x00000001));
    /* Writable but not present: the address bits do not matter. */
    TEST_ASSERT(!paging_entry_is_present(0x000B8002));
    TEST_ASSERT(!paging_entry_is_present(0));
}

static void test_entry_round_trip(void)
{
    paging_entry_t entry = paging_entry_make(0x0032A000, PAGING_PRESENT | PAGING_WRITABLE);

    TEST_ASSERT_EQ_INT(0x0032A000, paging_entry_get_phys(entry));
    TEST_ASSERT(paging_entry_is_present(entry));
}

static void test_table_virt(void)
{
    TEST_ASSERT_EQ_INT(0xFFC00000, paging_table_virt(0));
    TEST_ASSERT_EQ_INT(0xFFC01000, paging_table_virt(1));
    TEST_ASSERT_EQ_INT(0xFFFFE000, paging_table_virt(1022));
}

static void test_table_virt_recursive_is_directory(void)
{
    /* Table 1023 loops through the directory twice: it is the directory. */
    TEST_ASSERT_EQ_INT(PAGING_DIRECTORY_VIRT,
                       paging_table_virt(PAGING_RECURSIVE_INDEX));
}

static void test_table_virt_in_recursive_region(void)
{
    /* Every table address must translate through directory[1023]. */
    TEST_ASSERT_EQ_INT(PAGING_RECURSIVE_INDEX,
                       paging_dir_index(paging_table_virt(0)));
    TEST_ASSERT_EQ_INT(PAGING_RECURSIVE_INDEX,
                       paging_dir_index(paging_table_virt(1023)));
    /* ...and its table index selects directory[N], i.e. page table N. */
    TEST_ASSERT_EQ_INT(0, paging_table_index(paging_table_virt(0)));
    TEST_ASSERT_EQ_INT(768, paging_table_index(paging_table_virt(768)));
}

static void test_table_virt_vga_pte(void)
{
    /* PTE for 0xB8000: table 0, entry 184 -> 0xFFC00000 + 184 * 4. */
    uintptr_t table = paging_table_virt(paging_dir_index(0xB8000));

    TEST_ASSERT_EQ_INT(0xFFC002E0,
                       table + paging_table_index(0xB8000) * sizeof(paging_entry_t));
}

static void test_table_virt_wraps_index(void)
{
    TEST_ASSERT_EQ_INT(paging_table_virt(0), paging_table_virt(1024));
}

int main(void)
{
    TEST_RUN(test_index_vga_buffer);
    TEST_RUN(test_index_kernel_start);
    TEST_RUN(test_index_ignores_offset);
    TEST_RUN(test_index_4mib_boundary);
    TEST_RUN(test_index_extremes);
    TEST_RUN(test_entry_make);
    TEST_RUN(test_entry_make_masks_fields);
    TEST_RUN(test_entry_get_phys);
    TEST_RUN(test_entry_is_present);
    TEST_RUN(test_entry_round_trip);
    TEST_RUN(test_table_virt);
    TEST_RUN(test_table_virt_recursive_is_directory);
    TEST_RUN(test_table_virt_in_recursive_region);
    TEST_RUN(test_table_virt_vga_pte);
    TEST_RUN(test_table_virt_wraps_index);

    return TEST_REPORT("paging_entry");
}
