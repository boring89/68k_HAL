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
#include "kernel/lib/k_errno.h"
#include "kernel/memory/heap.h"
#include "kernel/vfs/ehfsfs.h"
#include "kernel/vfs/ramfs.h"
#include "kernel/vfs/vfs.h"

#define SECTORS (16384u * 8u)       /* 64 MiB */

static uint8_t arena[8u << 20] __attribute__((aligned(16)));
static uint8_t *disk;
static ehfs_t scratch;
static ehfs_check_t report;
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

static const block_ops_t ops = {mem_read, mem_write};
static const block_ops_t read_only_ops = {mem_read, NULL};
static block_device_t blank = {"blank", 512, 2048, &read_only_ops, NULL};
static block_device_t dev = {"ehfs0", 512, SECTORS, &ops, NULL};

static uint64_t clock_fn(void)
{
    return fake_now;
}

/* A fresh volume on dev (after a device with nothing on it), mounted at /ehfs over a RAM root. */
static void setup(void)
{
    ehfsfs_unmount();
    ehfsfs_set_mac_running(false);
    heap_init((uintptr_t)arena, sizeof(arena));
    vfs_reset();
    block_reset();

    if (disk == NULL)
        disk = malloc((size_t)SECTORS * 512);
    memset(disk, 0x5A, (size_t)SECTORS * 512);

    ehfs_format_options_t o;

    memset(&o, 0, sizeof o);
    o.name = "Data";
    o.now = fake_now;
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_format(&scratch, &dev, 0, SECTORS, &o));
    TEST_ASSERT_EQ_INT(0, block_register(&blank));
    TEST_ASSERT_EQ_INT(0, block_register(&dev));
    TEST_ASSERT_EQ_INT(0, ehfsfs_mount_any());
    TEST_ASSERT(ehfsfs_volume() != NULL && ehfsfs_volume()->dev == &dev);
    TEST_ASSERT_EQ_INT(0, vfs_mount("/", ramfs_create()));
    TEST_ASSERT_EQ_INT(0, vfs_mount("/ehfs", ehfsfs_create(clock_fn)));
}

static void fsck(void)
{
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_check(ehfsfs_volume(), &report));
    if (report.errors != 0)
        printf("    fsck: %s\n", report.first);
}

static int32_t put(const char *path, uint32_t flags, const void *data, uint32_t length)
{
    vfs_file_t *f;
    int32_t r = vfs_open(path, VFS_O_WRONLY | flags, 0644, &f);

    if (r != 0)
        return r;
    r = vfs_write(f, data, length);
    vfs_file_close(f);
    return r;
}

/* The whole file in buffer (size bytes of room); -errno or its length. */
static int32_t get(const char *path, void *buffer, uint32_t size)
{
    vfs_file_t *f;
    int32_t r = vfs_open(path, VFS_O_RDONLY, 0, &f);
    uint32_t total = 0;

    if (r != 0)
        return r;
    while (total < size && (r = vfs_read(f, (uint8_t *)buffer + total, size - total)) > 0)
        total += (uint32_t)r;
    vfs_file_close(f);
    return r < 0 ? r : (int32_t)total;
}

static bool content_is(const char *path, const char *text)
{
    char buffer[256];
    int32_t n = get(path, buffer, sizeof buffer - 1);

    if (n < 0)
        return false;
    buffer[n] = '\0';
    return strcmp(buffer, text) == 0;
}

/* The names of a folder after "." and "..", separated by spaces. */
static void listing(const char *path, char *out, size_t size)
{
    vfs_file_t *f;
    vfs_dirent_t e;

    out[0] = '\0';
    TEST_ASSERT_EQ_INT(0, vfs_open(path, VFS_O_RDONLY | VFS_O_DIRECTORY, 0, &f));
    TEST_ASSERT_EQ_INT(0, vfs_readdir(f, &e));
    TEST_ASSERT_EQ_STR(".", e.name);
    TEST_ASSERT_EQ_INT(0, vfs_readdir(f, &e));
    TEST_ASSERT_EQ_STR("..", e.name);
    while (vfs_readdir(f, &e) == 0)
    {
        if (out[0] != '\0')
            strncat(out, " ", size - strlen(out) - 1);
        strncat(out, e.name, size - strlen(out) - 1);
    }
    vfs_file_close(f);
}

