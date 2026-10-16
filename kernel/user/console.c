/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "console.h"

#include "kernel/console/console.h"
#include "kernel/input/event_buffer.h"
#include "kernel/keyboard/keychar.h"
#include "kernel/keyboard/state.h"
#include "kernel/lib/k_errno.h"
#include "kernel/lib/k_memory.h"
#include "kernel/lib/k_string.h"
#include "kernel/memory/heap.h"

#include "hal/cpu.h"

/*
 * Line discipline of the console: characters typed are echoed and
 * collected until Enter, then handed to read() in order. Backspace
 * edits the line that is being typed.
 */
#define LINE_MAX 256

static char line[LINE_MAX];
static uint32_t line_length;   /* characters typed so far */
static uint32_t line_ready;    /* the line is complete: read() may take it */
static uint32_t line_consumed; /* how much of a complete line read() has taken */

static void feed_key(char c)
{
    if (c == '\b' || c == 0x7F)
    {
        if (line_length > 0)
        {
            line_length--;
            console_write_char('\b');
        }

        return;
    }

    if (c == '\r')
        c = '\n';

    if (line_length >= LINE_MAX - 1 && c != '\n')
        return;

    console_write_char(c);
    line[line_length++] = c;

    if (c == '\n')
        line_ready = 1;
}

/* Feed the keyboard events that have arrived to the line editor, until a line is complete. */
static void process_keys(void)
{
    input_event_t event;

    while (!line_ready && input_event_pop(&event) == 0)
    {
        if (event.type != INPUT_EVENT_KEYBOARD)
            continue;

        keyboard_state_update(event.data.keyboard.key, event.data.keyboard.pressed);

        if (!event.data.keyboard.pressed)
            continue;

        char c = keyboard_keycode_to_ascii(event.data.keyboard.key, keyboard_state_get());

        if (c)
            feed_key(c);
    }
}

static void wait_for_line(void)
{
    while (!line_ready)
    {
        process_keys();

        if (!line_ready)
        {
            hal_cpu_enable_interrupts();
            hal_cpu_idle();
        }
    }
}

/* poll(): the console can always be written; it can be read once a line is complete. */
static uint32_t console_poll(vfs_file_t *file, uint32_t events)
{
    (void)file;
    process_keys();

    return (events & VFS_POLLOUT) | ((events & VFS_POLLIN) && line_ready ? VFS_POLLIN : 0);
}

static int32_t console_read(vfs_file_t *file, uint64_t pos, void *buffer, uint32_t count)
{
    (void)file;
    (void)pos;

    if (count == 0)
        return 0;

    wait_for_line();

    uint32_t available = line_length - line_consumed;

    if (count > available)
        count = available;

    k_memcpy(buffer, line + line_consumed, count);
    line_consumed += count;

    if (line_consumed == line_length)
    {
        line_length = 0;
        line_consumed = 0;
        line_ready = 0;
    }

    return (int32_t)count;
}

static char *capture_buffer;
static size_t capture_size;
static size_t capture_length;

void user_console_capture_start(char *buffer, size_t size)
{
    capture_buffer = buffer;
    capture_size = size;
    capture_length = 0;

    if (size > 0)
        buffer[0] = '\0';
}

size_t user_console_capture_stop(void)
{
    capture_buffer = NULL;

    return capture_length;
}

static int32_t console_write_bytes(vfs_file_t *file, uint64_t pos, const void *buffer, uint32_t count)
{
    const char *text = buffer;

    (void)file;
    (void)pos;

    for (uint32_t i = 0; i < count; i++)
    {
        console_write_char(text[i]);

        if (capture_buffer != NULL && capture_length + 1 < capture_size)
        {
            capture_buffer[capture_length++] = text[i];
            capture_buffer[capture_length] = '\0';
        }
    }

    return (int32_t)count;
}

static int32_t char_device_stat(vfs_file_t *file, vfs_stat_t *stat)
{
    (void)file;
    stat->mode = VFS_S_IFCHR | 0666;
    stat->size = 0;
    stat->inode = 3;
    stat->mtime = 0;

    return 0;
}

static const vfs_file_ops_t console_ops = {
    .read = console_read,
    .write = console_write_bytes,
    .stat = char_device_stat,
    .poll = console_poll,
};

static int32_t null_read(vfs_file_t *file, uint64_t pos, void *buffer, uint32_t count)
{
    (void)file;
    (void)pos;
    (void)buffer;
    (void)count;

    return 0;
}

