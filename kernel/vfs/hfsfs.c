/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hfsfs.h"

#include "kernel/fs/volume.h"
#include "kernel/lib/k_errno.h"
#include "kernel/lib/k_memory.h"
#include "kernel/lib/k_string.h"
#include "kernel/memory/heap.h"

typedef struct
{
    hfs_entry_t entry;
} open_file_t;

/* "a/b" -> "a:b" (HFS separator); a name that already holds ':' cannot be addressed. */
static int to_hfs_path(const char *path, char *out, size_t size)
{
    size_t n = 0;

    for (; *path != '\0'; path++)
    {
        if (n + 1 >= size || *path == ':')
            return -1;

        out[n++] = *path == '/' ? ':' : *path;
    }

    out[n] = '\0';

    return 0;
}

static int32_t lookup(const char *path, hfs_entry_t *entry)
{
    hfs_volume_t *volume = volume_get();
    char hfs_path[VFS_PATH_MAX];

    if (volume == NULL)
        return path[0] == '\0' ? 0 : -K_ENOENT;

    if (to_hfs_path(path, hfs_path, sizeof(hfs_path)) != 0)
        return -K_ENOENT;

    return hfs_lookup(volume, hfs_path, entry) == 0 ? 0 : -K_ENOENT;
}

static void fill_stat(const hfs_entry_t *entry, vfs_stat_t *stat)
{
    stat->mode = entry->is_folder ? (VFS_S_IFDIR | 0555) : (VFS_S_IFREG | 0444);
    stat->size = entry->is_folder ? 0 : entry->data_length;
    stat->inode = entry->id;
    stat->mtime = 0;
}

static int32_t file_read(vfs_file_t *file, uint64_t pos, void *buffer, uint32_t count)
{
    open_file_t *open = file->priv;

    if (open->entry.is_folder)
        return -K_EISDIR;

    if (pos >= open->entry.data_length)
        return 0;

    int got = hfs_read(volume_get(), &open->entry, HFS_FORK_DATA, (uint32_t)pos, buffer, count);

    return got < 0 ? -K_EIO : got;
}

static int32_t file_stat(vfs_file_t *file, vfs_stat_t *stat)
{
    open_file_t *open = file->priv;

    fill_stat(&open->entry, stat);

    return 0;
}

typedef struct
{
    uint32_t index;
    uint32_t seen;
    vfs_dirent_t *out;
    int found;
} list_search_t;

static int list_visit(const hfs_entry_t *entry, void *context)
{
    list_search_t *search = context;

    if (search->seen++ != search->index)
        return 0;

    for (size_t i = 0; i < sizeof(entry->name) && i < VFS_NAME_MAX; i++)
    {
        char c = entry->name[i];

        search->out->name[i] = c == '/' ? ':' : c;

        if (c == '\0')
            break;
    }

    search->out->name[VFS_NAME_MAX] = '\0';
    search->out->inode = entry->id;
    search->out->type = entry->is_folder ? VFS_DT_DIR : VFS_DT_REG;
    search->found = 1;

    return 1;
}

static int32_t file_readdir(vfs_file_t *file, uint32_t index, vfs_dirent_t *out)
{
    open_file_t *open = file->priv;

    if (!open->entry.is_folder)
        return -K_ENOTDIR;

    if (index < 2)
    {
        k_strcpy(out->name, index == 0 ? "." : "..");
        out->inode = open->entry.id;
        out->type = VFS_DT_DIR;

        return 0;
    }

    hfs_volume_t *volume = volume_get();

    if (volume == NULL)
        return 1;

    list_search_t search = {index - 2, 0, out, 0};

    if (hfs_list(volume, open->entry.id, list_visit, &search) != 0 && !search.found)
        return -K_EIO;

    return search.found ? 0 : 1;
}

static void file_close(vfs_file_t *file)
{
    kfree(file->priv);
}

static const vfs_file_ops_t file_ops = {
    .read = file_read,
    .readdir = file_readdir,
    .stat = file_stat,
    .close = file_close,
};

static int32_t hfsfs_open(vfs_fs_t *fs, const char *path, uint32_t flags,
                          uint32_t mode, vfs_file_t **file)
{
    (void)fs;
    (void)mode;

    if ((flags & VFS_O_ACCMODE) != VFS_O_RDONLY || (flags & (VFS_O_CREAT | VFS_O_TRUNC)))
        return -K_EROFS;

    open_file_t *open = kmalloc(sizeof(*open));

    if (open == NULL)
        return -K_ENOMEM;

    k_memset(open, 0, sizeof(*open));

    int32_t status = lookup(path, &open->entry);

    if (status == 0 && path[0] == '\0')
    {
        /* The root exists even without a volume (then it is empty). */
        open->entry.is_folder = true;
    }

    if (status == 0 && !open->entry.is_folder && (flags & VFS_O_DIRECTORY))
        status = -K_ENOTDIR;

    if (status != 0)
    {
        kfree(open);

        return status;
    }

    *file = vfs_file_new(&file_ops, open, flags);

    if (*file == NULL)
    {
        kfree(open);

        return -K_ENOMEM;
    }

    return 0;
}

static int32_t hfsfs_stat(vfs_fs_t *fs, const char *path, vfs_stat_t *stat)
{
    hfs_entry_t entry;
    int32_t status = lookup(path, &entry);

    (void)fs;

    if (status != 0)
        return status;

    if (path[0] == '\0')
        entry.is_folder = true;

    fill_stat(&entry, stat);

    return 0;
}

vfs_fs_t *hfsfs_create(void)
{
    vfs_fs_t *fs = kmalloc(sizeof(*fs));

    if (fs == NULL)
        return NULL;

    k_memset(fs, 0, sizeof(*fs));
    fs->open = hfsfs_open;
    fs->stat = hfsfs_stat;

    return fs;
}
