/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "vfs.h"

#include "kernel/lib/k_errno.h"
#include "kernel/lib/k_memory.h"
#include "kernel/lib/k_string.h"
#include "kernel/memory/heap.h"

#define MAX_MOUNTS 8

typedef struct
{
    char path[VFS_PATH_MAX]; /* "/" or "/tmp" (no trailing slash) */
    size_t length;
    vfs_fs_t *fs;
} mount_t;

static mount_t mounts[MAX_MOUNTS];
static int mount_count;

/* The current directory: what a path that does not start with "/" is relative to. Always normalised. */
static char cwd[VFS_PATH_MAX] = "/";

void vfs_reset(void)
{
    mount_count = 0;
    cwd[0] = '/';
    cwd[1] = '\0';
}

/* Normalise an absolute path (see vfs_normalize). */
static int32_t normalize_absolute(const char *path, char *out)
{
    size_t length = 0;

    out[0] = '\0';

    while (*path != '\0')
    {
        while (*path == '/')
            path++;

        if (*path == '\0')
            break;

        const char *start = path;

        while (*path != '\0' && *path != '/')
            path++;

        size_t part = (size_t)(path - start);

        if (part == 1 && start[0] == '.')
            continue;

        if (part == 2 && start[0] == '.' && start[1] == '.')
        {
            while (length > 0 && out[length - 1] != '/')
                length--;

            if (length > 0)
                length--; /* the slash in front of the component */

            out[length] = '\0';
            continue;
        }

        if (part > VFS_NAME_MAX)
            return -K_ENAMETOOLONG;

        if (length + 1 + part >= VFS_PATH_MAX)
            return -K_ENAMETOOLONG;

        out[length++] = '/';

        for (size_t i = 0; i < part; i++)
            out[length++] = start[i];

        out[length] = '\0';
    }

    if (length == 0)
    {
        out[0] = '/';
        out[1] = '\0';
    }

    return 0;
}

int32_t vfs_normalize(const char *path, char *out)
{
    if (path == NULL || path[0] == '\0')
        return -K_ENOENT;

    if (path[0] == '/')
        return normalize_absolute(path, out);

    /* Relative: in front of it goes the current directory. */
    char joined[2 * VFS_PATH_MAX];
    size_t cwd_length = k_strlen(cwd);
    size_t path_length = k_strlen(path);

    if (cwd_length + 1 + path_length >= sizeof(joined))
        return -K_ENAMETOOLONG;

    k_memcpy(joined, cwd, cwd_length);
    joined[cwd_length] = '/';
    k_memcpy(joined + cwd_length + 1, path, path_length + 1);

    return normalize_absolute(joined, out);
}

const char *vfs_getcwd(void)
{
    return cwd;
}

void vfs_set_cwd(const char *path)
{
    k_strcpy(cwd, path);
}

int32_t vfs_chdir(const char *path)
{
    char normal[VFS_PATH_MAX];
    vfs_stat_t stat;
    int32_t status = vfs_normalize(path, normal);

    if (status != 0)
        return status;

    status = vfs_stat(normal, &stat);

    if (status != 0)
        return status;

    if ((stat.mode & VFS_S_IFMT) != VFS_S_IFDIR)
        return -K_ENOTDIR;

    k_strcpy(cwd, normal);

    return 0;
}

int32_t vfs_mount(const char *path, vfs_fs_t *fs)
{
    char normal[VFS_PATH_MAX];
    int32_t status = vfs_normalize(path, normal);

    if (status != 0)
        return status;

    if (mount_count == MAX_MOUNTS)
        return -K_ENOMEM;

    for (int i = 0; i < mount_count; i++)
    {
        if (k_strcmp(mounts[i].path, normal) == 0)
            return -K_EEXIST;
    }

    k_strcpy(mounts[mount_count].path, normal);
    mounts[mount_count].length = k_strlen(normal);
    mounts[mount_count].fs = fs;
    mount_count++;

    return 0;
}

/*
 * Find the file system whose mount point is the longest prefix of the
 * normalised path, and the path inside it.
 */
static int32_t find_mount(const char *path, vfs_fs_t **fs, const char **inner)
{
    char normal[VFS_PATH_MAX];
    int32_t status = vfs_normalize(path, normal);

    if (status != 0)
        return status;

    int best = -1;
    size_t best_length = 0;

    for (int i = 0; i < mount_count; i++)
    {
        size_t length = mounts[i].length;
        int is_root = length == 1;

        if (!is_root)
        {
            if (k_strncmp(normal, mounts[i].path, length) != 0)
                continue;

            if (normal[length] != '\0' && normal[length] != '/')
                continue;
        }

        if (best < 0 || length > best_length)
        {
            best = i;
            best_length = length;
        }
    }

    if (best < 0)
        return -K_ENOENT;

    /* normal lives on this stack frame: callers need a stable copy. */
    static char inner_path[VFS_PATH_MAX];
    const char *rest = normal + (best_length == 1 ? 1 : best_length);

    if (*rest == '/')
        rest++;

    k_strcpy(inner_path, rest);
    *fs = mounts[best].fs;
    *inner = inner_path;

    return 0;
}

int32_t vfs_open(const char *path, uint32_t flags, uint32_t mode, vfs_file_t **file)
{
    vfs_fs_t *fs;
    const char *inner;
    int32_t status = find_mount(path, &fs, &inner);

    if (status != 0)
        return status;

    if (fs->open == NULL)
        return -K_ENOSYS;

    status = fs->open(fs, inner, flags, mode, file);

    if (status != 0)
        return status;

    (*file)->flags = flags;
    (*file)->refcount = 1;

    return 0;
}

