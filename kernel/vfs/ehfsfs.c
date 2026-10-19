/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ehfsfs.h"

#include "kernel/fs/ehfs/catalog.h"
#include "kernel/fs/ehfs/file.h"
#include "kernel/fs/gpt.h"
#include "kernel/lib/k_errno.h"
#include "kernel/lib/k_memory.h"
#include "kernel/lib/k_string.h"
#include "kernel/memory/heap.h"

/*
 * Paths inside the volume are its names as they are (UTF-8; eHFS names have
 * no '/'), compared the eHFS way (NFC, without regard to case). Every open
 * file remembers the volume's generation: after an unmount or another mount
 * it fails with EIO instead of reaching a different volume.
 */

typedef struct
{
    uint64_t cnid;          /* 0: the root while no volume is mounted */
    bool is_dir;
    uint32_t generation;
} open_file_t;

static ehfs_t *volume;
static uint32_t generation;
static bool mac_running;
static ehfsfs_clock_fn clock_now;

/* An entry is large (its name and fork descriptors): one for each step of the work, not on the stack. */
static ehfs_entry_t found, other;

static int32_t to_errno(int r)
{
    switch (r)
    {
    case EHFS_OK:
        return 0;
    case EHFS_ENOENT:
        return -K_ENOENT;
    case EHFS_EEXIST:
        return -K_EEXIST;
    case EHFS_ENOTDIR:
        return -K_ENOTDIR;
    case EHFS_EISDIR:
        return -K_EISDIR;
    case EHFS_ENOTEMPTY:
        return -K_ENOTEMPTY;
    case EHFS_ENOSPC:
        return -K_ENOSPC;
    case EHFS_EROFS:
        return -K_EROFS;
    case EHFS_ENAME:
    case EHFS_EINVAL:
        return -K_EINVAL;
    default:
        return -K_EIO;
    }
}

static uint64_t now(void)
{
    return clock_now != NULL ? clock_now() : 0;
}

/* Whether the file tree may change the volume now. */
static int32_t writable(void)
{
    if (volume == NULL)
        return -K_EROFS;

    return volume->read_only || mac_running ? -K_EROFS : 0;
}

/* The item at the first length bytes of path ("" = the root). */
static int32_t walk(const char *path, size_t length, ehfs_entry_t *entry)
{
    if (volume == NULL)
        return -K_ENOENT;

    int r = ehfs_cat_get(volume, EHFS_CNID_ROOT, entry);
    size_t at = 0;

    while (r == EHFS_OK && at < length)
    {
        size_t end = at;

        while (end < length && path[end] != '/')
            end++;

        if (!entry->is_dir)
            return -K_ENOTDIR;

        if (end - at > EHFS_NAME_MAX)
            return -K_ENAMETOOLONG;

        r = ehfs_cat_lookup(volume, entry->cnid, (const uint8_t *)path + at, (uint32_t)(end - at), entry);
        at = end + 1;
    }

    return to_errno(r);
}

/*
 * The folder that would hold the last part of path: *parent = its CNID,
 * *name and *length = the last part. The root itself has no such folder.
 */
static int32_t walk_parent(const char *path, uint64_t *parent, const char **name, uint32_t *length)
{
    size_t total = k_strlen(path);
    size_t cut = total;

    if (total == 0)
        return -K_EPERM;

    while (cut > 0 && path[cut - 1] != '/')
        cut--;

    if (total - cut > EHFS_NAME_MAX)
        return -K_ENAMETOOLONG;

    int32_t status = walk(path, cut > 0 ? cut - 1 : 0, &other);

    if (status != 0)
        return status;

    if (!other.is_dir)
        return -K_ENOTDIR;

    *parent = other.cnid;
    *name = path + cut;
    *length = (uint32_t)(total - cut);

    return 0;
}

static uint64_t data_length(const ehfs_entry_t *entry)
{
    return ehfs_le64(entry->data_fork + FORK_LENGTH);
}

static void fill_stat(const ehfs_entry_t *entry, vfs_stat_t *stat)
{
    uint32_t write = (entry->flags & 1u) ? 0 : 0200; /* a locked item cannot be written */

    stat->mode = entry->is_dir ? (VFS_S_IFDIR | 0555 | write) : (VFS_S_IFREG | 0444 | write);
    stat->size = entry->is_dir ? 0 : data_length(entry);
    stat->inode = (uint32_t)entry->cnid;

    uint64_t seconds = entry->times[1] / 1000000000u;

    stat->mtime = seconds > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)seconds;
}

