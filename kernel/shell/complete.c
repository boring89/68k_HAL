/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "complete.h"

#include "kernel/lib/k_string.h"
#include "kernel/vfs/vfs.h"

#include <stdbool.h>
#include <stddef.h>

static int (*command_count)(void);
static const char *(*command_name)(int index);

void shell_complete_set_commands(int (*count)(void), const char *(*name)(int index))
{
    command_count = count;
    command_name = name;
}

static bool has_item(const terminal_completion_t *completion, const char *name)
{
    for (int i = 0; i < completion->count; i++)
    {
        if (k_strcmp(completion->item[i], name) == 0)
            return true;
    }

    return false;
}

/* Add prefix + name (+ "/" for a directory) if it starts with typed and fits. */
static void add_item(terminal_completion_t *completion, const char *prefix, const char *name,
                     bool directory, const char *typed, size_t typed_length)
{
    char word[TERMINAL_COMPLETION_LENGTH];
    size_t prefix_length = k_strlen(prefix);
    size_t name_length = k_strlen(name);

    if (completion->count >= TERMINAL_COMPLETION_MAX)
        return;

    if (k_strncmp(name, typed, typed_length) != 0)
        return;

    /* A name that starts with a dot is only offered when the dot was typed. */
    if (name[0] == '.' && typed_length == 0)
        return;

    if (prefix_length + name_length + 2 > sizeof(word))
        return;

    k_strcpy(word, prefix);
    k_strcpy(word + prefix_length, name);

    if (directory)
    {
        word[prefix_length + name_length] = '/';
        word[prefix_length + name_length + 1] = '\0';
    }

    if (!has_item(completion, word))
        k_strcpy(completion->item[completion->count++], word);
}

/* Names in directory dir (an absolute or relative path) that start with typed. */
static void add_directory(terminal_completion_t *completion, const char *dir, const char *prefix,
                          const char *typed, bool programs_only)
{
    vfs_file_t *file;
    vfs_dirent_t entry;

    if (vfs_open(dir, VFS_O_RDONLY | VFS_O_DIRECTORY, 0, &file) != 0)
        return;

    size_t typed_length = k_strlen(typed);

    while (vfs_readdir(file, &entry) == 0)
    {
        if (k_strcmp(entry.name, ".") == 0 || k_strcmp(entry.name, "..") == 0)
            continue;

        if (programs_only && entry.type == VFS_DT_DIR)
            continue;

        add_item(completion, prefix, entry.name, entry.type == VFS_DT_DIR, typed, typed_length);
    }

    vfs_file_close(file);
}

void shell_complete(const char *line, size_t cursor, terminal_completion_t *completion)
{
    size_t start = cursor;

    while (start > 0 && line[start - 1] != ' ')
        start--;

    completion->start = start;
    completion->count = 0;

    char word[TERMINAL_COMPLETION_LENGTH];
    size_t length = cursor - start;

    if (length >= sizeof(word))
        return;

    for (size_t i = 0; i < length; i++)
        word[i] = line[start + i];

    word[length] = '\0';

    /* Is this the first word of the line? */
    bool first = true;

    for (size_t i = 0; i < start; i++)
    {
        if (line[i] != ' ')
            first = false;
    }

    /* A path: the part up to the last slash is the directory, the rest is what is typed. */
    const char *slash = NULL;

    for (const char *p = word; *p != '\0'; p++)
    {
        if (*p == '/')
            slash = p;
    }

    if (slash != NULL)
    {
        char dir[TERMINAL_COMPLETION_LENGTH];
        char prefix[TERMINAL_COMPLETION_LENGTH];
        size_t dir_length = (size_t)(slash - word) + 1;

        for (size_t i = 0; i < dir_length; i++)
            dir[i] = prefix[i] = word[i];

        dir[dir_length] = prefix[dir_length] = '\0';

        add_directory(completion, dir, prefix, slash + 1, false);

        return;
    }

    if (first)
    {
        size_t typed_length = k_strlen(word);

        for (int i = 0; command_count != NULL && i < command_count(); i++)
            add_item(completion, "", command_name(i), false, word, typed_length);

        add_directory(completion, "/bin", "", word, true);

        return;
    }

    add_directory(completion, ".", "", word, false);
}
