/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * ehfs-cp: copy files in and out of an eHFS volume (docs/ehfs.md).
 *
 * Usage: ehfs-cp [--offset SECTORS] [--device] TARGET COMMAND ...
 *
 *   in HOST EHFS     copy a file or a folder (with everything in it) in; a file that exists is replaced
 *   out EHFS HOST    copy a file or a folder out
 *   ls [EHFS]        list a folder: kind, data and resource fork sizes, type/creator, name
 *   mkdir EHFS       make a folder
 *   rm EHFS          remove a file or an empty folder
 *
 * EHFS paths start at the volume's root: "/", "/Documents/notes.txt".
 *
 * The Mac's parts of a file travel as extended attributes on Linux (decision
 * E7; Linux wants a namespace, hence "user."): user.com.apple.ResourceFork
 * is the resource fork, user.com.apple.FinderInfo the 32 bytes of Finder
 * information (type, creator, flags ...). Other extended attributes are kept
 * as eHFS attributes under the same names (values up to 3 KiB).
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include "ehfs_tool.h"
#include "kernel/fs/ehfs/file.h"

#define RESOURCE_FORK_XATTR "user.com.apple.ResourceFork"
#define FINDER_INFO_XATTR "user.com.apple.FinderInfo"
#define CHUNK (1u << 20)

static ehfs_t fs;
static uint8_t chunk[CHUNK];
static int warnings;

#define S(x) (const uint8_t *)(x), (uint32_t)strlen(x)

static void warn(const char *what, const char *path)
{
    fprintf(stderr, "ehfs-cp: %s: %s\n", path, what);
    warnings++;
}

/* The item at an eHFS path ("/" is the root). */
static int resolve(const char *path, ehfs_entry_t *e)
{
    char copy[4096];
    int r = ehfs_cat_get(&fs, EHFS_CNID_ROOT, e);

    snprintf(copy, sizeof copy, "%s", path);
    for (char *part = strtok(copy, "/"); r == EHFS_OK && part != NULL; part = strtok(NULL, "/"))
    {
        if (!e->is_dir)
            return EHFS_ENOTDIR;
        r = ehfs_cat_lookup(&fs, e->cnid, S(part), e);
    }
    return r;
}

/* The folder an eHFS path's last part is in, and that part. */
static int resolve_parent(const char *path, ehfs_entry_t *folder, char *name, size_t size)
{
    char copy[4096];
    char *slash;

    snprintf(copy, sizeof copy, "%s", path);
    while (strlen(copy) > 1 && copy[strlen(copy) - 1] == '/')
        copy[strlen(copy) - 1] = '\0';
    slash = strrchr(copy, '/');
    const char *last = slash != NULL ? slash + 1 : copy;

    if (strlen(last) >= size)
        return EHFS_ENAME;
    memcpy(name, last, strlen(last) + 1);
    if (slash != NULL)
        *slash = '\0';
    if (name[0] == '\0')
        return EHFS_EINVAL;

    int r = resolve(slash != NULL ? copy : "/", folder);

    if (r == EHFS_OK && !folder->is_dir)
        r = EHFS_ENOTDIR;
    return r;
}

static uint64_t ns(const struct timespec *t)
{
    return (uint64_t)t->tv_sec * 1000000000ull + (uint64_t)t->tv_nsec;
}

static int copy_in(const char *host, const char *path);

static int copy_in_folder(const char *host, const char *path, const ehfs_entry_t *folder)
{
    DIR *d = opendir(host);
    struct dirent *de;
    int r = EHFS_OK;

    (void)folder;
    if (d == NULL)
    {
        warn(strerror(errno), host);
        return EHFS_OK;
    }
    while ((de = readdir(d)) != NULL && r == EHFS_OK)
    {
        char from[4096], to[4096];

        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        snprintf(from, sizeof from, "%s/%s", host, de->d_name);
        snprintf(to, sizeof to, "%s/%s", strcmp(path, "/") == 0 ? "" : path, de->d_name);
        r = copy_in(from, to);
    }
    closedir(d);
    return r;
}