static void test_files(void)
{
    vfs_stat_t st;
    vfs_file_t *f;
    char buffer[64];

    setup();
    TEST_ASSERT_EQ_INT(-K_ENOENT, vfs_open("/ehfs/a.txt", VFS_O_RDONLY, 0, &f));
    TEST_ASSERT_EQ_INT(5, put("/ehfs/a.txt", VFS_O_CREAT, "hello", 5));
    TEST_ASSERT(content_is("/ehfs/a.txt", "hello"));
    TEST_ASSERT_EQ_INT(0, vfs_stat("/ehfs/a.txt", &st));
    TEST_ASSERT_EQ_INT(5, (int)st.size);
    TEST_ASSERT_EQ_INT(VFS_S_IFREG | 0644, (int)st.mode);
    TEST_ASSERT_EQ_INT(1700000000, (int)st.mtime);

    /* Appending, replacing, refusing to create what exists. */
    TEST_ASSERT_EQ_INT(2, put("/ehfs/a.txt", VFS_O_APPEND, "!!", 2));
    TEST_ASSERT(content_is("/ehfs/a.txt", "hello!!"));
    TEST_ASSERT_EQ_INT(3, put("/ehfs/a.txt", VFS_O_TRUNC, "new", 3));
    TEST_ASSERT(content_is("/ehfs/a.txt", "new"));
    TEST_ASSERT_EQ_INT(-K_EEXIST, put("/ehfs/a.txt", VFS_O_CREAT | VFS_O_EXCL, "x", 1));
    TEST_ASSERT_EQ_INT(-K_ENOENT, put("/ehfs/b.txt", 0, "x", 1));

    /* Past the end: the gap reads as zeros; then shorter again. */
    TEST_ASSERT_EQ_INT(0, vfs_open("/ehfs/a.txt", VFS_O_RDWR, 0, &f));
    TEST_ASSERT_EQ_INT(10000, (int)vfs_seek(f, 10000, VFS_SEEK_SET));
    TEST_ASSERT_EQ_INT(4, vfs_write(f, "tail", 4));
    TEST_ASSERT_EQ_INT(0, vfs_fstat(f, &st));
    TEST_ASSERT_EQ_INT(10004, (int)st.size);
    TEST_ASSERT_EQ_INT(5000, (int)vfs_seek(f, 5000, VFS_SEEK_SET));
    TEST_ASSERT_EQ_INT(8, vfs_read(f, buffer, 8));
    TEST_ASSERT(memcmp(buffer, "\0\0\0\0\0\0\0\0", 8) == 0);
    TEST_ASSERT_EQ_INT(0, vfs_ftruncate(f, 2));
    TEST_ASSERT_EQ_INT(0, vfs_fstat(f, &st));
    TEST_ASSERT_EQ_INT(2, (int)st.size);
    vfs_file_close(f);
    TEST_ASSERT(content_is("/ehfs/a.txt", "ne"));

    /* A larger file, in pieces of odd sizes. */
    uint32_t size = 300u * 1024u + 77u;
    uint8_t *data = malloc(size), *back = malloc(size);

    for (uint32_t i = 0; i < size; i++)
        data[i] = (uint8_t)(i * 13u + i / 4099u);
    TEST_ASSERT_EQ_INT(0, vfs_open("/ehfs/big.bin", VFS_O_WRONLY | VFS_O_CREAT, 0644, &f));
    for (uint32_t at = 0; at < size;)
    {
        uint32_t n = size - at < 7001u ? size - at : 7001u;

        TEST_ASSERT_EQ_INT((int)n, vfs_write(f, data + at, n));
        at += n;
    }
    vfs_file_close(f);
    TEST_ASSERT_EQ_INT((int)size, get("/ehfs/big.bin", back, size));
    TEST_ASSERT(memcmp(data, back, size) == 0);
    free(data);
    free(back);
    fsck();
}