static void fill_root_without_volume(vfs_stat_t *stat)
{
    stat->mode = VFS_S_IFDIR | 0555;
    stat->size = 0;
    stat->inode = EHFS_CNID_ROOT;
    stat->mtime = 0;
}

/* The open file's item, now. */
static int32_t current(const open_file_t *open, ehfs_entry_t *entry)
{
    if (open->generation != generation || volume == NULL)
        return -K_EIO;

    return to_errno(ehfs_cat_get(volume, open->cnid, entry));
}

static int32_t file_read(vfs_file_t *file, uint64_t pos, void *buffer, uint32_t count)
{
    open_file_t *open = file->priv;

    if (open->is_dir)
        return -K_EISDIR;

    if (open->generation != generation || volume == NULL)
        return -K_EIO;

    if (count > 0x7FFFFFFFu)
        count = 0x7FFFFFFFu;

    uint32_t done = 0;
    int r = ehfs_read(volume, open->cnid, EHFS_DATA_FORK, pos, buffer, count, &done);

    return r == EHFS_OK ? (int32_t)done : to_errno(r);
}

static int32_t file_write(vfs_file_t *file, uint64_t pos, const void *buffer, uint32_t count)
{
    open_file_t *open = file->priv;

    if (open->is_dir)
        return -K_EISDIR;

    int32_t status = writable();

    if (status != 0)
        return status;

    if (file->flags & VFS_O_APPEND)
    {
        status = current(open, &found);

        if (status != 0)
            return status;

        pos = data_length(&found);
    }
    else if (open->generation != generation)
    {
        return -K_EIO;
    }

    if (count > 0x7FFFFFFFu)
        count = 0x7FFFFFFFu;

    int r = ehfs_write(volume, open->cnid, EHFS_DATA_FORK, pos, buffer, count, now());

    if (r != EHFS_OK)
        return to_errno(r);

    if (file->flags & VFS_O_APPEND)
        file->pos = pos; /* vfs_write adds count */

    return (int32_t)count;
}

static int32_t file_truncate(vfs_file_t *file, uint64_t size)
{
    open_file_t *open = file->priv;

    if (open->is_dir)
        return -K_EISDIR;

    int32_t status = writable();

    if (status != 0)
        return status;

    if (open->generation != generation)
        return -K_EIO;

    return to_errno(ehfs_set_length(volume, open->cnid, EHFS_DATA_FORK, size, now()));
}

static int32_t file_stat(vfs_file_t *file, vfs_stat_t *stat)
{
    open_file_t *open = file->priv;

    if (open->cnid == 0)
    {
        fill_root_without_volume(stat);

        return 0;
    }

    int32_t status = current(open, &found);

    if (status == 0)
        fill_stat(&found, stat);

    return status;
}

typedef struct
{
    uint32_t index;
    uint32_t seen;
    vfs_dirent_t *out;
    bool found;
} list_search_t;

static int list_visit(void *context, const ehfs_entry_t *entry)
{
    list_search_t *search = context;

    if (search->seen++ != search->index)
        return 0;

    uint32_t length = entry->name_length < VFS_NAME_MAX ? entry->name_length : VFS_NAME_MAX;

    k_memcpy(search->out->name, entry->name, length);
    search->out->name[length] = '\0';
    search->out->inode = (uint32_t)entry->cnid;
    search->out->type = entry->is_dir ? VFS_DT_DIR : VFS_DT_REG;
    search->found = true;

    return 1;
}

static int32_t file_readdir(vfs_file_t *file, uint32_t index, vfs_dirent_t *out)
{
    open_file_t *open = file->priv;

    if (!open->is_dir)
        return -K_ENOTDIR;

    if (index < 2)
    {
        k_strcpy(out->name, index == 0 ? "." : "..");
        out->inode = (uint32_t)(open->cnid != 0 ? open->cnid : EHFS_CNID_ROOT);
        out->type = VFS_DT_DIR;

        return 0;
    }

    if (open->cnid == 0)
        return 1;

    if (open->generation != generation || volume == NULL)
        return -K_EIO;

    /* Each call walks the folder from its start: simple, and folders are seldom huge. */
    list_search_t search = {index - 2, 0, out, false};
    int r = ehfs_cat_list(volume, open->cnid, list_visit, &search);

    if (r != EHFS_OK && !search.found)
        return to_errno(r);

    return search.found ? 0 : 1;
}