static int copy_xattrs_in(const char *host, ehfs_entry_t *e)
{
    char names[8192];
    ssize_t n = llistxattr(host, names, sizeof names);

    for (ssize_t i = 0; i < n; i += (ssize_t)strlen(names + i) + 1)
    {
        const char *name = names + i;
        ssize_t size = lgetxattr(host, name, chunk, CHUNK);
        int r = EHFS_OK;

        if (size < 0)
        {
            warn("an extended attribute cannot be read", host);
            continue;
        }
        if (strcmp(name, RESOURCE_FORK_XATTR) == 0)
        {
            r = ehfs_write(&fs, e->cnid, EHFS_RESOURCE_FORK, 0, chunk, (uint32_t)size, e->times[1]);
        }
        else if (strcmp(name, FINDER_INFO_XATTR) == 0)
        {
            if (size == 32)
                memcpy(e->finder, chunk, 32);
            else
                warn("user.com.apple.FinderInfo is not 32 bytes: left out", host);
        }
        else if (size > (ssize_t)EHFS_ATTR_VALUE_MAX)
        {
            warn("an extended attribute is larger than 3 KiB: left out", host);
        }
        else
        {
            r = ehfs_attr_set(&fs, e->cnid, S(name), chunk, (uint32_t)size);
        }
        if (r != EHFS_OK)
            return r;
    }
    return EHFS_OK;
}

static int copy_in(const char *host, const char *path)
{
    struct stat st;
    ehfs_entry_t folder, e;
    char name[512];
    int r;

    if (lstat(host, &st) != 0)
    {
        warn(strerror(errno), host);
        return EHFS_OK;
    }
    if (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode))
    {
        warn("not a file or a folder: left out", host);
        return EHFS_OK;
    }
    if (strcmp(path, "/") == 0)
    {
        if (!S_ISDIR(st.st_mode))
            return EHFS_EISDIR;
        r = ehfs_cat_get(&fs, EHFS_CNID_ROOT, &e);
        return r == EHFS_OK ? copy_in_folder(host, path, &e) : r;
    }
    r = resolve_parent(path, &folder, name, sizeof name);
    if (r != EHFS_OK)
        return r;
    if (!ehfs_name_valid(S(name)))
    {
        warn("not a valid eHFS name (UTF-8 expected): left out", host);
        return EHFS_OK;
    }

    uint64_t mtime = ns(&st.st_mtim);

    r = ehfs_cat_lookup(&fs, folder.cnid, S(name), &e);
    if (r == EHFS_ENOENT)
        r = ehfs_create(&fs, folder.cnid, S(name), S_ISDIR(st.st_mode), mtime, &e);
    else if (r == EHFS_OK && e.is_dir != S_ISDIR(st.st_mode))
        r = e.is_dir ? EHFS_EISDIR : EHFS_ENOTDIR;
    if (r != EHFS_OK)
        return r;

    if (S_ISDIR(st.st_mode))
        return copy_in_folder(host, path, &e);

    int fd = open(host, O_RDONLY);

    if (fd < 0)
    {
        warn(strerror(errno), host);
        return EHFS_OK;
    }
    r = ehfs_set_length(&fs, e.cnid, EHFS_DATA_FORK, 0, mtime);
    if (r == EHFS_OK)
        r = ehfs_set_length(&fs, e.cnid, EHFS_RESOURCE_FORK, 0, mtime);
    for (uint64_t offset = 0; r == EHFS_OK;)
    {
        ssize_t n = read(fd, chunk, CHUNK);

        if (n <= 0)
        {
            if (n < 0)
                warn(strerror(errno), host);
            break;
        }
        r = ehfs_write(&fs, e.cnid, EHFS_DATA_FORK, offset, chunk, (uint32_t)n, mtime);
        offset += (uint64_t)n;
    }
    close(fd);
    if (r == EHFS_OK)
        r = ehfs_cat_get(&fs, e.cnid, &e);
    if (r == EHFS_OK)
    {
        e.times[0] = e.times[1] = mtime;
        r = copy_xattrs_in(host, &e);
    }
    if (r == EHFS_OK)
        r = ehfs_set_info(&fs, &e);
    return r;
}

