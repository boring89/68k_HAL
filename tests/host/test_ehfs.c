/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/block/block.h"
#include "kernel/fs/ehfs/ehfs.h"
#include "kernel/fs/ehfs/name.h"
#include "kernel/lib/k_crc32c.h"

#define SECTORS (32768u * 8u)       /* 32768 blocks = 128 MiB: a bitmap of one block */
#define OFFSET 2048u                /* the volume starts at sector 2048, like a partition */

static uint8_t *disk;
static ehfs_t fs;
static ehfs_check_t report;

static int mem_read(block_device_t *d, uint64_t lba, uint32_t count, void *buf)
{
    (void)d;
    memcpy(buf, disk + lba * 512, (size_t)count * 512);
    return 0;
}

static int mem_write(block_device_t *d, uint64_t lba, uint32_t count, const void *buf)
{
    (void)d;
    memcpy(disk + lba * 512, buf, (size_t)count * 512);
    return 0;
}

static const block_ops_t ops = {mem_read, mem_write};
static block_device_t dev = {"ehfs", 512, OFFSET + SECTORS, &ops, NULL};

static ehfs_format_options_t options(void)
{
    ehfs_format_options_t o;

    memset(&o, 0, sizeof o);
    o.name = "Data";
    for (int i = 0; i < 16; i++)
        o.uuid[i] = (uint8_t)(i * 17 + 3);
    o.now = 1760000000ull * 1000000000ull;
    return o;
}

static void fresh(void)
{
    ehfs_format_options_t o = options();

    memset(disk, 0xA5, (size_t)(OFFSET + SECTORS) * 512);
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_format(&fs, &dev, OFFSET, SECTORS, &o));
}

static void test_crc32c(void)
{
    uint8_t zeros[32] = {0};

    TEST_ASSERT_EQ_INT(0xE3069283u, k_crc32c(0, "123456789", 9));
    TEST_ASSERT_EQ_INT(0x8A9136AAu, k_crc32c(0, zeros, sizeof zeros));      /* RFC 3720 B.4 */
    TEST_ASSERT_EQ_INT(k_crc32c(0, "123456789", 9), k_crc32c(k_crc32c(0, "1234", 4), "56789", 5));
}

static bool same_key(const char *a, const char *b)
{
    uint8_t ka[EHFS_KEY_MAX], kb[EHFS_KEY_MAX];
    int la = ehfs_name_key((const uint8_t *)a, (uint32_t)strlen(a), ka, sizeof ka);
    int lb = ehfs_name_key((const uint8_t *)b, (uint32_t)strlen(b), kb, sizeof kb);

    return la > 0 && la == lb && memcmp(ka, kb, (size_t)la) == 0;
}

static void test_names(void)
{
    uint8_t key[EHFS_KEY_MAX];
    char long_name[300];

    TEST_ASSERT(same_key("readme.txt", "README.TXT"));
    TEST_ASSERT(same_key("Caf\xC3\xA9", "Cafe\xCC\x81"));               /* NFC: e + U+0301 = U+00E9 */
    TEST_ASSERT(same_key("CAF\xC3\x89", "caf\xC3\xA9"));                /* folding */
    TEST_ASSERT(same_key("\xEA\xB0\x80", "\xE1\x84\x80\xE1\x85\xA1"));  /* Hangul: GA = G + A */
    TEST_ASSERT(same_key("\xCE\xA3\xCE\x8A", "\xCF\x83\xCE\xAF"));      /* Greek capital sigma + iota tonos */
    TEST_ASSERT(!same_key("a", "b"));
    TEST_ASSERT(!same_key("\xC3\x9F", "ss"));                           /* simple folding keeps sharp s */

    /* The comparison form of an ASCII name is the name in lower case. */
    TEST_ASSERT_EQ_INT(5, ehfs_name_key((const uint8_t *)"HeLLo", 5, key, sizeof key));
    TEST_ASSERT(memcmp(key, "hello", 5) == 0);

    /* Not names: empty, '/', NUL, broken UTF-8, overlong, surrogate, too long. */
    TEST_ASSERT(!ehfs_name_valid((const uint8_t *)"", 0));
    TEST_ASSERT(!ehfs_name_valid((const uint8_t *)"a/b", 3));
    TEST_ASSERT(!ehfs_name_valid((const uint8_t *)"a\0b", 3));
    TEST_ASSERT(!ehfs_name_valid((const uint8_t *)"\xC3", 1));
    TEST_ASSERT(!ehfs_name_valid((const uint8_t *)"\xC0\xAF", 2));
    TEST_ASSERT(!ehfs_name_valid((const uint8_t *)"\xED\xA0\x80", 3));
    TEST_ASSERT(!ehfs_name_valid((const uint8_t *)"\xF4\x90\x80\x80", 4));
    memset(long_name, 'x', sizeof long_name);
    TEST_ASSERT(ehfs_name_valid((const uint8_t *)long_name, 255));
    TEST_ASSERT(!ehfs_name_valid((const uint8_t *)long_name, 256));
    TEST_ASSERT_EQ_INT(-1, ehfs_name_key((const uint8_t *)long_name, 255, key, 10));
}

