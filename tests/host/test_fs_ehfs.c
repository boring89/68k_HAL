/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/block/block.h"
#include "kernel/fs/ehfs/catalog.h"
#include "kernel/fs/ehfs/ehfs.h"
#include "kernel/fs/ehfs/file.h"
#include "kernel/mac/fs_ehfs.h"

#define SECTORS (4096u * 8u)        /* 16 MiB */
#define MAC_1970 2082844800u

static uint8_t *disk;
static ehfs_t fs;
static ehfs_check_t report;
static mac_fs_ehfs_t volume;
static const mac_fs_ops_t *ops;
static uint64_t fake_now = 1700000000ull * 1000000000ull;

static int mem_read(block_device_t *d, uint64_t lba, uint32_t count, void *b)
{
    (void)d;
    memcpy(b, disk + lba * 512, (size_t)count * 512);
    return 0;
}

static int mem_write(block_device_t *d, uint64_t lba, uint32_t count, const void *b)
{
    (void)d;
    memcpy(disk + lba * 512, b, (size_t)count * 512);
    return 0;
}

static const block_ops_t block_ops = {mem_read, mem_write};
static block_device_t dev = {"ehfs0", 512, SECTORS, &block_ops, NULL};

static uint64_t clock_fn(void)
{
    return fake_now;
}

static void setup(const char *name)
{
    ehfs_format_options_t o;

    if (disk == NULL)
        disk = malloc((size_t)SECTORS * 512);
    memset(disk, 0, (size_t)SECTORS * 512);
    memset(&o, 0, sizeof o);
    o.name = name;
    o.now = fake_now;
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_format(&fs, &dev, 0, SECTORS, &o));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_mount(&fs, &dev, 0, false));
    mac_fs_ehfs_init(&volume, &fs, clock_fn);
    ops = &volume.ops;
}

static void fsck(void)
{
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_check(&fs, &report));
    if (report.errors != 0)
        printf("    fsck: %s\n", report.first);
}

/* A Pascal string as a C string (for the comparisons). */
static const char *c_name(const uint8_t *p)
{
    static char out[40];

    memcpy(out, p + 1, p[0]);
    out[p[0]] = '\0';
    return out;
}

static int lookup(uint32_t parent, const char *name, mac_fs_item_t *it)
{
    return ops->lookup(ops->ctx, parent, (const uint8_t *)name, (uint32_t)strlen(name), it);
}

static int create(uint32_t parent, const char *name, bool is_dir, mac_fs_item_t *it)
{
    return ops->create(ops->ctx, parent, (const uint8_t *)name, (uint32_t)strlen(name), is_dir, it);
}

static void test_items(void)
{
    mac_fs_item_t root, it, f;
    mac_fs_volume_t v;

    setup("My Data");
    TEST_ASSERT_EQ_STR("My Data", volume.name);
    TEST_ASSERT_EQ_INT(0, ops->get(ops->ctx, MAC_FS_ROOT_ID, &root));
    TEST_ASSERT(root.is_dir);
    TEST_ASSERT_EQ_STR("My Data", c_name(root.name));
    TEST_ASSERT_EQ_INT((int)(1700000000u + MAC_1970), (int)root.created);
    TEST_ASSERT_EQ_INT(-1, ops->get(ops->ctx, 999, &it));

    /* Made by the Mac: a folder and a file in it. */
    TEST_ASSERT_EQ_INT(0, create(MAC_FS_ROOT_ID, "Letters", true, &it));
    TEST_ASSERT(it.is_dir && it.id >= MAC_FS_FIRST_ID && it.parent == MAC_FS_ROOT_ID);
    TEST_ASSERT_EQ_INT(0, create(it.id, "Note 1/2", false, &f));    /* '/' is ':' on the HAL's side */
    TEST_ASSERT_EQ_STR("Note 1/2", c_name(f.name));
    TEST_ASSERT_EQ_INT(MAC_FS_DUP_FN_ERR, create(it.id, "NOTE 1/2", false, &f));
    TEST_ASSERT_EQ_INT(MAC_FS_BD_NAM_ERR, create(it.id, "caf\x8E", false, &f));   /* Mac Roman, not yet */
    TEST_ASSERT_EQ_INT(MAC_FS_DIR_NF_ERR, create(4242, "x", false, &f));

    ehfs_entry_t e;

    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_cat_lookup(&fs, it.id, (const uint8_t *)"Note 1:2", 8, &e));
    TEST_ASSERT_EQ_INT(0, lookup(it.id, "note 1/2", &f));             /* without regard to case */
    TEST_ASSERT_EQ_INT((int)e.cnid, (int)f.id);
    TEST_ASSERT_EQ_INT(0, ops->get(ops->ctx, it.id, &it));
    TEST_ASSERT_EQ_INT(1, (int)it.valence);
    TEST_ASSERT_EQ_INT(0, ops->child(ops->ctx, it.id, 1, &f));
    TEST_ASSERT_EQ_STR("Note 1/2", c_name(f.name));
    TEST_ASSERT_EQ_INT(-1, ops->child(ops->ctx, it.id, 2, &f));
    TEST_ASSERT_EQ_INT(-1, lookup(it.id, "nothing", &f));

    TEST_ASSERT_EQ_INT(0, ops->volume(ops->ctx, &v));
    TEST_ASSERT_EQ_INT(1, (int)v.files);
    TEST_ASSERT_EQ_INT(1, (int)v.folders);
    TEST_ASSERT(v.total_bytes == 16u * 1024 * 1024 && v.free_bytes < v.total_bytes && !v.read_only);
    fsck();
}

