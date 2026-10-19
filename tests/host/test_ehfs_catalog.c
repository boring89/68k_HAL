/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/block/block.h"
#include "kernel/fs/ehfs/catalog.h"
#include "kernel/fs/ehfs/ehfs.h"

#define SECTORS (16384u * 8u)       /* 64 MiB */

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
static block_device_t dev = {"ehfs", 512, SECTORS, &ops, NULL};

#define S(x) (const uint8_t *)(x), (uint32_t)strlen(x)

static void fresh(void)
{
    ehfs_format_options_t o;

    memset(&o, 0, sizeof o);
    o.name = "Macintosh HD";
    o.now = 1000;
    memset(disk, 0, (size_t)SECTORS * 512);
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_format(&fs, &dev, 0, SECTORS, &o));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_mount(&fs, &dev, 0, false));
}

static void fsck(void)
{
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_check(&fs, &report));
    if (report.errors)
        printf("    fsck: %s\n", report.first);
    TEST_ASSERT_EQ_INT(0, report.errors);
}

static int create(uint64_t parent, const char *name, bool dir, ehfs_entry_t *e)
{
    int r = ehfs_begin(&fs);

    if (r == EHFS_OK)
        r = ehfs_cat_create(&fs, parent, (const uint8_t *)name, (uint32_t)strlen(name), dir, 2000, e);
    if (r == EHFS_OK)
        r = ehfs_commit(&fs);
    else
        ehfs_abort(&fs);
    return r;
}

typedef struct
{
    char names[16][64];
    int count;
} list_t;

static int collect(void *ctx, const ehfs_entry_t *e)
{
    list_t *l = ctx;

    if (l->count < 16)
    {
        memcpy(l->names[l->count], e->name, e->name_length);
        l->names[l->count][e->name_length] = '\0';
    }
    l->count++;
    return 0;
}

static void test_root(void)
{
    ehfs_entry_t root;

    fresh();
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_cat_get(&fs, EHFS_CNID_ROOT, &root));
    TEST_ASSERT(root.is_dir);
    TEST_ASSERT_EQ_INT(EHFS_CNID_ROOT_PARENT, root.parent);
    TEST_ASSERT_EQ_INT(0, root.valence);
    TEST_ASSERT_EQ_INT(12, root.name_length);
    TEST_ASSERT(memcmp(root.name, "Macintosh HD", 12) == 0);
    TEST_ASSERT_EQ_INT(1000, root.times[0]);
    fsck();
}

