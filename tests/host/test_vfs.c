/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "kernel/lib/k_errno.h"
#include "kernel/memory/heap.h"
#include "kernel/vfs/ramfs.h"
#include "kernel/vfs/tarfs.h"
#include "kernel/vfs/vfs.h"

#include <stdio.h>
#include <string.h>

static uint8_t arena[1 << 20] __attribute__((aligned(16)));

static void setup(void)
{
    heap_init((uintptr_t)arena, sizeof(arena));
    vfs_reset();
}

static void normalized(const char *in, const char *want)
{
    char out[VFS_PATH_MAX];

    TEST_ASSERT_EQ_INT(0, vfs_normalize(in, out));
    TEST_ASSERT_EQ_STR(want, out);
}

static void test_normalize(void)
{
    normalized("/", "/");
    normalized("//", "/");
    normalized("/a/b", "/a/b");
    normalized("/a//b/", "/a/b");
    normalized("/a/./b", "/a/b");
    normalized("/a/b/..", "/a");
    normalized("/a/../..", "/");
    normalized("/..", "/");
    normalized("/a/b/../../c", "/c");
    normalized("/.hidden", "/.hidden");
    normalized("/a/...", "/a/...");

    char out[VFS_PATH_MAX];

    TEST_ASSERT_EQ_INT(-K_ENOENT, vfs_normalize("", out));
    TEST_ASSERT_EQ_INT(-K_ENOENT, vfs_normalize(NULL, out));

    /* The current directory is "/" at first. */
    normalized("relative", "/relative");
    normalized("a/../b/./c", "/b/c");
    normalized("..", "/");

    char longname[400];

    memset(longname, 'x', sizeof(longname) - 1);
    longname[0] = '/';
    longname[sizeof(longname) - 1] = '\0';
    TEST_ASSERT_EQ_INT(-K_ENAMETOOLONG, vfs_normalize(longname, out));
}

static void test_cwd(void)
{
    setup();
    vfs_mount("/", ramfs_create());

    vfs_file_t *file;

    TEST_ASSERT_EQ_STR("/", vfs_getcwd());
    TEST_ASSERT_EQ_INT(0, vfs_mkdir("/work", 0755));
    TEST_ASSERT_EQ_INT(0, vfs_mkdir("/work/sub", 0755));
    TEST_ASSERT_EQ_INT(0, vfs_open("/work/sub/f.txt", VFS_O_WRONLY | VFS_O_CREAT, 0644, &file));
    vfs_file_close(file);

    TEST_ASSERT_EQ_INT(0, vfs_chdir("work"));
    TEST_ASSERT_EQ_STR("/work", vfs_getcwd());
    normalized("sub/f.txt", "/work/sub/f.txt");
    normalized("../x", "/x");
    normalized("/abs", "/abs");

    /* Relative paths find files from there. */
    vfs_stat_t stat;

    TEST_ASSERT_EQ_INT(0, vfs_stat("sub/f.txt", &stat));
    TEST_ASSERT_EQ_INT(0, vfs_open("sub/f.txt", VFS_O_RDONLY, 0, &file));
    vfs_file_close(file);

    TEST_ASSERT_EQ_INT(0, vfs_chdir("sub/.."));
    TEST_ASSERT_EQ_STR("/work", vfs_getcwd());
    TEST_ASSERT_EQ_INT(0, vfs_chdir("/"));
    TEST_ASSERT_EQ_STR("/", vfs_getcwd());

    /* A file or a missing directory is refused, and the directory stays. */
    TEST_ASSERT_EQ_INT(0, vfs_chdir("/work"));
    TEST_ASSERT_EQ_INT(-K_ENOTDIR, vfs_chdir("sub/f.txt"));
    TEST_ASSERT_EQ_INT(-K_ENOENT, vfs_chdir("nothing"));
    TEST_ASSERT_EQ_STR("/work", vfs_getcwd());

    vfs_reset();
    TEST_ASSERT_EQ_STR("/", vfs_getcwd());
}

