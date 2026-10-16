/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include <stdio.h>
#include <string.h>

#include "kernel/lib/k_errno.h"
#include "kernel/memory/heap.h"
#include "kernel/vfs/procfs.h"
#include "kernel/vfs/vfs.h"

static uint8_t arena[256 * 1024];
static int counter;

static size_t hello_text(char *buffer, size_t size)
{
    (void)size;
    memcpy(buffer, "hello\n", 6);

    return 6;
}

/* Different every time it is made. */
static size_t counter_text(char *buffer, size_t size)
{
    return (size_t)snprintf(buffer, size, "%d\n", ++counter);
}

static size_t huge_text(char *buffer, size_t size)
{
    memset(buffer, 'x', size);

    return size + 100; /* a generator that lies about its length */
}

static vfs_fs_t *setup(void)
{
    heap_init((uintptr_t)arena, sizeof(arena));
    vfs_reset();
    counter = 0;

    vfs_fs_t *fs = procfs_create();

    TEST_ASSERT(fs != NULL);
    TEST_ASSERT_EQ_INT(0, procfs_add(fs, "hello", hello_text));
    TEST_ASSERT_EQ_INT(0, procfs_add(fs, "counter", counter_text));
    TEST_ASSERT_EQ_INT(0, procfs_add(fs, "huge", huge_text));
    vfs_mount("/proc", fs);

    return fs;
}

static void test_read_and_stat(void)
{
    vfs_fs_t *fs = setup();
    vfs_file_t *f;
    vfs_stat_t st;
    char buf[64];

    TEST_ASSERT_EQ_INT(0, vfs_open("/proc/hello", VFS_O_RDONLY, 0, &f));
    TEST_ASSERT_EQ_INT(6, vfs_read(f, buf, sizeof(buf)));
    TEST_ASSERT(memcmp(buf, "hello\n", 6) == 0);
    TEST_ASSERT_EQ_INT(0, vfs_read(f, buf, sizeof(buf))); /* end */
    vfs_file_close(f);

    TEST_ASSERT_EQ_INT(0, vfs_stat("/proc/hello", &st));
    TEST_ASSERT_EQ_INT(6, (int)st.size);
    TEST_ASSERT_EQ_INT(VFS_S_IFREG, st.mode & VFS_S_IFMT);
    TEST_ASSERT_EQ_INT(0, vfs_stat("/proc", &st));
    TEST_ASSERT_EQ_INT(VFS_S_IFDIR, st.mode & VFS_S_IFMT);
    TEST_ASSERT_EQ_INT(-K_ENOENT, vfs_stat("/proc/nothing", &st));

    /* A generator that claims too much is cut at the limit. */
    TEST_ASSERT_EQ_INT(0, vfs_stat("/proc/huge", &st));
    TEST_ASSERT_EQ_INT(PROCFS_MAX_SIZE, (int)st.size);

    (void)fs;
}

static void test_snapshot_at_open(void)
{
    setup();
    vfs_file_t *f;
    char buf[16];

    /* The text is made at open: it does not change while the file is open, and a new open makes it again. */
    TEST_ASSERT_EQ_INT(0, vfs_open("/proc/counter", VFS_O_RDONLY, 0, &f));
    TEST_ASSERT_EQ_INT(2, vfs_read(f, buf, 2));
    TEST_ASSERT(memcmp(buf, "1\n", 2) == 0);
    TEST_ASSERT_EQ_INT(0, (int)vfs_seek(f, 0, VFS_SEEK_SET));
    TEST_ASSERT_EQ_INT(2, vfs_read(f, buf, 2));
    TEST_ASSERT(memcmp(buf, "1\n", 2) == 0);
    vfs_file_close(f);

    TEST_ASSERT_EQ_INT(0, vfs_open("/proc/counter", VFS_O_RDONLY, 0, &f));
    TEST_ASSERT_EQ_INT(2, vfs_read(f, buf, 2));
    TEST_ASSERT(memcmp(buf, "2\n", 2) == 0);
    vfs_file_close(f);
}

static void test_directory_and_rejects(void)
{
    setup();
    vfs_file_t *f;
    vfs_dirent_t de;
    int names = 0;

    TEST_ASSERT_EQ_INT(0, vfs_open("/proc", VFS_O_RDONLY, 0, &f));

    while (vfs_readdir(f, &de) == 0)
        names++;

    TEST_ASSERT_EQ_INT(5, names); /* . .. hello counter huge */
    vfs_file_close(f);

    TEST_ASSERT_EQ_INT(-K_EROFS, vfs_open("/proc/hello", VFS_O_WRONLY, 0, &f));
    TEST_ASSERT_EQ_INT(-K_EROFS, vfs_open("/proc/new", VFS_O_WRONLY | VFS_O_CREAT, 0, &f));
    TEST_ASSERT_EQ_INT(-K_ENOENT, vfs_open("/proc/missing", VFS_O_RDONLY, 0, &f));
    TEST_ASSERT_EQ_INT(-K_ENOTDIR, vfs_open("/proc/hello", VFS_O_RDONLY | VFS_O_DIRECTORY, 0, &f));
}

static void test_add_rejects(void)
{
    vfs_fs_t *fs = setup();

    TEST_ASSERT_EQ_INT(-1, procfs_add(fs, "hello", hello_text)); /* twice */
    TEST_ASSERT_EQ_INT(-1, procfs_add(fs, "", hello_text));
    TEST_ASSERT_EQ_INT(-1, procfs_add(fs, "a/b", hello_text));
    TEST_ASSERT_EQ_INT(-1, procfs_add(fs, "x", NULL));

    char name[8];

    for (int i = 0; i < PROCFS_MAX_FILES; i++)
    {
        snprintf(name, sizeof(name), "f%d", i);
        procfs_add(fs, name, hello_text);
    }

    TEST_ASSERT_EQ_INT(-1, procfs_add(fs, "last", hello_text)); /* full */
}

int main(void)
{
    TEST_RUN(test_read_and_stat);
    TEST_RUN(test_snapshot_at_open);
    TEST_RUN(test_directory_and_rejects);
    TEST_RUN(test_add_rejects);

    return TEST_REPORT("procfs");
}