static int32_t null_write(vfs_file_t *file, uint64_t pos, const void *buffer, uint32_t count)
{
    (void)file;
    (void)pos;
    (void)buffer;

    return (int32_t)count;
}

static int32_t zero_read(vfs_file_t *file, uint64_t pos, void *buffer, uint32_t count)
{
    (void)file;
    (void)pos;
    k_memset(buffer, 0, count);

    return (int32_t)count;
}

/* xorshift32; not secure, but programs only ask for hash seeds and such. */
static uint32_t random_state = 0x2545F491u;

static int32_t random_read(vfs_file_t *file, uint64_t pos, void *buffer, uint32_t count)
{
    uint8_t *bytes = buffer;

    (void)file;
    (void)pos;

    for (uint32_t i = 0; i < count; i++)
    {
        random_state ^= random_state << 13;
        random_state ^= random_state >> 17;
        random_state ^= random_state << 5;
        bytes[i] = (uint8_t)random_state;
    }

    return (int32_t)count;
}

static const vfs_file_ops_t null_ops = {
    .read = null_read, .write = null_write, .stat = char_device_stat};
static const vfs_file_ops_t zero_ops = {
    .read = zero_read, .write = null_write, .stat = char_device_stat};
static const vfs_file_ops_t random_ops = {
    .read = random_read, .write = null_write, .stat = char_device_stat};

vfs_file_t *console_file_open(uint32_t flags)
{
    vfs_file_t *file = vfs_file_new(&console_ops, NULL, flags);

    if (file != NULL)
        file->seekable = 0;

    return file;
}

/* A directory listing for /dev: just the names. */
static const struct
{
    const char *name;
    const vfs_file_ops_t *ops;
} devices[] = {
    {"console", &console_ops},
    {"tty", &console_ops},
    {"null", &null_ops},
    {"zero", &zero_ops},
    {"urandom", &random_ops},
    {"random", &random_ops},
};

#define DEVICE_COUNT ((int)(sizeof(devices) / sizeof(devices[0])))

static int32_t dir_readdir(vfs_file_t *file, uint32_t index, vfs_dirent_t *entry)
{
    (void)file;

    if (index < 2)
    {
        k_strcpy(entry->name, index == 0 ? "." : "..");
        entry->inode = 1;
        entry->type = VFS_DT_DIR;

        return 0;
    }

    if (index - 2 >= (uint32_t)DEVICE_COUNT)
        return 1;

    k_strcpy(entry->name, devices[index - 2].name);
    entry->inode = index;
    entry->type = VFS_DT_CHR;

    return 0;
}

static int32_t dir_stat(vfs_file_t *file, vfs_stat_t *stat)
{
    (void)file;
    stat->mode = VFS_S_IFDIR | 0755;
    stat->size = 0;
    stat->inode = 1;
    stat->mtime = 0;

    return 0;
}

static const vfs_file_ops_t dir_ops = {
    .readdir = dir_readdir,
    .stat = dir_stat,
};

static int32_t devfs_open(vfs_fs_t *fs, const char *path, uint32_t flags,
                          uint32_t mode, vfs_file_t **file)
{
    (void)fs;
    (void)mode;

    if (path[0] == '\0')
    {
        if ((flags & VFS_O_ACCMODE) != VFS_O_RDONLY)
            return -K_EISDIR;

        *file = vfs_file_new(&dir_ops, NULL, flags);

        return *file != NULL ? 0 : -K_ENOMEM;
    }

    for (int i = 0; i < DEVICE_COUNT; i++)
    {
        if (k_strcmp(path, devices[i].name) == 0)
        {
            *file = vfs_file_new(devices[i].ops, NULL, flags);

            if (*file == NULL)
                return -K_ENOMEM;

            (*file)->seekable = 0;

            return 0;
        }
    }

    return -K_ENOENT;
}

static int32_t devfs_stat(vfs_fs_t *fs, const char *path, vfs_stat_t *stat)
{
    (void)fs;

    if (path[0] == '\0')
        return dir_stat(NULL, stat);

    for (int i = 0; i < DEVICE_COUNT; i++)
    {
        if (k_strcmp(path, devices[i].name) == 0)
            return char_device_stat(NULL, stat);
    }

    return -K_ENOENT;
}

vfs_fs_t *devfs_create(void)
{
    vfs_fs_t *fs = kmalloc(sizeof(*fs));

    if (fs == NULL)
        return NULL;

    k_memset(fs, 0, sizeof(*fs));
    fs->open = devfs_open;
    fs->stat = devfs_stat;

    return fs;
}