static void test_ramfs_files(void)
{
    setup();
    vfs_fs_t *fs = ramfs_create();

    TEST_ASSERT(fs != NULL);
    TEST_ASSERT_EQ_INT(0, vfs_mount("/tmp", fs));

    vfs_file_t *f;
    char buf[64];

    TEST_ASSERT_EQ_INT(-K_ENOENT, vfs_open("/tmp/a.txt", VFS_O_RDONLY, 0, &f));
    TEST_ASSERT_EQ_INT(0, vfs_open("/tmp/a.txt", VFS_O_RDWR | VFS_O_CREAT, 0644, &f));
    TEST_ASSERT_EQ_INT(5, vfs_write(f, "hello", 5));
    TEST_ASSERT_EQ_INT(6, vfs_write(f, " world", 6));
    TEST_ASSERT_EQ_INT(0, (int)vfs_seek(f, 0, VFS_SEEK_SET));
    TEST_ASSERT_EQ_INT(11, vfs_read(f, buf, sizeof(buf)));
    TEST_ASSERT(memcmp(buf, "hello world", 11) == 0);
    TEST_ASSERT_EQ_INT(0, vfs_read(f, buf, sizeof(buf)));
    TEST_ASSERT_EQ_INT(11, (int)vfs_seek(f, 0, VFS_SEEK_END));

    vfs_stat_t st;

    TEST_ASSERT_EQ_INT(0, vfs_fstat(f, &st));
    TEST_ASSERT_EQ_INT(11, (int)st.size);
    TEST_ASSERT_EQ_INT(VFS_S_IFREG, st.mode & VFS_S_IFMT);

    /* A hole reads as zeros. */
    TEST_ASSERT_EQ_INT(20, (int)vfs_seek(f, 20, VFS_SEEK_SET));
    TEST_ASSERT_EQ_INT(1, vfs_write(f, "!", 1));
    TEST_ASSERT_EQ_INT(11, (int)vfs_seek(f, 11, VFS_SEEK_SET));
    TEST_ASSERT_EQ_INT(10, vfs_read(f, buf, 10));
    TEST_ASSERT_EQ_INT(0, buf[0] | buf[8]);
    TEST_ASSERT_EQ_INT('!', buf[9]);

    TEST_ASSERT_EQ_INT(0, vfs_ftruncate(f, 5));
    TEST_ASSERT_EQ_INT(0, vfs_stat("/tmp/a.txt", &st));
    TEST_ASSERT_EQ_INT(5, (int)st.size);
    vfs_file_close(f);

    /* Truncate on open, exclusive create, append. */
    TEST_ASSERT_EQ_INT(-K_EEXIST, vfs_open("/tmp/a.txt", VFS_O_WRONLY | VFS_O_CREAT | VFS_O_EXCL, 0, &f));
    TEST_ASSERT_EQ_INT(0, vfs_open("/tmp/a.txt", VFS_O_WRONLY | VFS_O_APPEND, 0, &f));
    TEST_ASSERT_EQ_INT(2, vfs_write(f, "++", 2));
    vfs_file_close(f);
    TEST_ASSERT_EQ_INT(0, vfs_stat("/tmp/a.txt", &st));
    TEST_ASSERT_EQ_INT(7, (int)st.size);
    TEST_ASSERT_EQ_INT(0, vfs_open("/tmp/a.txt", VFS_O_WRONLY | VFS_O_TRUNC, 0, &f));
    vfs_file_close(f);
    TEST_ASSERT_EQ_INT(0, vfs_stat("/tmp/a.txt", &st));
    TEST_ASSERT_EQ_INT(0, (int)st.size);

    /* Access modes are enforced. */
    TEST_ASSERT_EQ_INT(0, vfs_open("/tmp/a.txt", VFS_O_RDONLY, 0, &f));
    TEST_ASSERT_EQ_INT(-K_EBADF, vfs_write(f, "x", 1));
    vfs_file_close(f);
    TEST_ASSERT_EQ_INT(0, vfs_open("/tmp/a.txt", VFS_O_WRONLY, 0, &f));
    TEST_ASSERT_EQ_INT(-K_EBADF, vfs_read(f, buf, 1));
    vfs_file_close(f);

    ramfs_destroy(fs);
}

