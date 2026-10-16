/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_VFS_VFS_H
#define KERNEL_VFS_VFS_H

#include <stddef.h>
#include <stdint.h>

/*
 * A small virtual file system: one tree of paths (/a/b/c), made of
 * file systems mounted at directories. The flag and mode values are
 * the Linux i386 ones because user programs pass them straight through
 * system calls.
 *
 *   /        the program disk (initrd, read-only)
 *   /tmp     a RAM file system (read/write)
 *   /hfs     the HFS volume of the Mac disk (read-only)
 *   /dev/... the console
 */

/* open() flags (Linux asm-generic/fcntl.h). */
#define VFS_O_RDONLY 0u
#define VFS_O_WRONLY 1u
#define VFS_O_RDWR 2u
#define VFS_O_ACCMODE 3u
#define VFS_O_CREAT 0100u
#define VFS_O_EXCL 0200u
#define VFS_O_TRUNC 01000u
#define VFS_O_APPEND 02000u
#define VFS_O_DIRECTORY 0200000u

/* st_mode type bits (Linux uapi/linux/stat.h). */
#define VFS_S_IFMT 0170000u
#define VFS_S_IFREG 0100000u
#define VFS_S_IFDIR 0040000u
#define VFS_S_IFCHR 0020000u

/* lseek whence. */
#define VFS_SEEK_SET 0
#define VFS_SEEK_CUR 1
#define VFS_SEEK_END 2

/* Directory entry types (DT_* in dirent.h). */
#define VFS_DT_CHR 2
#define VFS_DT_DIR 4
#define VFS_DT_REG 8

/* poll() event bits (Linux asm-generic/poll.h). */
#define VFS_POLLIN 0x0001u
#define VFS_POLLPRI 0x0002u
#define VFS_POLLOUT 0x0004u
#define VFS_POLLERR 0x0008u
#define VFS_POLLHUP 0x0010u
#define VFS_POLLNVAL 0x0020u

#define VFS_NAME_MAX 255
#define VFS_PATH_MAX 512

typedef struct
{
    uint32_t mode; /* VFS_S_IFxxx | permission bits */
    uint64_t size;
    uint32_t inode;
    uint32_t mtime; /* seconds since 1970, 0 if unknown */
} vfs_stat_t;

typedef struct
{
    char name[VFS_NAME_MAX + 1];
    uint32_t inode;
    uint8_t type; /* VFS_DT_* */
} vfs_dirent_t;

typedef struct vfs_file vfs_file_t;

/*
 * What a file object can do. Every function returns a byte count or 0
 * on success, or -errno (kernel/lib/k_errno.h); a missing function
 * means "not supported" (-EINVAL for reads and writes of a directory
 * and so on, decided by the vfs_* wrappers).
 */
typedef struct
{
    int32_t (*read)(vfs_file_t *file, uint64_t pos, void *buffer, uint32_t count);
    int32_t (*write)(vfs_file_t *file, uint64_t pos, const void *buffer, uint32_t count);
    /* Entry number index of a directory: 0 = got one, 1 = no more. */
    int32_t (*readdir)(vfs_file_t *file, uint32_t index, vfs_dirent_t *entry);
    int32_t (*stat)(vfs_file_t *file, vfs_stat_t *stat);
    int32_t (*truncate)(vfs_file_t *file, uint64_t size);
    /*
     * Which of the VFS_POLL* events in events can happen now without waiting
     * (read: data or end of file; write: room). A file without it is always
     * ready for reading and writing. May poll the hardware; must not block.
     */
    uint32_t (*poll)(vfs_file_t *file, uint32_t events);
    /* ioctl(): request and arg as the program gave them (arg is usually a user address). Returns 0 or -errno; -ENOTTY without it. */
    int32_t (*ioctl)(vfs_file_t *file, uint32_t request, uint32_t arg);
    /*
     * mmap() of a device: set *phys to the physical address of byte offset of the file (the
     * device memory is contiguous); the program's pages are mapped onto it directly. 0 or -errno.
     */
    int32_t (*mmap)(vfs_file_t *file, uint32_t length, uint32_t offset, uintptr_t *phys);
    void (*close)(vfs_file_t *file);
} vfs_file_ops_t;