static void test_folders(void)
{
    vfs_file_t *f;
    vfs_stat_t st;
    char names[256];

    setup();
    TEST_ASSERT_EQ_INT(0, vfs_mkdir("/ehfs/Docs", 0755));
    TEST_ASSERT_EQ_INT(-K_EEXIST, vfs_mkdir("/ehfs/Docs", 0755));
    TEST_ASSERT_EQ_INT(-K_EEXIST, vfs_mkdir("/ehfs", 0755));
    TEST_ASSERT_EQ_INT(-K_ENOENT, vfs_mkdir("/ehfs/No/Such", 0755));
    TEST_ASSERT_EQ_INT(0, vfs_stat("/ehfs/Docs", &st));
    TEST_ASSERT_EQ_INT(VFS_S_IFDIR | 0755, (int)st.mode);
    TEST_ASSERT_EQ_INT(4, put("/ehfs/Docs/zeta", VFS_O_CREAT, "zzzz", 4));
    TEST_ASSERT_EQ_INT(5, put("/ehfs/Docs/Alpha", VFS_O_CREAT, "aaaaa", 5));
    TEST_ASSERT_EQ_INT(0, vfs_mkdir("/ehfs/Docs/beta", 0755));
    listing("/ehfs/Docs", names, sizeof names);
    TEST_ASSERT_EQ_STR("Alpha beta zeta", names);
    listing("/ehfs", names, sizeof names);
    TEST_ASSERT_EQ_STR("Docs", names);

    /* What does not fit. */
    TEST_ASSERT_EQ_INT(-K_ENOTEMPTY, vfs_rmdir("/ehfs/Docs"));
    TEST_ASSERT_EQ_INT(-K_EISDIR, vfs_unlink("/ehfs/Docs/beta"));
    TEST_ASSERT_EQ_INT(-K_ENOTDIR, vfs_rmdir("/ehfs/Docs/zeta"));
    TEST_ASSERT_EQ_INT(-K_EISDIR, vfs_open("/ehfs/Docs", VFS_O_WRONLY, 0, &f));
    TEST_ASSERT_EQ_INT(-K_ENOTDIR, vfs_open("/ehfs/Docs/zeta", VFS_O_RDONLY | VFS_O_DIRECTORY, 0, &f));
    TEST_ASSERT_EQ_INT(-K_ENOTDIR, vfs_open("/ehfs/Docs/zeta/x", VFS_O_RDONLY, 0, &f));
    TEST_ASSERT_EQ_INT(-K_ENOTDIR, put("/ehfs/Docs/zeta/x", VFS_O_CREAT, "x", 1));

    /* Emptied and removed. */
    TEST_ASSERT_EQ_INT(0, vfs_unlink("/ehfs/Docs/zeta"));
    TEST_ASSERT_EQ_INT(0, vfs_unlink("/ehfs/Docs/Alpha"));
    TEST_ASSERT_EQ_INT(0, vfs_rmdir("/ehfs/Docs/beta"));
    TEST_ASSERT_EQ_INT(0, vfs_rmdir("/ehfs/Docs"));
    listing("/ehfs", names, sizeof names);
    TEST_ASSERT_EQ_STR("", names);
    TEST_ASSERT_EQ_INT(0, (int)ehfs_sb_get64(ehfsfs_volume(), SB_FILE_COUNT));
    fsck();
}

static void test_names(void)
{
    char name[300];
    vfs_file_t *f;

    setup();
    TEST_ASSERT_EQ_INT(3, put("/ehfs/Read Me.txt", VFS_O_CREAT, "abc", 3));
    TEST_ASSERT(content_is("/ehfs/READ ME.TXT", "abc"));        /* without regard to case */
    TEST_ASSERT_EQ_INT(-K_EEXIST, put("/ehfs/read me.txt", VFS_O_CREAT | VFS_O_EXCL, "x", 1));

    /* "café" written precomposed, found decomposed (NFC), listed as it was written. */
    TEST_ASSERT_EQ_INT(4, put("/ehfs/caf\xC3\xA9", VFS_O_CREAT, "cafe", 4));
    TEST_ASSERT(content_is("/ehfs/cafe\xCC\x81", "cafe"));
    char names[256];

    listing("/ehfs", names, sizeof names);
    TEST_ASSERT_EQ_STR("caf\xC3\xA9 Read Me.txt", names);

    /* 255 bytes is the longest name. */
    memset(name, 0, sizeof name);
    strcpy(name, "/ehfs/");
    memset(name + 6, 'n', 255);
    TEST_ASSERT_EQ_INT(1, put(name, VFS_O_CREAT, "n", 1));
    name[6 + 255] = 'n';
    TEST_ASSERT_EQ_INT(-K_ENAMETOOLONG, put(name, VFS_O_CREAT, "n", 1));
    TEST_ASSERT_EQ_INT(-K_ENAMETOOLONG, vfs_open(name, VFS_O_RDONLY, 0, &f));

    /* Not UTF-8. */
    TEST_ASSERT_EQ_INT(-K_EINVAL, put("/ehfs/bad\xFF", VFS_O_CREAT, "x", 1));
    fsck();
}