static void test_ramfs_directories(void)
{
    setup();
    vfs_fs_t *fs = ramfs_create();
    vfs_file_t *f;
    vfs_stat_t st;
    vfs_dirent_t de;

    vfs_mount("/", fs);

    TEST_ASSERT_EQ_INT(0, vfs_mkdir("/dir", 0755));
    TEST_ASSERT_EQ_INT(-K_EEXIST, vfs_mkdir("/dir", 0755));
    TEST_ASSERT_EQ_INT(-K_ENOENT, vfs_mkdir("/nope/sub", 0755));
    TEST_ASSERT_EQ_INT(0, vfs_open("/dir/x", VFS_O_WRONLY | VFS_O_CREAT, 0, &f));
    vfs_file_close(f);
    TEST_ASSERT_EQ_INT(0, vfs_open("/dir/y", VFS_O_WRONLY | VFS_O_CREAT, 0, &f));
    vfs_file_close(f);
    TEST_ASSERT_EQ_INT(0, vfs_stat("/dir", &st));
    TEST_ASSERT_EQ_INT(VFS_S_IFDIR, st.mode & VFS_S_IFMT);

    TEST_ASSERT_EQ_INT(-K_ENOTDIR, vfs_open("/dir/x/z", VFS_O_RDONLY, 0, &f));
    TEST_ASSERT_EQ_INT(-K_EISDIR, vfs_open("/dir", VFS_O_WRONLY, 0, &f));

    TEST_ASSERT_EQ_INT(0, vfs_open("/dir", VFS_O_RDONLY | VFS_O_DIRECTORY, 0, &f));
    int names = 0;

    while (vfs_readdir(f, &de) == 0)
        names++;

    TEST_ASSERT_EQ_INT(4, names); /* . .. x y */
    vfs_file_close(f);

    TEST_ASSERT_EQ_INT(-K_ENOTEMPTY, vfs_rmdir("/dir"));
    TEST_ASSERT_EQ_INT(-K_EISDIR, vfs_unlink("/dir"));
    TEST_ASSERT_EQ_INT(-K_ENOTDIR, vfs_rmdir("/dir/x"));
    TEST_ASSERT_EQ_INT(0, vfs_unlink("/dir/x"));
    TEST_ASSERT_EQ_INT(-K_ENOENT, vfs_stat("/dir/x", &st));

    /* rename: move into another directory, replace a file. */
    TEST_ASSERT_EQ_INT(0, vfs_mkdir("/other", 0755));
    TEST_ASSERT_EQ_INT(0, vfs_rename("/dir/y", "/other/z"));
    TEST_ASSERT_EQ_INT(-K_ENOENT, vfs_stat("/dir/y", &st));
    TEST_ASSERT_EQ_INT(0, vfs_stat("/other/z", &st));
    TEST_ASSERT_EQ_INT(0, vfs_rmdir("/dir"));
    TEST_ASSERT_EQ_INT(-K_EINVAL, vfs_rename("/other", "/other/inside"));

    /* Unlinking an open file keeps it usable until the last close. */
    TEST_ASSERT_EQ_INT(0, vfs_open("/other/z", VFS_O_RDWR, 0, &f));
    TEST_ASSERT_EQ_INT(0, vfs_unlink("/other/z"));
    TEST_ASSERT_EQ_INT(3, vfs_write(f, "abc", 3));
    vfs_file_close(f);

    ramfs_destroy(fs);
}

/* --- tarfs --- */

static size_t add_entry(uint8_t *tar, size_t at, const char *name, const char *prefix,
                        char type, const char *content)
{
    uint8_t *h = tar + at;
    size_t length = content ? strlen(content) : 0;

    memset(h, 0, 512);
    strcpy((char *)h, name);
    strcpy((char *)h + 345, prefix);
    sprintf((char *)h + 124, "%011zo", length);
    h[156] = (uint8_t)type;
    memcpy(h + 257, "ustar", 5);

    if (length)
        memcpy(h + 512, content, length);

    return at + 512 + (length + 511) / 512 * 512;
}