struct vfs_file
{
    const vfs_file_ops_t *ops;
    void *priv;
    uint64_t pos;
    uint32_t flags; /* the VFS_O_* it was opened with */
    int refcount;   /* open file descriptors that share it */
    int seekable;   /* 0 for the console */
};

typedef struct vfs_fs vfs_fs_t;

/*
 * A file system. Paths are relative to its mount point, without a
 * leading slash and already normalised ("" is the root of the file
 * system); a missing function means the operation is not supported
 * (-EROFS for the ones that change something).
 */
struct vfs_fs
{
    int32_t (*open)(vfs_fs_t *fs, const char *path, uint32_t flags,
                    uint32_t mode, vfs_file_t **file);
    int32_t (*stat)(vfs_fs_t *fs, const char *path, vfs_stat_t *stat);
    int32_t (*mkdir)(vfs_fs_t *fs, const char *path, uint32_t mode);
    int32_t (*unlink)(vfs_fs_t *fs, const char *path);
    int32_t (*rmdir)(vfs_fs_t *fs, const char *path);
    int32_t (*rename)(vfs_fs_t *fs, const char *from, const char *to);
    void *priv;
};

/* Mount fs at the absolute directory path ("/" for the root). Returns 0 or -errno. */
int32_t vfs_mount(const char *path, vfs_fs_t *fs);

/* Forget every mount (for tests). */
void vfs_reset(void);

/*
 * Turn path (relative paths are relative to the current directory) into a
 * normalised absolute one in out (VFS_PATH_MAX bytes): no ".", "..",
 * doubled or trailing slashes. Returns 0 or -errno (-ENOENT for an empty path).
 */
int32_t vfs_normalize(const char *path, char *out);

/*
 * The current directory, "/" at first: one for the whole system (the shell and
 * the program it runs share it; the program loader restores it afterwards).
 * vfs_chdir needs an existing directory: 0 or -errno.
 */
const char *vfs_getcwd(void);
int32_t vfs_chdir(const char *path);

/* Open a file or directory. Returns 0 and a file with refcount 1, or -errno. */
int32_t vfs_open(const char *path, uint32_t flags, uint32_t mode, vfs_file_t **file);

int32_t vfs_stat(const char *path, vfs_stat_t *stat);
int32_t vfs_mkdir(const char *path, uint32_t mode);
int32_t vfs_unlink(const char *path);
int32_t vfs_rmdir(const char *path);
int32_t vfs_rename(const char *from, const char *to);

/* Share an open file (one more descriptor). */
vfs_file_t *vfs_file_ref(vfs_file_t *file);

/* Drop one reference; the last one closes the file. */
void vfs_file_close(vfs_file_t *file);

/* Read and write at the file position, which they advance. */
int32_t vfs_read(vfs_file_t *file, void *buffer, uint32_t count);
int32_t vfs_write(vfs_file_t *file, const void *buffer, uint32_t count);

/* Returns the new position or -errno. */
int64_t vfs_seek(vfs_file_t *file, int64_t offset, int whence);

int32_t vfs_fstat(vfs_file_t *file, vfs_stat_t *stat);

/* Next entries of a directory file (position counts entries). 0 = got one, 1 = end. */
int32_t vfs_readdir(vfs_file_t *file, vfs_dirent_t *entry);

int32_t vfs_ftruncate(vfs_file_t *file, uint64_t size);

/* ioctl(): 0 or -errno (-ENOTTY for files that have none). */
int32_t vfs_ioctl(vfs_file_t *file, uint32_t request, uint32_t arg);

/* The events (VFS_POLL*) of events that are ready now. */
uint32_t vfs_poll(vfs_file_t *file, uint32_t events);

/* Helper for file systems: allocate a vfs_file_t. NULL if out of memory. */
vfs_file_t *vfs_file_new(const vfs_file_ops_t *ops, void *priv, uint32_t flags);

#endif