static void test_tree(void)
{
    ehfs_entry_t docs, file, deep, e;
    list_t l;

    fresh();
    TEST_ASSERT_EQ_INT(EHFS_OK, create(EHFS_CNID_ROOT, "Documents", true, &docs));
    TEST_ASSERT_EQ_INT(EHFS_CNID_FIRST, docs.cnid);
    TEST_ASSERT_EQ_INT(EHFS_OK, create(docs.cnid, "Caf\xC3\xA9 Menu.txt", false, &file));
    TEST_ASSERT_EQ_INT(EHFS_OK, create(docs.cnid, "b", false, NULL));
    TEST_ASSERT_EQ_INT(EHFS_OK, create(docs.cnid, "A", false, NULL));
    TEST_ASSERT_EQ_INT(EHFS_OK, create(docs.cnid, "Deep", true, &deep));
    TEST_ASSERT_EQ_INT(EHFS_OK, create(deep.cnid, "Deeper", true, NULL));
    fsck();

    /* The same name in other spellings (case, NFD) is the same name. */
    TEST_ASSERT_EQ_INT(EHFS_EEXIST, create(docs.cnid, "CAFE\xCC\x81 menu.TXT", false, NULL));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_cat_lookup(&fs, docs.cnid, S("cafe\xCC\x81 MENU.txt"), &e));
    TEST_ASSERT_EQ_INT(file.cnid, e.cnid);
    TEST_ASSERT(memcmp(e.name, "Caf\xC3\xA9 Menu.txt", e.name_length) == 0);   /* kept as given */
    TEST_ASSERT_EQ_INT(EHFS_ENOENT, ehfs_cat_lookup(&fs, docs.cnid, S("none"), &e));
    TEST_ASSERT_EQ_INT(EHFS_ENAME, ehfs_cat_lookup(&fs, docs.cnid, S("a/b"), &e));

    /* By CNID; the folder counts; listing in the order of the comparison forms. */
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_cat_get(&fs, file.cnid, &e));
    TEST_ASSERT_EQ_INT(docs.cnid, e.parent);
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_cat_get(&fs, docs.cnid, &e));
    TEST_ASSERT_EQ_INT(4, e.valence);
    TEST_ASSERT_EQ_INT(2000, e.times[1]);
    memset(&l, 0, sizeof l);
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_cat_list(&fs, docs.cnid, collect, &l));
    TEST_ASSERT_EQ_INT(4, l.count);
    TEST_ASSERT_EQ_STR("A", l.names[0]);
    TEST_ASSERT_EQ_STR("b", l.names[1]);
    TEST_ASSERT_EQ_STR("Caf\xC3\xA9 Menu.txt", l.names[2]);
    TEST_ASSERT_EQ_STR("Deep", l.names[3]);
    TEST_ASSERT_EQ_INT(4, ehfs_sb_get64(&fs, SB_FILE_COUNT) + ehfs_sb_get64(&fs, SB_FOLDER_COUNT) - 2);
    TEST_ASSERT_EQ_INT(3, ehfs_sb_get64(&fs, SB_FILE_COUNT));
    TEST_ASSERT_EQ_INT(3, ehfs_sb_get64(&fs, SB_FOLDER_COUNT));

    /* Not into a file; not a folder into itself or below itself. */
    TEST_ASSERT_EQ_INT(EHFS_ENOTDIR, create(file.cnid, "x", false, NULL));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_begin(&fs));
    TEST_ASSERT_EQ_INT(EHFS_EINVAL, ehfs_cat_move(&fs, docs.cnid, S("Deep"), deep.cnid, S("Deep"), 3000));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_cat_lookup(&fs, deep.cnid, S("Deeper"), &e));
    TEST_ASSERT_EQ_INT(EHFS_EINVAL, ehfs_cat_move(&fs, EHFS_CNID_ROOT, S("Documents"), e.cnid, S("D"), 3000));
    TEST_ASSERT_EQ_INT(EHFS_EEXIST, ehfs_cat_move(&fs, docs.cnid, S("A"), docs.cnid, S("B"), 3000));
    ehfs_abort(&fs);

    /* Rename (keeps the CNID), a new spelling of the same name, a move to the root. */
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_begin(&fs));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_cat_move(&fs, docs.cnid, S("Caf\xC3\xA9 Menu.txt"), docs.cnid, S("Menu"), 3000));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_cat_move(&fs, docs.cnid, S("b"), docs.cnid, S("B"), 3000));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_cat_move(&fs, docs.cnid, S("Deep"), EHFS_CNID_ROOT, S("Moved"), 3000));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_commit(&fs));
    fsck();
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_cat_get(&fs, file.cnid, &e));
    TEST_ASSERT(e.name_length == 4 && memcmp(e.name, "Menu", 4) == 0);
    TEST_ASSERT_EQ_INT(3000, e.times[2]);
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_cat_lookup(&fs, docs.cnid, S("b"), &e));
    TEST_ASSERT(e.name[0] == 'B');
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_cat_get(&fs, deep.cnid, &e));
    TEST_ASSERT_EQ_INT(EHFS_CNID_ROOT, e.parent);
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_cat_get(&fs, docs.cnid, &e));
    TEST_ASSERT_EQ_INT(3, e.valence);
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_cat_get(&fs, EHFS_CNID_ROOT, &e));
    TEST_ASSERT_EQ_INT(2, e.valence);

    /* Remove: a full folder no, an empty one and files yes. */
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_begin(&fs));
    TEST_ASSERT_EQ_INT(EHFS_ENOTEMPTY, ehfs_cat_remove(&fs, EHFS_CNID_ROOT, S("Moved"), 4000));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_cat_remove(&fs, deep.cnid, S("deeper"), 4000));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_cat_remove(&fs, EHFS_CNID_ROOT, S("Moved"), 4000));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_cat_remove(&fs, docs.cnid, S("menu"), 4000));
    TEST_ASSERT_EQ_INT(EHFS_ENOENT, ehfs_cat_remove(&fs, docs.cnid, S("menu"), 4000));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_commit(&fs));
    fsck();
    TEST_ASSERT_EQ_INT(EHFS_ENOENT, ehfs_cat_get(&fs, file.cnid, &e));
    TEST_ASSERT_EQ_INT(2, ehfs_sb_get64(&fs, SB_FILE_COUNT));
    TEST_ASSERT_EQ_INT(1, ehfs_sb_get64(&fs, SB_FOLDER_COUNT));
}