/* A hard link entry: no data, the linkname field names the file it shares. */
static size_t add_link(uint8_t *tar, size_t at, const char *name, const char *target)
{
    uint8_t *h = tar + at;

    memset(h, 0, 512);
    strcpy((char *)h, name);
    strcpy((char *)h + 157, target);
    sprintf((char *)h + 124, "%011o", 0);
    h[156] = '1';
    memcpy(h + 257, "ustar", 5);

    return at + 512;
}

static size_t make_tar(uint8_t *tar)
{
    size_t at = 0;

    memset(tar, 0, 8192);
    at = add_entry(tar, at, "./", "", '5', NULL);
    at = add_entry(tar, at, "./hello.txt", "", '0', "hello from tar\n");
    at = add_entry(tar, at, "bin/", "", '5', NULL);
    at = add_entry(tar, at, "bin/prog", "", '0', "ELF?");
    at = add_entry(tar, at, "deep/er/file", "", '0', "x");
    at = add_entry(tar, at, "name", "long/prefix", '0', "p");
    at = add_link(tar, at, "bin/alias", "bin/prog");
    at = add_link(tar, at, "bin/dangling", "bin/missing");

    return at + 1024;
}

static void test_tarfs(void)
{
    setup();
    static uint8_t tar[8192];
    size_t size = make_tar(tar);
    vfs_fs_t *fs = tarfs_create(tar, size);

    TEST_ASSERT(fs != NULL);
    vfs_mount("/", fs);

    vfs_file_t *f;
    vfs_stat_t st;
    char buf[64];

    TEST_ASSERT_EQ_INT(0, vfs_open("/hello.txt", VFS_O_RDONLY, 0, &f));
    TEST_ASSERT_EQ_INT(15, vfs_read(f, buf, sizeof(buf)));
    TEST_ASSERT(memcmp(buf, "hello from tar\n", 15) == 0);
    vfs_file_close(f);

    TEST_ASSERT_EQ_INT(0, vfs_stat("/bin/prog", &st));
    TEST_ASSERT_EQ_INT(4, (int)st.size);
    TEST_ASSERT_EQ_INT(0, vfs_stat("/bin", &st));
    TEST_ASSERT_EQ_INT(VFS_S_IFDIR, st.mode & VFS_S_IFMT);
    TEST_ASSERT_EQ_INT(0, vfs_stat("/deep/er", &st)); /* only implied by a path */
    TEST_ASSERT_EQ_INT(VFS_S_IFDIR, st.mode & VFS_S_IFMT);
    TEST_ASSERT_EQ_INT(0, vfs_stat("/long/prefix/name", &st));
    TEST_ASSERT_EQ_INT(-K_ENOENT, vfs_stat("/missing", &st));
    TEST_ASSERT_EQ_INT(-K_ENOENT, vfs_stat("/bin/pro", &st));

    /* Read-only. */
    TEST_ASSERT_EQ_INT(-K_EROFS, vfs_open("/hello.txt", VFS_O_WRONLY, 0, &f));
    TEST_ASSERT_EQ_INT(-K_EROFS, vfs_open("/new", VFS_O_WRONLY | VFS_O_CREAT, 0, &f));
    TEST_ASSERT_EQ_INT(-K_EROFS, vfs_mkdir("/d", 0755));
    TEST_ASSERT_EQ_INT(-K_EROFS, vfs_unlink("/hello.txt"));

    /* Listing the root: hello.txt, bin, deep, long (each once). */
    TEST_ASSERT_EQ_INT(0, vfs_open("/", VFS_O_RDONLY, 0, &f));
    vfs_dirent_t de;
    int names = 0, saw_bin = 0, saw_hello = 0;

    while (vfs_readdir(f, &de) == 0)
    {
        names++;
        saw_bin |= strcmp(de.name, "bin") == 0 && de.type == VFS_DT_DIR;
        saw_hello |= strcmp(de.name, "hello.txt") == 0 && de.type == VFS_DT_REG;
    }

    TEST_ASSERT_EQ_INT(6, names); /* . .. hello.txt bin deep long */
    TEST_ASSERT(saw_bin && saw_hello);
    vfs_file_close(f);

    TEST_ASSERT_EQ_INT(0, vfs_open("/bin", VFS_O_RDONLY, 0, &f));
    names = 0;

    while (vfs_readdir(f, &de) == 0)
        names++;

    TEST_ASSERT_EQ_INT(5, names); /* . .. prog alias dangling */
    vfs_file_close(f);

    /* A hard link has the contents of the file it names; one to a missing file is empty. */
    TEST_ASSERT_EQ_INT(0, vfs_stat("/bin/alias", &st));
    TEST_ASSERT_EQ_INT(4, (int)st.size);
    TEST_ASSERT_EQ_INT(0, vfs_open("/bin/alias", VFS_O_RDONLY, 0, &f));
    TEST_ASSERT_EQ_INT(4, vfs_read(f, buf, sizeof(buf)));
    TEST_ASSERT(memcmp(buf, "ELF?", 4) == 0);
    vfs_file_close(f);
    TEST_ASSERT_EQ_INT(0, vfs_stat("/bin/dangling", &st));
    TEST_ASSERT_EQ_INT(0, (int)st.size);

    /* Seeking and partial reads. */
    TEST_ASSERT_EQ_INT(0, vfs_open("/hello.txt", VFS_O_RDONLY, 0, &f));
    TEST_ASSERT_EQ_INT(6, (int)vfs_seek(f, 6, VFS_SEEK_SET));
    TEST_ASSERT_EQ_INT(4, vfs_read(f, buf, 4));
    TEST_ASSERT(memcmp(buf, "from", 4) == 0);
    TEST_ASSERT_EQ_INT(-K_EINVAL, (int)vfs_seek(f, -100, VFS_SEEK_CUR));
    vfs_file_close(f);

    tarfs_destroy(fs);
}

