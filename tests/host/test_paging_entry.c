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

int main(void)
{
    TEST_RUN(test_index_vga_buffer);
    TEST_RUN(test_index_kernel_start);
    TEST_RUN(test_index_ignores_offset);
    TEST_RUN(test_index_4mib_boundary);
    TEST_RUN(test_index_extremes);

    return TEST_REPORT("paging_entry");
}