static void test_rename(void)
{
    char names[256];
    vfs_stat_t st;

    setup();
    TEST_ASSERT_EQ_INT(0, vfs_mkdir("/ehfs/A", 0755));
    TEST_ASSERT_EQ_INT(0, vfs_mkdir("/ehfs/A/B", 0755));
    TEST_ASSERT_EQ_INT(1, put("/ehfs/one", VFS_O_CREAT, "1", 1));
    TEST_ASSERT_EQ_INT(1, put("/ehfs/two", VFS_O_CREAT, "2", 1));

    TEST_ASSERT_EQ_INT(0, vfs_rename("/ehfs/one", "/ehfs/A/uno"));
    TEST_ASSERT(content_is("/ehfs/A/uno", "1"));
    TEST_ASSERT_EQ_INT(-K_ENOENT, vfs_stat("/ehfs/one", &st));

    /* Over a file that exists: it is replaced. */
    TEST_ASSERT_EQ_INT(0, vfs_rename("/ehfs/two", "/ehfs/A/uno"));
    TEST_ASSERT(content_is("/ehfs/A/uno", "2"));
    TEST_ASSERT_EQ_INT(1, (int)ehfs_sb_get64(ehfsfs_volume(), SB_FILE_COUNT));

    /* Only the case changes. */
    TEST_ASSERT_EQ_INT(0, vfs_rename("/ehfs/A/uno", "/ehfs/A/UNO"));
    listing("/ehfs/A", names, sizeof names);
    TEST_ASSERT_EQ_STR("B UNO", names);

    /* A folder into itself, a folder over a file and the other way, over a folder with things in it. */
    TEST_ASSERT_EQ_INT(-K_EINVAL, vfs_rename("/ehfs/A", "/ehfs/A/B/A"));
    TEST_ASSERT_EQ_INT(-K_ENOTDIR, vfs_rename("/ehfs/A/B", "/ehfs/A/UNO"));
    TEST_ASSERT_EQ_INT(-K_EISDIR, vfs_rename("/ehfs/A/UNO", "/ehfs/A/B"));
    TEST_ASSERT_EQ_INT(0, vfs_mkdir("/ehfs/C", 0755));
    TEST_ASSERT_EQ_INT(-K_ENOTEMPTY, vfs_rename("/ehfs/C", "/ehfs/A"));

    /* A folder over an empty folder, then out to the root. */
    TEST_ASSERT_EQ_INT(0, vfs_rename("/ehfs/C", "/ehfs/A/B"));
    TEST_ASSERT_EQ_INT(0, vfs_rename("/ehfs/A/B", "/ehfs/D"));
    listing("/ehfs", names, sizeof names);
    TEST_ASSERT_EQ_STR("A D", names);
    TEST_ASSERT_EQ_INT(-K_ENOENT, vfs_rename("/ehfs/nothing", "/ehfs/x"));
    fsck();
}

static void test_mac_running(void)
{
    vfs_file_t *f;

    setup();
    TEST_ASSERT_EQ_INT(2, put("/ehfs/f", VFS_O_CREAT, "ok", 2));
    TEST_ASSERT_EQ_INT(0, vfs_open("/ehfs/f", VFS_O_RDWR, 0, &f));
    ehfsfs_set_mac_running(true);
    TEST_ASSERT(content_is("/ehfs/f", "ok"));
    TEST_ASSERT_EQ_INT(-K_EROFS, put("/ehfs/f", 0, "no", 2));
    TEST_ASSERT_EQ_INT(-K_EROFS, put("/ehfs/g", VFS_O_CREAT, "no", 2));
    TEST_ASSERT_EQ_INT(-K_EROFS, vfs_write(f, "no", 2));    /* open from before */
    TEST_ASSERT_EQ_INT(-K_EROFS, vfs_ftruncate(f, 0));
    TEST_ASSERT_EQ_INT(-K_EROFS, vfs_mkdir("/ehfs/d", 0755));
    TEST_ASSERT_EQ_INT(-K_EROFS, vfs_unlink("/ehfs/f"));
    TEST_ASSERT_EQ_INT(-K_EROFS, vfs_rename("/ehfs/f", "/ehfs/h"));
    ehfsfs_set_mac_running(false);
    TEST_ASSERT_EQ_INT(3, vfs_write(f, "yes", 3));
    vfs_file_close(f);
    TEST_ASSERT(content_is("/ehfs/f", "yes"));
    fsck();
}