static void test_names(void)
{
    mac_fs_item_t it;
    ehfs_entry_t e;
    char want[40];

    setup("Vol");
    /* Made on the HAL's side: names the Mac sees changed. */
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_create(&fs, EHFS_CNID_ROOT, (const uint8_t *)"caf\xC3\xA9.txt", 9, false, 0, &e));
    TEST_ASSERT_EQ_INT(0, ops->get(ops->ctx, (uint32_t)e.cnid, &it));
    snprintf(want, sizeof want, "caf_.txt#%u", (unsigned)e.cnid);
    TEST_ASSERT_EQ_STR(want, c_name(it.name));
    TEST_ASSERT_EQ_INT(0, lookup(MAC_FS_ROOT_ID, want, &it));
    TEST_ASSERT_EQ_INT((int)e.cnid, (int)it.id);
    want[0] = 'C';                                                 /* case */
    TEST_ASSERT_EQ_INT(0, lookup(MAC_FS_ROOT_ID, want, &it));
    TEST_ASSERT_EQ_INT(-1, lookup(MAC_FS_ROOT_ID, "caf_.txt#9999", &it));

    const char *longname = "A very long file name from Linux that the Mac cannot show.txt";

    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_create(&fs, EHFS_CNID_ROOT, (const uint8_t *)longname,
                                            (uint32_t)strlen(longname), false, 0, &e));
    TEST_ASSERT_EQ_INT(0, ops->get(ops->ctx, (uint32_t)e.cnid, &it));
    TEST_ASSERT_EQ_INT(31, it.name[0]);
    snprintf(want, sizeof want, "%s", c_name(it.name));
    TEST_ASSERT_EQ_INT(0, lookup(MAC_FS_ROOT_ID, want, &it));
    TEST_ASSERT_EQ_INT((int)e.cnid, (int)it.id);

    /* "#<ID>" of an item in another folder is not this folder's. */
    mac_fs_item_t dir;

    TEST_ASSERT_EQ_INT(0, create(MAC_FS_ROOT_ID, "Sub", true, &dir));
    TEST_ASSERT_EQ_INT(-1, lookup(dir.id, want, &it));

    /* A volume name that is not ASCII. */
    setup("Daten \xC3\xBC" "ber");
    TEST_ASSERT_EQ_STR("Daten _ber", volume.name);
    fsck();
}

static void test_contents(void)
{
    mac_fs_item_t f, it;
    uint8_t buffer[9000];

    setup("Vol");
    TEST_ASSERT_EQ_INT(0, create(MAC_FS_ROOT_ID, "doc", false, &f));
    TEST_ASSERT_EQ_INT(5, ops->write(ops->ctx, f.id, false, 0, "hello", 5));
    TEST_ASSERT_EQ_INT(4, ops->write(ops->ctx, f.id, true, 0, "rsrc", 4));
    TEST_ASSERT_EQ_INT(0, ops->get(ops->ctx, f.id, &it));
    TEST_ASSERT_EQ_INT(5, (int)it.data_length);
    TEST_ASSERT_EQ_INT(4, (int)it.rsrc_length);
    TEST_ASSERT_EQ_INT(5, ops->read(ops->ctx, f.id, false, 0, buffer, sizeof buffer));
    TEST_ASSERT(memcmp(buffer, "hello", 5) == 0);
    TEST_ASSERT_EQ_INT(4, ops->read(ops->ctx, f.id, true, 0, buffer, sizeof buffer));
    TEST_ASSERT(memcmp(buffer, "rsrc", 4) == 0);

    for (uint32_t i = 0; i < sizeof buffer; i++)
        buffer[i] = (uint8_t)(i * 7);
    TEST_ASSERT_EQ_INT(9000, ops->write(ops->ctx, f.id, false, 3, buffer, 9000));
    TEST_ASSERT_EQ_INT(0, ops->set_length(ops->ctx, f.id, false, 6000));
    TEST_ASSERT_EQ_INT(0, ops->get(ops->ctx, f.id, &it));
    TEST_ASSERT_EQ_INT(6000, (int)it.data_length);

    uint8_t back[6000];

    TEST_ASSERT_EQ_INT(6000, ops->read(ops->ctx, f.id, false, 0, back, sizeof back));
    TEST_ASSERT(memcmp(back, "hel", 3) == 0 && memcmp(back + 3, buffer, 5997) == 0);
    TEST_ASSERT_EQ_INT(0, ops->set_length(ops->ctx, f.id, true, 0));
    TEST_ASSERT_EQ_INT(0, ops->get(ops->ctx, f.id, &it));
    TEST_ASSERT_EQ_INT(0, (int)it.rsrc_length);

    /* Finder information, dates, the lock. */
    memcpy(it.finfo, "TEXTttxt", 8);
    it.finfo[8] = 0x01;
    memset(it.fxinfo, 0x22, 16);
    it.created = MAC_1970 + 1000;
    it.modified = MAC_1970 + 2000;
    it.backed_up = MAC_1970 + 3000;
    it.locked = true;
    TEST_ASSERT_EQ_INT(0, ops->set_info(ops->ctx, &it));
    memset(&it, 0, sizeof it);
    TEST_ASSERT_EQ_INT(0, ops->get(ops->ctx, f.id, &it));
    TEST_ASSERT(memcmp(it.finfo, "TEXTttxt\x01", 9) == 0 && it.fxinfo[15] == 0x22);
    TEST_ASSERT_EQ_INT((int)(MAC_1970 + 1000), (int)it.created);
    TEST_ASSERT_EQ_INT((int)(MAC_1970 + 2000), (int)it.modified);
    TEST_ASSERT_EQ_INT((int)(MAC_1970 + 3000), (int)it.backed_up);
    TEST_ASSERT(it.locked);
    TEST_ASSERT_EQ_INT(MAC_FS_FLCKD_ERR, ops->remove(ops->ctx, f.id));
    it.locked = false;
    TEST_ASSERT_EQ_INT(0, ops->set_info(ops->ctx, &it));
    TEST_ASSERT_EQ_INT(0, ops->remove(ops->ctx, f.id));
    TEST_ASSERT_EQ_INT(-1, ops->get(ops->ctx, f.id, &it));
    fsck();
}