static void test_format_and_mount(void)
{
    fresh();
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_mount(&fs, &dev, OFFSET, false));
    TEST_ASSERT_EQ_INT(32768, fs.blocks);
    TEST_ASSERT_EQ_INT(EHFS_STATE_MOUNTED, ehfs_sb_get32(&fs, SB_STATE));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_check(&fs, &report));
    if (report.errors)
        printf("    fsck: %s\n", report.first);
    TEST_ASSERT_EQ_INT(0, report.errors);
    TEST_ASSERT(ehfs_sb_get64(&fs, SB_FREE_BLOCKS) > 30000);
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_unmount(&fs));

    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_mount(&fs, &dev, OFFSET, true));
    TEST_ASSERT_EQ_INT(EHFS_STATE_CLEAN, ehfs_sb_get32(&fs, SB_STATE));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_check(&fs, &report));
    TEST_ASSERT_EQ_INT(EHFS_EROFS, ehfs_begin(&fs));

    /* Too small, a bad name, a read-only device. */
    ehfs_format_options_t o = options();

    TEST_ASSERT_EQ_INT(EHFS_EINVAL, ehfs_format(&fs, &dev, OFFSET, 1000 * 8, &o));
    o.name = "a/b";
    TEST_ASSERT_EQ_INT(EHFS_ENAME, ehfs_format(&fs, &dev, OFFSET, SECTORS, &o));
}

static void test_alloc_free(void)
{
    uint64_t start, count, start2, count2;

    fresh();
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_mount(&fs, &dev, OFFSET, false));

    uint64_t free0 = ehfs_sb_get64(&fs, SB_FREE_BLOCKS);

    TEST_ASSERT_EQ_INT(EHFS_EINVAL, ehfs_alloc(&fs, 0, 10, &start, &count));   /* outside a transaction */
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_begin(&fs));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_alloc(&fs, 0, 100, &start, &count));
    TEST_ASSERT_EQ_INT(100, count);
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_alloc(&fs, 0, 50, &start2, &count2));
    TEST_ASSERT_EQ_INT(start + 100, start2);                                    /* the next free run */
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_commit(&fs));
    TEST_ASSERT_EQ_INT(free0 - 150, ehfs_sb_get64(&fs, SB_FREE_BLOCKS));

    /* Nothing owns them: fsck says so. */
    TEST_ASSERT_EQ_INT(EHFS_ECORRUPT, ehfs_check(&fs, &report));
    TEST_ASSERT(strstr(report.first, "nothing owns it") != NULL);

    /* Freed again: consistent; a second free of the same blocks is refused. */
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_begin(&fs));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_free(&fs, start, 150));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_commit(&fs));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_check(&fs, &report));
    TEST_ASSERT_EQ_INT(free0, ehfs_sb_get64(&fs, SB_FREE_BLOCKS));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_begin(&fs));
    TEST_ASSERT_EQ_INT(EHFS_ECORRUPT, ehfs_free(&fs, start, 1));
    ehfs_abort(&fs);
    TEST_ASSERT_EQ_INT(free0, ehfs_sb_get64(&fs, SB_FREE_BLOCKS));

    /* A run that wraps round to the start of the volume; all space taken. */
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_begin(&fs));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_alloc(&fs, fs.blocks - 2, 10, &start, &count));
    TEST_ASSERT(start < fs.blocks - 1);
    uint64_t total = count;

    while (ehfs_alloc(&fs, 0, 255, &start, &count) == EHFS_OK)
        total += count;
    TEST_ASSERT_EQ_INT(free0, total);
    TEST_ASSERT_EQ_INT(0, ehfs_sb_get64(&fs, SB_FREE_BLOCKS));
    ehfs_abort(&fs);

    /* Transactions are limited. */
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_begin(&fs));
    memset(fs.block, 0, EHFS_BLOCK_SIZE);
    int r = EHFS_OK;

    for (uint32_t i = 0; i <= EHFS_TXN_BLOCKS && r == EHFS_OK; i++)
        r = ehfs_write_meta(&fs, 20000 + i, fs.block);
    TEST_ASSERT_EQ_INT(EHFS_ETXN, r);
    ehfs_abort(&fs);
}