static int copy_out(const char *path, const char *host);

typedef struct
{
    const char *path, *host;
    int result;
} out_t;

static int copy_out_item(void *ctx, const ehfs_entry_t *e)
{
    out_t *o = ctx;
    char from[4096], to[4096], name[EHFS_NAME_MAX + 1];

    memcpy(name, e->name, e->name_length);
    name[e->name_length] = '\0';
    snprintf(from, sizeof from, "%s/%s", strcmp(o->path, "/") == 0 ? "" : o->path, name);
    snprintf(to, sizeof to, "%s/%s", o->host, name);
    o->result = copy_out(from, to);
    return o->result != EHFS_OK;
}

static int copy_out(const char *path, const char *host)
{
    ehfs_entry_t e;
    int r = resolve(path, &e);

    if (r != EHFS_OK)
        return r;
    if (e.is_dir)
    {
        out_t o = {path, host, EHFS_OK};

        if (mkdir(host, 0777) != 0 && errno != EEXIST)
        {
            warn(strerror(errno), host);
            return EHFS_OK;
        }
        r = ehfs_cat_list(&fs, e.cnid, copy_out_item, &o);
        return r < 0 ? r : o.result;
    }

    int fd = open(host, O_WRONLY | O_CREAT | O_TRUNC, 0666);

    if (fd < 0)
    {
        warn(strerror(errno), host);
        return EHFS_OK;
    }

    uint64_t length = ehfs_le64(e.data_fork + FORK_LENGTH);

    for (uint64_t offset = 0; offset < length;)
    {
        uint32_t done;

        r = ehfs_read(&fs, e.cnid, EHFS_DATA_FORK, offset, chunk, CHUNK, &done);
        if (r != EHFS_OK || done == 0)
            break;
        if (write(fd, chunk, done) != (ssize_t)done)
        {
            warn(strerror(errno), host);
            break;
        }
        offset += done;
    }
    close(fd);
    if (r != EHFS_OK)
        return r;

    /* The resource fork, the Finder information, the other attributes. */
    uint64_t rsrc = ehfs_le64(e.rsrc_fork + FORK_LENGTH);
    static const uint8_t zeros[32];

    if (rsrc > CHUNK)
    {
        warn("the resource fork is larger than 1 MiB: left out", host);
    }
    else if (rsrc > 0)
    {
        uint32_t done;

        r = ehfs_read(&fs, e.cnid, EHFS_RESOURCE_FORK, 0, chunk, (uint32_t)rsrc, &done);
        if (r != EHFS_OK)
            return r;
        if (lsetxattr(host, RESOURCE_FORK_XATTR, chunk, done, 0) != 0)
            warn("the resource fork cannot be kept as an extended attribute here", host);
    }
    if (memcmp(e.finder, zeros, 32) != 0 && lsetxattr(host, FINDER_INFO_XATTR, e.finder, 32, 0) != 0)
        warn("the Finder information cannot be kept as an extended attribute here", host);
    for (uint32_t i = 0;; i++)
    {
        uint8_t name[EHFS_ATTR_NAME_MAX + 1];
        uint32_t n, size;

        if (ehfs_attr_name(&fs, e.cnid, i, name, &n) != EHFS_OK)
            break;
        name[n] = '\0';
        if (ehfs_attr_get(&fs, e.cnid, name, n, chunk, CHUNK, &size) == EHFS_OK &&
            lsetxattr(host, (const char *)name, chunk, size, 0) != 0)
            warn("an attribute cannot be kept as an extended attribute here", host);
    }

    struct timespec times[2];

    times[0].tv_sec = times[1].tv_sec = (time_t)(e.times[1] / 1000000000ull);
    times[0].tv_nsec = times[1].tv_nsec = (long)(e.times[1] % 1000000000ull);
    utimensat(AT_FDCWD, host, times, AT_SYMLINK_NOFOLLOW);
    return EHFS_OK;
}

