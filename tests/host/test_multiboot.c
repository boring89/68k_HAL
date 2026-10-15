/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* MAP_32BIT needs _GNU_SOURCE under -std=c11. */
#define _GNU_SOURCE

#include "test.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>

#include "platform/pc/boot/multiboot.h"

/*
 * multiboot_info_t stores 32-bit addresses, so the fake boot data must
 * live below 4 GiB even on a 64-bit host: map it with MAP_32BIT.
 */
#define AREA_SIZE 4096

static uint8_t *area;
static multiboot_info_t *mbi;
static uint8_t *table;

static uint32_t addr32(const void *ptr)
{
    return (uint32_t)(uintptr_t)ptr;
}

static void reset(void)
{
    memset(area, 0, AREA_SIZE);
    mbi = (multiboot_info_t *)area;
    table = area + 256;
}

/* Append an entry with the given size field; returns the next write position. */
static uint8_t *put_entry(uint8_t *pos, uint32_t size,
                          uint64_t addr, uint64_t len, uint32_t type)
{
    multiboot_mmap_entry_t entry = {size, addr, len, type};

    memcpy(pos, &entry, sizeof(entry));
    return pos + sizeof(uint32_t) + size;
}

static void boot(uint32_t flags, const uint8_t *end)
{
    mbi->flags = flags;
    mbi->mmap_addr = addr32(table);
    mbi->mmap_length = (uint32_t)(end - table);
    multiboot_init(addr32(mbi));
}

static void test_entry_layout(void)
{
    TEST_ASSERT_EQ_INT(24, sizeof(multiboot_mmap_entry_t));
    TEST_ASSERT_EQ_INT(4, offsetof(multiboot_mmap_entry_t, addr));
    TEST_ASSERT_EQ_INT(12, offsetof(multiboot_mmap_entry_t, len));
    TEST_ASSERT_EQ_INT(20, offsetof(multiboot_mmap_entry_t, type));
}

static void test_no_map_flag(void)
{
    reset();

    uint8_t *end = put_entry(table, 20, 0, 0x9FC00, 1);

    boot(0, end);

    TEST_ASSERT(multiboot_mmap_first() == NULL);
}

static void test_empty_map(void)
{
    reset();
    boot(MULTIBOOT_INFO_MEM_MAP, table);

    TEST_ASSERT(multiboot_mmap_first() == NULL);
}

static void test_walk_entries(void)
{
    reset();

    uint8_t *end = table;
    end = put_entry(end, 20, 0x0, 0x9FC00, 1);
    end = put_entry(end, 20, 0x9FC00, 0x400, 2);
    end = put_entry(end, 20, 0xFFFC0000ULL, 0x40000, 2);

    boot(MULTIBOOT_INFO_MEM_MAP, end);

    const multiboot_mmap_entry_t *e = multiboot_mmap_first();
    TEST_ASSERT(e != NULL);
    TEST_ASSERT_EQ_INT(0x9FC00, e->len);
    TEST_ASSERT_EQ_INT(1, e->type);

    e = multiboot_mmap_next(e);
    TEST_ASSERT(e != NULL);
    TEST_ASSERT_EQ_INT(0x9FC00, e->addr);

    e = multiboot_mmap_next(e);
    TEST_ASSERT(e != NULL);
    TEST_ASSERT_EQ_INT(0xFFFC0000LL, e->addr);

    TEST_ASSERT(multiboot_mmap_next(e) == NULL);
}

static void test_larger_entry_uses_size_field(void)
{
    reset();

    /* size = 28: 8 extra bytes the iterator must skip, not sizeof(). */
    uint8_t *end = table;
    end = put_entry(end, 28, 0x0, 0x1000, 1);
    end = put_entry(end, 20, 0x100000, 0x2000, 1);

    boot(MULTIBOOT_INFO_MEM_MAP, end);

    const multiboot_mmap_entry_t *e = multiboot_mmap_first();
    TEST_ASSERT(e != NULL);

    e = multiboot_mmap_next(e);
    TEST_ASSERT(e != NULL);
    TEST_ASSERT_EQ_INT(0x100000, e->addr);
    TEST_ASSERT_EQ_INT(0x2000, e->len);

    TEST_ASSERT(multiboot_mmap_next(e) == NULL);
}

static void test_truncated_entry_rejected(void)
{
    reset();

    uint8_t *end = table;
    end = put_entry(end, 20, 0x0, 0x1000, 1);
    end = put_entry(end, 20, 0x100000, 0x2000, 1);

    /* mmap_length cuts the second entry in half. */
    boot(MULTIBOOT_INFO_MEM_MAP, end - 10);

    const multiboot_mmap_entry_t *e = multiboot_mmap_first();
    TEST_ASSERT(e != NULL);
    TEST_ASSERT(multiboot_mmap_next(e) == NULL);
}

static void test_short_size_rejected(void)
{
    reset();

    /* size smaller than addr + len + type would read past the entry. */
    uint8_t *end = put_entry(table, 8, 0x0, 0x1000, 1);

    boot(MULTIBOOT_INFO_MEM_MAP, end + 16);

    TEST_ASSERT(multiboot_mmap_first() == NULL);
}

int main(void)
{
    area = mmap(NULL, AREA_SIZE, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);

    if (area == MAP_FAILED)
    {
        fprintf(stderr, "mmap(MAP_32BIT) failed\n");
        return 1;
    }

    TEST_RUN(test_entry_layout);
    TEST_RUN(test_no_map_flag);
    TEST_RUN(test_empty_map);
    TEST_RUN(test_walk_entries);
    TEST_RUN(test_larger_entry_uses_size_field);
    TEST_RUN(test_truncated_entry_rejected);
    TEST_RUN(test_short_size_rejected);

    munmap(area, AREA_SIZE);

    return TEST_REPORT("multiboot");
}