static void file_close(vfs_file_t *file)
{
    kfree(file->priv);
}

static const vfs_file_ops_t file_ops = {
    .read = file_read,
    .write = file_write,
    .readdir = file_readdir,
    .stat = file_stat,
    .truncate = file_truncate,
    .close = file_close,
};

static int32_t ehfsfs_open(vfs_fs_t *fs, const char *path, uint32_t flags, uint32_t mode, vfs_file_t **file)
{
    bool for_writing = (flags & VFS_O_ACCMODE) != VFS_O_RDONLY;
    int32_t status;

    (void)fs;
    (void)mode;

    if (volume == NULL)
    {
        /* The root exists even without a volume (then it is empty). */
        if (path[0] != '\0')
            return (flags & VFS_O_CREAT) ? -K_EROFS : -K_ENOENT;

        if (for_writing)
            return -K_EISDIR;

        found.cnid = 0;
        found.is_dir = true;
        status = 0;
    }
    else
    {
        status = walk(path, k_strlen(path), &found);

        if (status == -K_ENOENT && (flags & VFS_O_CREAT))
        {
            uint64_t parent;
            const char *name;
            uint32_t length;

            status = walk_parent(path, &parent, &name, &length);

            if (status == 0)
                status = writable();

            if (status == 0)
                status = to_errno(ehfs_create(volume, parent, (const uint8_t *)name, length, false, now(), &found));
        }
        else if (status == 0 && (flags & VFS_O_CREAT) && (flags & VFS_O_EXCL))
        {
            status = -K_EEXIST;
        }
    }

    if (status != 0)
        return status;

    if (found.is_dir && for_writing)
        return -K_EISDIR;

    if (!found.is_dir && (flags & VFS_O_DIRECTORY))
        return -K_ENOTDIR;

    if (!found.is_dir && for_writing)
    {
        status = writable();

        if (status == 0 && (found.flags & 1u))
            status = -K_EACCES; /* locked */

        if (status == 0 && (flags & VFS_O_TRUNC) && data_length(&found) != 0)
            status = to_errno(ehfs_set_length(volume, found.cnid, EHFS_DATA_FORK, 0, now()));

        if (status != 0)
            return status;
    }

    open_file_t *open = kmalloc(sizeof(*open));

    if (open == NULL)
        return -K_ENOMEM;

    open->cnid = found.cnid;
    open->is_dir = found.is_dir;
    open->generation = generation;
    *file = vfs_file_new(&file_ops, open, flags);

    if (*file == NULL)
    {
        kfree(open);

        return -K_ENOMEM;
    }

    return 0;
}

static int32_t ehfsfs_stat(vfs_fs_t *fs, const char *path, vfs_stat_t *stat)
{
    (void)fs;

    if (volume == NULL)
    {
        if (path[0] != '\0')
            return -K_ENOENT;

        fill_root_without_volume(stat);

        return 0;
    }

    int32_t status = walk(path, k_strlen(path), &found);

    if (status == 0)
        fill_stat(&found, stat);

    return status;
}

static int32_t ehfsfs_mkdir(vfs_fs_t *fs, const char *path, uint32_t mode)
{
    uint64_t parent;
    const char *name;
    uint32_t length;

    (void)fs;
    (void)mode;

    int32_t status = writable();

    if (status == 0)
        status = walk_parent(path, &parent, &name, &length);

    if (status == -K_EPERM)
        return -K_EEXIST; /* the root */

    if (status != 0)
        return status;

    return to_errno(ehfs_create(volume, parent, (const uint8_t *)name, length, true, now(), &found));
}

static int32_t remove_item(const char *path, bool want_dir)
{
    uint64_t parent;
    const char *name;
    uint32_t length;
    int32_t status = writable();

    if (status == 0)
        status = walk_parent(path, &parent, &name, &length);

    if (status == 0)
        status = to_errno(ehfs_cat_lookup(volume, parent, (const uint8_t *)name, length, &found));

    if (status != 0)
        return status;

    if (want_dir && !found.is_dir)
        return -K_ENOTDIR;

    if (!want_dir && found.is_dir)
        return -K_EISDIR;

    if (found.flags & 1u)
        return -K_EPERM; /* locked */

    return to_errno(ehfs_remove(volume, parent, (const uint8_t *)name, length, now()));
}

static int32_t ehfsfs_unlink(vfs_fs_t *fs, const char *path)
{
    (void)fs;

    return remove_item(path, false);
}