static void test_moves(void)
{
    mac_fs_item_t a, b, f, it;

    setup("Vol");
    TEST_ASSERT_EQ_INT(0, create(MAC_FS_ROOT_ID, "A", true, &a));
    TEST_ASSERT_EQ_INT(0, create(a.id, "B", true, &b));
    TEST_ASSERT_EQ_INT(0, create(MAC_FS_ROOT_ID, "file", false, &f));

    /* Renamed (the ID stays), moved, both refused where they cannot be. */
    TEST_ASSERT_EQ_INT(0, ops->move(ops->ctx, f.id, MAC_FS_ROOT_ID, (const uint8_t *)"File 2", 6));
    TEST_ASSERT_EQ_INT(0, lookup(MAC_FS_ROOT_ID, "file 2", &it));
    TEST_ASSERT_EQ_INT((int)f.id, (int)it.id);
    TEST_ASSERT_EQ_INT(0, ops->move(ops->ctx, f.id, b.id, NULL, 0));
    TEST_ASSERT_EQ_INT(0, lookup(b.id, "File 2", &it));
    TEST_ASSERT_EQ_INT(MAC_FS_BAD_MOV_ERR, ops->move(ops->ctx, a.id, b.id, NULL, 0));
    TEST_ASSERT_EQ_INT(MAC_FS_DIR_NF_ERR, ops->move(ops->ctx, a.id, f.id, NULL, 0));
    TEST_ASSERT_EQ_INT(0, create(MAC_FS_ROOT_ID, "B", false, &it));
    TEST_ASSERT_EQ_INT(MAC_FS_DUP_FN_ERR, ops->move(ops->ctx, b.id, MAC_FS_ROOT_ID, NULL, 0));
    TEST_ASSERT_EQ_INT(MAC_FS_BD_NAM_ERR, ops->move(ops->ctx, f.id, b.id, (const uint8_t *)"", 0));

    /* A folder with something in it stays. */
    TEST_ASSERT_EQ_INT(MAC_FS_FBSY_ERR, ops->remove(ops->ctx, a.id));
    TEST_ASSERT_EQ_INT(0, ops->remove(ops->ctx, f.id));
    TEST_ASSERT_EQ_INT(0, ops->remove(ops->ctx, b.id));
    TEST_ASSERT_EQ_INT(0, ops->remove(ops->ctx, a.id));
    TEST_ASSERT_EQ_INT(MAC_FS_FNF_ERR, ops->remove(ops->ctx, MAC_FS_ROOT_ID));
    fsck();
}

static void test_read_only(void)
{
    mac_fs_volume_t v;

    setup("Vol");
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_unmount(&fs));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_mount(&fs, &dev, 0, true));
    mac_fs_ehfs_init(&volume, &fs, clock_fn);
    TEST_ASSERT(ops->create == NULL && ops->write == NULL && ops->remove == NULL);
    TEST_ASSERT_EQ_INT(0, ops->volume(ops->ctx, &v));
    TEST_ASSERT(v.read_only);
}

int main(void)
{
    TEST_RUN(test_items);
    TEST_RUN(test_names);
    TEST_RUN(test_contents);
    TEST_RUN(test_moves);
    TEST_RUN(test_read_only);
    free(disk);

    return TEST_REPORT("fs_ehfs");
}
