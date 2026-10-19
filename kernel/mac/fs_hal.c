/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "fs_hal.h"

#include "emu/mac/fs_paths.h"
#include "kernel/lib/k_memory.h"
#include "kernel/lib/k_string.h"
#include "kernel/memory/heap.h"
#include "kernel/vfs/vfs.h"

static mac_fs_paths_t *paths;
static bool without_ehfs;

/* "/" + path (fs_paths' paths have no leading '/'). */
static int absolute(const char *path, char *out, uint32_t size)
{
    uint32_t n = (uint32_t)k_strlen(path);

    if (n + 2 > size)
        return -1;
    out[0] = '/';
    k_memcpy(out + 1, path, n + 1);
    return 0;
}

/* Devices and the kernel's /proc are no files for the Mac; nor /ehfs when it is a volume of its own. */
static bool left_out(const char *dir, const char *name)
{
    return dir[0] == '\0' &&
           (k_strcmp(name, "dev") == 0 || k_strcmp(name, "proc") == 0 || (without_ehfs && k_strcmp(name, "ehfs") == 0));
}

static int hal_entry(void *ctx, const char *path, uint32_t index, char *name, uint32_t size)
{
    char full[MAC_FS_PATH_MAX + 2];
    vfs_file_t *dir;
    vfs_dirent_t e;
    uint32_t seen = 0;
    int result = -1;

    (void)ctx;
    if (absolute(path, full, sizeof full) != 0 || vfs_open(full, VFS_O_RDONLY | VFS_O_DIRECTORY, 0, &dir) != 0)
        return -1;
    while (vfs_readdir(dir, &e) == 0)
    {
        if (k_strcmp(e.name, ".") == 0 || k_strcmp(e.name, "..") == 0 || left_out(path, e.name))
            continue;
        if (seen++ == index)
        {
            uint32_t n = (uint32_t)k_strlen(e.name);

            if (n >= size)
                n = size - 1;
            k_memcpy(name, e.name, n);
            name[n] = '\0';
            result = 0;
            break;
        }
    }
    vfs_file_close(dir);
    return result;
}

static int hal_stat(void *ctx, const char *path, bool *is_dir, uint32_t *size, uint32_t *mtime)
{
    char full[MAC_FS_PATH_MAX + 2];
    vfs_stat_t st;

    (void)ctx;
    if (absolute(path, full, sizeof full) != 0 || vfs_stat(full, &st) != 0)
        return -1;
    *is_dir = (st.mode & VFS_S_IFMT) == VFS_S_IFDIR;
    *size = st.size > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)st.size;
    *mtime = st.mtime;
    return 0;
}

static int32_t hal_read(void *ctx, const char *path, uint32_t offset, void *buffer, uint32_t length)
{
    char full[MAC_FS_PATH_MAX + 2];
    vfs_file_t *file;

    (void)ctx;
    if (absolute(path, full, sizeof full) != 0 || vfs_open(full, VFS_O_RDONLY, 0, &file) != 0)
        return -1;

    int32_t n = vfs_seek(file, offset, VFS_SEEK_SET) == (int64_t)offset ? vfs_read(file, buffer, length) : -1;

    vfs_file_close(file);
    return n < 0 ? -1 : n;
}

static const mac_fs_host_t hal_host = {NULL, hal_entry, hal_stat, hal_read};

const mac_fs_ops_t *mac_fs_hal(bool leave_out_ehfs)
{
    without_ehfs = leave_out_ehfs;
    if (paths == NULL)
    {
        paths = kmalloc(sizeof *paths);
        if (paths == NULL)
            return NULL;
        mac_fs_paths_init(paths, &hal_host, "HAL");
    }
    return &paths->ops;
}
