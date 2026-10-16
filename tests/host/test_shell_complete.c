/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include <string.h>

#include "kernel/memory/heap.h"
#include "kernel/shell/complete.h"
#include "kernel/vfs/ramfs.h"
#include "kernel/vfs/vfs.h"

static uint8_t arena[256 * 1024];

static int count(void)
{
    return 3;
}

static const char *name(int i)
{
    static const char *const names[] = {"help", "heap", "run"};

    return names[i];
}

static terminal_completion_t result;

static void complete(const char *line)
{
    shell_complete(line, strlen(line), &result);
}

static int has(const char *word)
{
    for (int i = 0; i < result.count; i++)
    {
        if (strcmp(result.item[i], word) == 0)
            return 1;
    }

    return 0;
}

static void setup(void)
{
    heap_init((uintptr_t)arena, sizeof(arena));
    vfs_reset();
    vfs_mount("/", ramfs_create());
    vfs_mkdir("/bin", 0755);
    vfs_mkdir("/docs", 0755);
    vfs_mkdir("/.hidden", 0755);

    vfs_file_t *f;

    vfs_open("/bin/hello", VFS_O_WRONLY | VFS_O_CREAT, 0755, &f);
    vfs_file_close(f);
    vfs_open("/bin/help", VFS_O_WRONLY | VFS_O_CREAT, 0755, &f);
    vfs_file_close(f);
    vfs_open("/docs/readme.txt", VFS_O_WRONLY | VFS_O_CREAT, 0644, &f);
    vfs_file_close(f);
    shell_complete_set_commands(count, name);
}

static void test_first_word(void)
{
    setup();

    /* Built-ins and /bin, each name once (help is both). */
    complete("he");
    TEST_ASSERT_EQ_INT(3, result.count);
    TEST_ASSERT(has("help") && has("heap") && has("hello"));
    TEST_ASSERT_EQ_INT(0, (int)result.start);

    complete("r");
    TEST_ASSERT_EQ_INT(1, result.count);
    TEST_ASSERT(has("run"));

    complete("zz");
    TEST_ASSERT_EQ_INT(0, result.count);
}

static void test_arguments_are_files(void)
{
    setup();

    complete("cat d");
    TEST_ASSERT_EQ_INT(1, result.count);
    TEST_ASSERT(has("docs/"));
    TEST_ASSERT_EQ_INT(4, (int)result.start);

    /* A path: the words keep the directory part. */
    complete("cat docs/r");
    TEST_ASSERT(has("docs/readme.txt"));
    TEST_ASSERT_EQ_INT(1, result.count);

    complete("cat /bin/he");
    TEST_ASSERT_EQ_INT(2, result.count);
    TEST_ASSERT(has("/bin/hello") && has("/bin/help"));

    /* Hidden names only when the dot is typed. */
    complete("ls ");
    TEST_ASSERT(!has(".hidden/"));
    complete("ls .");
    TEST_ASSERT(has(".hidden/"));

    /* Relative to the current directory. */
    vfs_chdir("/docs");
    complete("cat ");
    TEST_ASSERT_EQ_INT(1, result.count);
    TEST_ASSERT(has("readme.txt"));
}

int main(void)
{
    TEST_RUN(test_first_word);
    TEST_RUN(test_arguments_are_files);

    return TEST_REPORT("shell_complete");
}