static void test_no_volume(void)
{
    vfs_file_t *f, *dir;
    vfs_stat_t st;
    vfs_dirent_t e;
    char buffer[8];

    setup();
    TEST_ASSERT_EQ_INT(2, put("/ehfs/f", VFS_O_CREAT, "ok", 2));
    TEST_ASSERT_EQ_INT(0, vfs_open("/ehfs/f", VFS_O_RDONLY, 0, &f));
    ehfsfs_unmount();

    /* What was open belongs to a volume that is gone. */
    TEST_ASSERT_EQ_INT(-K_EIO, vfs_read(f, buffer, sizeof buffer));
    TEST_ASSERT_EQ_INT(-K_EIO, vfs_fstat(f, &st));
    vfs_file_close(f);

    /* An empty root that cannot be written. */
    TEST_ASSERT_EQ_INT(0, vfs_stat("/ehfs", &st));
    TEST_ASSERT_EQ_INT(VFS_S_IFDIR | 0555, (int)st.mode);
    TEST_ASSERT_EQ_INT(0, vfs_open("/ehfs", VFS_O_RDONLY | VFS_O_DIRECTORY, 0, &dir));
    TEST_ASSERT_EQ_INT(0, vfs_readdir(dir, &e));
    TEST_ASSERT_EQ_INT(0, vfs_readdir(dir, &e));
    TEST_ASSERT_EQ_INT(1, vfs_readdir(dir, &e));
    vfs_file_close(dir);
    TEST_ASSERT_EQ_INT(-K_ENOENT, vfs_stat("/ehfs/f", &st));
    TEST_ASSERT_EQ_INT(-K_EROFS, put("/ehfs/g", VFS_O_CREAT, "x", 1));
    TEST_ASSERT_EQ_INT(-K_EROFS, vfs_mkdir("/ehfs/d", 0755));

    /* Unmounted cleanly; mounted again, the file is there. */
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_mount(&scratch, &dev, 0, true));
    TEST_ASSERT_EQ_INT(EHFS_STATE_CLEAN, (int)ehfs_sb_get32(&scratch, SB_STATE));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_check(&scratch, &report));
    TEST_ASSERT_EQ_INT(0, ehfsfs_mount_any());
    TEST_ASSERT(content_is("/ehfs/f", "ok"));
    TEST_ASSERT_EQ_INT(EHFS_STATE_MOUNTED, (int)ehfs_sb_get32(ehfsfs_volume(), SB_STATE));

    /* A device that cannot be written: mounted read-only. */
    ehfsfs_unmount();
    dev.ops = &read_only_ops;
    TEST_ASSERT_EQ_INT(0, ehfsfs_mount_any());
    TEST_ASSERT(ehfsfs_volume()->read_only);
    TEST_ASSERT(content_is("/ehfs/f", "ok"));
    TEST_ASSERT_EQ_INT(-K_EROFS, put("/ehfs/f", 0, "no", 2));
    ehfsfs_unmount();
    dev.ops = &ops;

    /* No volume anywhere. */
    memset(disk, 0, 3 * 512);
    TEST_ASSERT_EQ_INT(-1, ehfsfs_mount_any());
    TEST_ASSERT(ehfsfs_volume() == NULL);
}

int main(void)
{
    TEST_RUN(test_files);
    TEST_RUN(test_folders);
    TEST_RUN(test_names);
    TEST_RUN(test_rename);
    TEST_RUN(test_mac_running);
    TEST_RUN(test_no_volume);
    ehfsfs_unmount();
    free(disk);

    return TEST_REPORT("ehfsfs");
}