static int32_t ehfsfs_rmdir(vfs_fs_t *fs, const char *path)
{
    (void)fs;

    return remove_item(path, true);
}

static int32_t ehfsfs_rename(vfs_fs_t *fs, const char *from, const char *to)
{
    uint64_t parent, new_parent;
    const char *name, *new_name;
    uint32_t length, new_length;

    (void)fs;

    int32_t status = writable();

    if (status == 0)
        status = walk_parent(from, &parent, &name, &length);

    if (status == 0)
        status = to_errno(ehfs_cat_lookup(volume, parent, (const uint8_t *)name, length, &found));

    if (status == 0)
        status = walk_parent(to, &new_parent, &new_name, &new_length);

    if (status != 0)
        return status;

    uint64_t moving = found.cnid;
    bool moving_dir = found.is_dir;
    int r = ehfs_cat_lookup(volume, new_parent, (const uint8_t *)new_name, new_length, &other);

    if (r == EHFS_OK && other.cnid != moving)
    {
        /* Replace a file by a file, an empty folder by a folder (not in one step: see docs/ehfs.md). */
        if (other.is_dir != moving_dir)
            return other.is_dir ? -K_EISDIR : -K_ENOTDIR;

        status = to_errno(ehfs_remove(volume, new_parent, (const uint8_t *)new_name, new_length, now()));

        if (status != 0)
            return status;
    }
    else if (r != EHFS_OK && r != EHFS_ENOENT)
    {
        return to_errno(r);
    }

    return to_errno(ehfs_move(volume, parent, (const uint8_t *)name, length, new_parent,
                              (const uint8_t *)new_name, new_length, now()));
}

int ehfsfs_mount(block_device_t *device, uint64_t start_sector, bool read_only)
{
    ehfs_t *fs = kmalloc(sizeof(*fs));

    if (fs == NULL)
        return EHFS_ENOSPC;

    int r = ehfs_mount(fs, device, start_sector, read_only || device->ops->write == NULL);

    if (r != EHFS_OK)
    {
        kfree(fs);

        return r;
    }

    ehfsfs_unmount();
    volume = fs;

    return EHFS_OK;
}

/* Whether device holds an eHFS volume at start_sector (only its superblock is read). */
static bool holds_volume(block_device_t *device, uint64_t start_sector)
{
    uint8_t sector[512];

    if (device->block_size != 512 || start_sector + 3 > device->block_count)
        return false;

    /* The superblock starts at byte 1024 of the volume (docs/ehfs.md 3.1): its magic is in sector 2. */
    if (block_read(device, start_sector + 2, 1, sector) != 0)
        return false;

    return k_memcmp(sector + SB_MAGIC, EHFS_MAGIC, sizeof(EHFS_MAGIC) - 1) == 0;
}

int ehfsfs_mount_any(void)
{
    static gpt_partition_t partitions[GPT_MAX_PARTITIONS];

    for (unsigned int i = 0; i < block_device_count(); i++)
    {
        block_device_t *device = block_device_get(i);

        if (holds_volume(device, 0))
        {
            if (ehfsfs_mount(device, 0, false) == EHFS_OK)
                return 0;

            continue;
        }

        int count = gpt_read(device, partitions, GPT_MAX_PARTITIONS);

        for (int p = 0; p < count; p++)
        {
            if (holds_volume(device, partitions[p].start) &&
                ehfsfs_mount(device, partitions[p].start, false) == EHFS_OK)
                return 0;
        }
    }

    return -1;
}

void ehfsfs_unmount(void)
{
    if (volume == NULL)
        return;

    ehfs_unmount(volume);
    kfree(volume);
    volume = NULL;
    generation++;
}

ehfs_t *ehfsfs_volume(void)
{
    return volume;
}

void ehfsfs_set_mac_running(bool running)
{
    mac_running = running;
}

vfs_fs_t *ehfsfs_create(ehfsfs_clock_fn clock)
{
    vfs_fs_t *fs = kmalloc(sizeof(*fs));

    if (fs == NULL)
        return NULL;

    k_memset(fs, 0, sizeof(*fs));
    fs->open = ehfsfs_open;
    fs->stat = ehfsfs_stat;
    fs->mkdir = ehfsfs_mkdir;
    fs->unlink = ehfsfs_unlink;
    fs->rmdir = ehfsfs_rmdir;
    fs->rename = ehfsfs_rename;
    clock_now = clock;

    return fs;
}