static void test_tarfs_rejects(void)
{
    setup();
    static uint8_t junk[2048];

    memset(junk, 'j', sizeof(junk));
    TEST_ASSERT(tarfs_create(junk, sizeof(junk)) == NULL);
    TEST_ASSERT(tarfs_create(junk, 100) == NULL);

    /* A truncated archive keeps the entries that are complete. */
    static uint8_t tar[8192];
    size_t size = make_tar(tar);
    vfs_fs_t *fs = tarfs_create(tar, 512 + 512 + 512 + 100);

    (void)size;
    TEST_ASSERT(fs != NULL);
    tarfs_destroy(fs);
}

static void test_mount_priority(void)
{
    setup();
    vfs_fs_t *root = ramfs_create();
    vfs_fs_t *tmp = ramfs_create();
    vfs_file_t *f;
    vfs_stat_t st;

    vfs_mount("/", root);
    vfs_mount("/tmp", tmp);
    TEST_ASSERT_EQ_INT(-K_EEXIST, vfs_mount("/tmp/", tmp));

    TEST_ASSERT_EQ_INT(0, vfs_open("/tmp/in_tmp", VFS_O_WRONLY | VFS_O_CREAT, 0, &f));
    vfs_file_close(f);
    TEST_ASSERT_EQ_INT(0, vfs_open("/tmpfile", VFS_O_WRONLY | VFS_O_CREAT, 0, &f));
    vfs_file_close(f);

    /* "/tmpfile" belongs to the root file system, not to /tmp. */
    TEST_ASSERT_EQ_INT(0, tmp->stat(tmp, "in_tmp", &st));
    TEST_ASSERT_EQ_INT(0, root->stat(root, "tmpfile", &st));
    TEST_ASSERT_EQ_INT(-K_ENOENT, root->stat(root, "tmp/in_tmp", &st));
    TEST_ASSERT_EQ_INT(0, vfs_stat("/tmp", &st));
    TEST_ASSERT_EQ_INT(-K_EEXIST, vfs_mkdir("/tmp", 0755));

    ramfs_destroy(root);
    ramfs_destroy(tmp);
}

int main(void)
{
    TEST_RUN(test_normalize);
    TEST_RUN(test_cwd);
    TEST_RUN(test_ramfs_files);
    TEST_RUN(test_ramfs_directories);
    TEST_RUN(test_tarfs);
    TEST_RUN(test_tarfs_rejects);
    TEST_RUN(test_mount_priority);

    return TEST_REPORT("vfs");
}