/* Power fails after k block writes of a commit: the next mount sees all of it or nothing. */
static void test_crash(void)
{
    int both[2] = {0, 0};

    for (int k = 0; k < 20; k++)
    {
        uint64_t start, count;

        fresh();
        TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_mount(&fs, &dev, OFFSET, false));

        uint64_t free0 = ehfs_sb_get64(&fs, SB_FREE_BLOCKS);

        TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_begin(&fs));
        TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_alloc(&fs, 0, 40, &start, &count));
        ehfs_sb_set64(&fs, SB_FILE_COUNT, 123);
        fs.crash_after = k;
        int r = ehfs_commit(&fs);

        /* Remount: read-only first (the journal is read through), then for writing (written home). */
        for (int pass = 0; pass < 2; pass++)
        {
            bool done;

            TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_mount(&fs, &dev, OFFSET, pass == 0));
            done = ehfs_sb_get64(&fs, SB_FILE_COUNT) == 123;
            TEST_ASSERT_EQ_INT(done ? free0 - 40 : free0, ehfs_sb_get64(&fs, SB_FREE_BLOCKS));

            bool used;

            TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_block_used(&fs, start, &used));
            TEST_ASSERT(used == done);
            TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_block_used(&fs, start + 39, &used));
            TEST_ASSERT(used == done);
            if (r == EHFS_OK)
                TEST_ASSERT(done);
            both[done]++;
        }
        /* After the writing mount nothing is left in the journal. */
        TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_unmount(&fs));
    }
    TEST_ASSERT(both[0] > 0 && both[1] > 0);    /* crashes before and after the commit block were seen */
}

static void test_damage(void)
{
    fresh();

    /* A flipped bit in the superblock: not mounted. */
    disk[(size_t)OFFSET * 512 + 1024 + 100] ^= 1;
    TEST_ASSERT_EQ_INT(EHFS_ECORRUPT, ehfs_mount(&fs, &dev, OFFSET, true));
    disk[(size_t)OFFSET * 512 + 1024 + 100] ^= 1;

    /* An unknown incompatible feature: not mounted. */
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_mount(&fs, &dev, OFFSET, false));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_begin(&fs));
    ehfs_sb_set32(&fs, SB_INCOMPAT, 1);
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_commit(&fs));
    TEST_ASSERT_EQ_INT(EHFS_EUNSUPPORTED, ehfs_mount(&fs, &dev, OFFSET, true));

    /* A bit set in the bitmap that nobody owns; a damaged header node. */
    fresh();
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_mount(&fs, &dev, OFFSET, true));

    uint64_t bitmap = ehfs_le64(ehfs_sb_fork(&fs, EHFS_FORK_BITMAP) + FORK_EXTENT);
    uint64_t catalog = ehfs_le64(ehfs_sb_fork(&fs, EHFS_FORK_CATALOG) + FORK_EXTENT);

    disk[(size_t)(OFFSET + bitmap * 8) * 512 + 3000] |= 1;
    TEST_ASSERT_EQ_INT(EHFS_ECORRUPT, ehfs_check(&fs, &report));
    TEST_ASSERT_EQ_INT(1, report.errors);
    disk[(size_t)(OFFSET + bitmap * 8) * 512 + 3000] &= (uint8_t)~1;
    disk[(size_t)(OFFSET + catalog * 8) * 512 + 200] ^= 0x40;
    TEST_ASSERT_EQ_INT(EHFS_ECORRUPT, ehfs_check(&fs, &report));
    TEST_ASSERT(strstr(report.first, "header node") != NULL);
}

int main(void)
{
    disk = malloc((size_t)(OFFSET + SECTORS) * 512);
    if (disk == NULL)
        return 1;
    TEST_RUN(test_crc32c);
    TEST_RUN(test_names);
    TEST_RUN(test_format_and_mount);
    TEST_RUN(test_alloc_free);
    TEST_RUN(test_crash);
    TEST_RUN(test_damage);
    free(disk);

    return TEST_REPORT("ehfs");
}