static int show(void *ctx, const ehfs_entry_t *e)
{
    (void)ctx;
    printf("%s %12llu %10llu %.4s/%.4s %.*s\n", e->is_dir ? "d" : "f",
           (unsigned long long)ehfs_le64(e->data_fork + FORK_LENGTH),
           (unsigned long long)ehfs_le64(e->rsrc_fork + FORK_LENGTH), e->is_dir ? "    " : (const char *)e->finder,
           e->is_dir ? "    " : (const char *)e->finder + 4, (int)e->name_length, (const char *)e->name);
    return 0;
}

int main(int argc, char **argv)
{
    uint64_t offset = 0;
    bool device = false;
    int i = 1;

    for (; i < argc && argv[i][0] == '-'; i++)
    {
        if (strcmp(argv[i], "--offset") == 0 && i + 1 < argc)
            offset = strtoull(argv[++i], NULL, 0);
        else if (strcmp(argv[i], "--device") == 0)
            device = true;
        else
            break;
    }
    if (argc - i < 2)
    {
        fprintf(stderr, "usage: ehfs-cp [--offset SECTORS] [--device] TARGET in HOST EHFS | out EHFS HOST | "
                        "ls [EHFS] | mkdir EHFS | rm EHFS\n");
        return 2;
    }

    const char *target = argv[i], *command = argv[i + 1];
    bool writing = strcmp(command, "in") == 0 || strcmp(command, "mkdir") == 0 || strcmp(command, "rm") == 0;
    tool_disk_t disk;
    int r;

    if (tool_open(&disk, target, writing, device, "ehfs-cp") != 0)
        return 1;
    r = ehfs_mount(&fs, &disk.dev, offset, !writing);
    if (r != EHFS_OK)
    {
        fprintf(stderr, "ehfs-cp: %s: %s\n", target, tool_error(r));
        tool_close(&disk);
        return 1;
    }

    const char *a = i + 2 < argc ? argv[i + 2] : NULL, *b = i + 3 < argc ? argv[i + 3] : NULL;
    ehfs_entry_t e, folder;
    char name[512];

    if (strcmp(command, "in") == 0 && a != NULL && b != NULL)
    {
        r = copy_in(a, b);
    }
    else if (strcmp(command, "out") == 0 && a != NULL && b != NULL)
    {
        r = copy_out(a, b);
    }
    else if (strcmp(command, "ls") == 0)
    {
        r = resolve(a != NULL ? a : "/", &e);
        if (r == EHFS_OK)
            r = e.is_dir ? ehfs_cat_list(&fs, e.cnid, show, NULL) : show(NULL, &e);
    }
    else if (strcmp(command, "mkdir") == 0 && a != NULL)
    {
        r = resolve_parent(a, &folder, name, sizeof name);
        if (r == EHFS_OK)
            r = ehfs_create(&fs, folder.cnid, S(name), true, tool_now(), NULL);
    }
    else if (strcmp(command, "rm") == 0 && a != NULL)
    {
        r = resolve_parent(a, &folder, name, sizeof name);
        if (r == EHFS_OK)
            r = ehfs_remove(&fs, folder.cnid, S(name), tool_now());
    }
    else
    {
        fprintf(stderr, "ehfs-cp: unknown command or missing argument\n");
        r = EHFS_EINVAL;
    }
    if (writing)
    {
        int u = ehfs_unmount(&fs);

        if (r == EHFS_OK)
            r = u;
    }
    tool_close(&disk);
    if (r != EHFS_OK)
    {
        fprintf(stderr, "ehfs-cp: %s\n", tool_error(r));
        return 1;
    }
    return warnings > 0 ? 3 : 0;
}
