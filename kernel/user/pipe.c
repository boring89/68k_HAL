/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "process.h"
#include "sched.h"
#include "signal.h"

#include "kernel/lib/k_errno.h"
#include "kernel/lib/k_memory.h"
#include "kernel/memory/heap.h"

/*
 * An anonymous pipe: a ring buffer between a read end and a write end, both ordinary open files
 * (so dup, fork and close work on them like on any file). A reader waits while the buffer is
 * empty and a writer is left; a writer waits while it is full and a reader is left. The last
 * writer going away is the end of file for the reader; writing with no reader is SIGPIPE.
 */
#define PIPE_SIZE 16384u
#define O_NONBLOCK 04000u
#define S_IFIFO_MODE 0010600u

typedef struct
{
    uint8_t *buffer;
    uint32_t head;  /* where the next byte is read */
    uint32_t count; /* bytes in the buffer */
    int readers;    /* open files for reading */
    int writers;
} pipe_t;

static int32_t pipe_read(vfs_file_t *file, uint64_t pos, void *buffer, uint32_t count)
{
    pipe_t *pipe = file->priv;
    uint8_t *out = buffer;

    (void)pos;

    if (count == 0)
        return 0;

    while (pipe->count == 0)
    {
        if (pipe->writers == 0)
            return 0;

        if (file->flags & O_NONBLOCK)
            return -K_EAGAIN;

        if (signal_interrupts(user_current))
            return -K_EINTR;

        sched_wait();
    }

    uint32_t n = count < pipe->count ? count : pipe->count;

    for (uint32_t i = 0; i < n; i++)
        out[i] = pipe->buffer[(pipe->head + i) % PIPE_SIZE];

    pipe->head = (pipe->head + n) % PIPE_SIZE;
    pipe->count -= n;
    sched_notify();

    return (int32_t)n;
}

static int32_t pipe_write(vfs_file_t *file, uint64_t pos, const void *buffer, uint32_t count)
{
    pipe_t *pipe = file->priv;
    const uint8_t *in = buffer;
    uint32_t done = 0;

    (void)pos;

    while (done < count)
    {
        if (pipe->readers == 0)
        {
            signal_send(user_current, SIGNAL_PIPE);

            return done > 0 ? (int32_t)done : -K_EPIPE;
        }

        uint32_t room = PIPE_SIZE - pipe->count;

        if (room == 0)
        {
            if (file->flags & O_NONBLOCK)
                return done > 0 ? (int32_t)done : -K_EAGAIN;

            if (signal_interrupts(user_current))
                return done > 0 ? (int32_t)done : -K_EINTR;

            sched_wait();

            continue;
        }

        uint32_t n = count - done < room ? count - done : room;

        for (uint32_t i = 0; i < n; i++)
            pipe->buffer[(pipe->head + pipe->count + i) % PIPE_SIZE] = in[done + i];

        pipe->count += n;
        done += n;
        sched_notify();
    }

    return (int32_t)done;
}

static uint32_t pipe_poll(vfs_file_t *file, uint32_t events)
{
    pipe_t *pipe = file->priv;
    uint32_t ready = 0;

    if ((file->flags & VFS_O_ACCMODE) == VFS_O_WRONLY)
    {
        if (pipe->readers == 0)
            ready |= VFS_POLLERR;
        else if (pipe->count < PIPE_SIZE)
            ready |= VFS_POLLOUT;
    }
    else
    {
        if (pipe->count > 0)
            ready |= VFS_POLLIN;

        if (pipe->writers == 0)
            ready |= VFS_POLLHUP;
    }

    return ready & (events | VFS_POLLERR | VFS_POLLHUP);
}

static int32_t pipe_stat(vfs_file_t *file, vfs_stat_t *stat)
{
    pipe_t *pipe = file->priv;

    k_memset(stat, 0, sizeof(*stat));
    stat->mode = S_IFIFO_MODE;
    stat->size = pipe->count;

    return 0;
}

static void pipe_close(vfs_file_t *file)
{
    pipe_t *pipe = file->priv;

    if ((file->flags & VFS_O_ACCMODE) == VFS_O_WRONLY)
        pipe->writers--;
    else
        pipe->readers--;

    /* A reader waits for the end of file, a writer for SIGPIPE: wake them. */
    sched_notify();

    if (pipe->readers == 0 && pipe->writers == 0)
    {
        kfree(pipe->buffer);
        kfree(pipe);
    }
}

static const vfs_file_ops_t pipe_ops = {
    .read = pipe_read,
    .write = pipe_write,
    .stat = pipe_stat,
    .poll = pipe_poll,
    .close = pipe_close,
};

int32_t pipe_create(vfs_file_t **read, vfs_file_t **write)
{
    pipe_t *pipe = kmalloc(sizeof(*pipe));

    if (pipe == NULL)
        return -K_ENOMEM;

    pipe->buffer = kmalloc(PIPE_SIZE);

    if (pipe->buffer == NULL)
    {
        kfree(pipe);

        return -K_ENOMEM;
    }

    pipe->head = 0;
    pipe->count = 0;
    pipe->readers = 1;
    pipe->writers = 1;

    *read = vfs_file_new(&pipe_ops, pipe, VFS_O_RDONLY);
    *write = vfs_file_new(&pipe_ops, pipe, VFS_O_WRONLY);

    if (*read == NULL || *write == NULL)
    {
        /* The missing end never counted; the one that exists frees the pipe when it closes. */
        if (*read == NULL)
            pipe->readers--;

        if (*write == NULL)
            pipe->writers--;

        if (*read != NULL)
            vfs_file_close(*read);

        if (*write != NULL)
            vfs_file_close(*write);

        if (*read == NULL && *write == NULL)
        {
            kfree(pipe->buffer);
            kfree(pipe);
        }

        return -K_ENOMEM;
    }

    (*read)->seekable = 0;
    (*write)->seekable = 0;

    return 0;
}