static void test_many(void)
{
    char name[64];
    ehfs_entry_t folder, e;
    list_t l;

    fresh();
    TEST_ASSERT_EQ_INT(EHFS_OK, create(EHFS_CNID_ROOT, "Many", true, &folder));
    for (int i = 0; i < 2000; i++)
    {
        snprintf(name, sizeof name, "file %05d with a longer name", (i * 7919) % 2000);
        TEST_ASSERT_EQ_INT(EHFS_OK, create(folder.cnid, name, i % 10 == 0, NULL));
    }
    fsck();
    memset(&l, 0, sizeof l);
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_cat_list(&fs, folder.cnid, collect, &l));
    TEST_ASSERT_EQ_INT(2000, l.count);
    TEST_ASSERT_EQ_STR("file 00000 with a longer name", l.names[0]);
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_cat_lookup(&fs, folder.cnid, S("FILE 01234 WITH A LONGER NAME"), &e));

    for (int i = 0; i < 2000; i += 3)
    {
        snprintf(name, sizeof name, "file %05d with a longer name", i);
        TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_begin(&fs));
        TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_cat_remove(&fs, folder.cnid, S(name), 5000));
        TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_commit(&fs));
    }
    fsck();
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_cat_get(&fs, folder.cnid, &e));
    TEST_ASSERT_EQ_INT(2000 - 667, e.valence);
}

/* A crash anywhere in the commit of a create: the item is there with all its parts, or not at all. */
static void test_crash(void)
{
    int seen[2] = {0, 0};

    for (int k = 0; k < 40; k++)
    {
        ehfs_entry_t e;

        fresh();
        TEST_ASSERT_EQ_INT(EHFS_OK, create(EHFS_CNID_ROOT, "Folder", true, NULL));
        TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_begin(&fs));
        TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_cat_create(&fs, EHFS_CNID_ROOT, S("New"), false, 5, &e));
        fs.crash_after = k;
        ehfs_commit(&fs);
        TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_mount(&fs, &dev, 0, false));
        fsck();

        bool there = ehfs_cat_lookup(&fs, EHFS_CNID_ROOT, S("New"), &e) == EHFS_OK;

        TEST_ASSERT_EQ_INT(there ? 1 : 0, ehfs_sb_get64(&fs, SB_FILE_COUNT));
        seen[there]++;
    }
    TEST_ASSERT(seen[0] > 0 && seen[1] > 0);
}

int main(void)
{
    disk = malloc((size_t)SECTORS * 512);
    if (disk == NULL)
        return 1;
    TEST_RUN(test_root);
    TEST_RUN(test_tree);
    TEST_RUN(test_many);
    TEST_RUN(test_crash);
    free(disk);

    return TEST_REPORT("ehfs_catalog");
}