int32_t vfs_stat(const char *path, vfs_stat_t *stat)
{
    vfs_fs_t *fs;
    const char *inner;
    int32_t status = find_mount(path, &fs, &inner);

    if (status != 0)
        return status;

    return fs->stat != NULL ? fs->stat(fs, inner, stat) : -K_ENOSYS;
}

int32_t vfs_mkdir(const char *path, uint32_t mode)
{
    vfs_fs_t *fs;
    const char *inner;
    int32_t status = find_mount(path, &fs, &inner);

    if (status != 0)
        return status;

    if (inner[0] == '\0')
        return -K_EEXIST;

    return fs->mkdir != NULL ? fs->mkdir(fs, inner, mode) : -K_EROFS;
}

int32_t vfs_unlink(const char *path)
{
    vfs_fs_t *fs;
    const char *inner;
    int32_t status = find_mount(path, &fs, &inner);

    if (status != 0)
        return status;

    return fs->unlink != NULL ? fs->unlink(fs, inner) : -K_EROFS;
}

int32_t vfs_rmdir(const char *path)
{
    vfs_fs_t *fs;
    const char *inner;
    int32_t status = find_mount(path, &fs, &inner);

    if (status != 0)
        return status;

    return fs->rmdir != NULL ? fs->rmdir(fs, inner) : -K_EROFS;
}

int32_t vfs_rename(const char *from, const char *to)
{
    vfs_fs_t *fs_from;
    vfs_fs_t *fs_to;
    char from_path[VFS_PATH_MAX];
    const char *inner;
    int32_t status = find_mount(from, &fs_from, &inner);

    if (status != 0)
        return status;

    k_strcpy(from_path, inner);

    status = find_mount(to, &fs_to, &inner);

    if (status != 0)
        return status;

    if (fs_from != fs_to)
        return -K_EINVAL; /* EXDEV in Linux; programs fall back to copying */

    return fs_from->rename != NULL ? fs_from->rename(fs_from, from_path, inner)
                                   : -K_EROFS;
}

vfs_file_t *vfs_file_new(const vfs_file_ops_t *ops, void *priv, uint32_t flags)
{
    vfs_file_t *file = kmalloc(sizeof(*file));

    if (file == NULL)
        return NULL;

    file->ops = ops;
    file->priv = priv;
    file->pos = 0;
    file->flags = flags;
    file->refcount = 1;
    file->seekable = 1;

    return file;
}

vfs_file_t *vfs_file_ref(vfs_file_t *file)
{
    file->refcount++;

    return file;
}

void vfs_file_close(vfs_file_t *file)
{
    if (--file->refcount > 0)
        return;

    if (file->ops->close != NULL)
        file->ops->close(file);

    kfree(file);
}

int32_t vfs_read(vfs_file_t *file, void *buffer, uint32_t count)
{
    if ((file->flags & VFS_O_ACCMODE) == VFS_O_WRONLY)
        return -K_EBADF;

    if (file->ops->read == NULL)
        return -K_EISDIR;

    int32_t n = file->ops->read(file, file->pos, buffer, count);

    if (n > 0)
        file->pos += (uint32_t)n;

    return n;
}

int32_t vfs_write(vfs_file_t *file, const void *buffer, uint32_t count)
{
    if ((file->flags & VFS_O_ACCMODE) == VFS_O_RDONLY)
        return -K_EBADF;

    if (file->ops->write == NULL)
        return -K_EBADF;

    int32_t n = file->ops->write(file, file->pos, buffer, count);

    if (n > 0)
        file->pos += (uint32_t)n;

    return n;
}

int32_t vfs_fstat(vfs_file_t *file, vfs_stat_t *stat)
{
    return file->ops->stat != NULL ? file->ops->stat(file, stat) : -K_EINVAL;
}

int64_t vfs_seek(vfs_file_t *file, int64_t offset, int whence)
{
    if (!file->seekable)
        return -K_ESPIPE;

    int64_t base;

    switch (whence)
    {
    case VFS_SEEK_SET:
        base = 0;
        break;

    case VFS_SEEK_CUR:
        base = (int64_t)file->pos;
        break;

    case VFS_SEEK_END:
    {
        vfs_stat_t stat;
        int32_t status = vfs_fstat(file, &stat);

        if (status != 0)
            return status;

        base = (int64_t)stat.size;
        break;
    }

    default:
        return -K_EINVAL;
    }

    if (base + offset < 0)
        return -K_EINVAL;

    file->pos = (uint64_t)(base + offset);

    return (int64_t)file->pos;
}

int32_t vfs_readdir(vfs_file_t *file, vfs_dirent_t *entry)
{
    if (file->ops->readdir == NULL)
        return -K_ENOTDIR;

    int32_t status = file->ops->readdir(file, (uint32_t)file->pos, entry);

    if (status == 0)
        file->pos++;

    return status;
}

int32_t vfs_ftruncate(vfs_file_t *file, uint64_t size)
{
    if ((file->flags & VFS_O_ACCMODE) == VFS_O_RDONLY)
        return -K_EBADF;

    return file->ops->truncate != NULL ? file->ops->truncate(file, size) : -K_EINVAL;
}

int32_t vfs_ioctl(vfs_file_t *file, uint32_t request, uint32_t arg)
{
    if (file->ops->ioctl == NULL)
        return -K_ENOTTY;

    return file->ops->ioctl(file, request, arg);
}

uint32_t vfs_poll(vfs_file_t *file, uint32_t events)
{
    if (file->ops->poll != NULL)
        return file->ops->poll(file, events);

    /* Plain files and directories never make anybody wait. */
    return events & (VFS_POLLIN | VFS_POLLOUT);
}
