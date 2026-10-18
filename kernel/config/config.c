/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"

#include "kernel/lib/k_string.h"

typedef struct
{
    char key[CONFIG_KEY_SIZE];
    char value[CONFIG_VALUE_SIZE];
} config_entry_t;

static config_entry_t entries[CONFIG_MAX_ENTRIES];
static int entry_count;

void config_reset(void)
{
    entry_count = 0;
}

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

/* Copy [start, end) without the spaces around it; -1 if it does not fit. */
static int copy_trimmed(char *out, size_t size, const char *start, const char *end)
{
    while (start < end && is_space(*start))
        start++;

    while (end > start && is_space(end[-1]))
        end--;

    size_t length = (size_t)(end - start);

    if (length + 1 > size)
        return -1;

    for (size_t i = 0; i < length; i++)
        out[i] = start[i];

    out[length] = '\0';

    return 0;
}

int config_set(const char *key, const char *value)
{
    if (key == NULL || value == NULL || key[0] == '\0' ||
        k_strlen(key) >= CONFIG_KEY_SIZE || k_strlen(value) >= CONFIG_VALUE_SIZE)
        return -1;

    for (int i = 0; i < entry_count; i++)
    {
        if (k_strcmp(entries[i].key, key) == 0)
        {
            k_strcpy(entries[i].value, value);
            return 0;
        }
    }

    if (entry_count == CONFIG_MAX_ENTRIES)
        return -1;

    k_strcpy(entries[entry_count].key, key);
    k_strcpy(entries[entry_count].value, value);
    entry_count++;

    return 0;
}

/* One "key = value" (without the newline). 1 if a setting was taken. */
static int parse_line(const char *start, const char *end)
{
    char key[CONFIG_KEY_SIZE];
    char value[CONFIG_VALUE_SIZE];
    const char *equal = NULL;

    for (const char *p = start; p < end; p++)
    {
        if (*p == '#')
        {
            end = p;
            break;
        }

        if (*p == '=' && equal == NULL)
            equal = p;
    }

    if (equal == NULL || copy_trimmed(key, sizeof key, start, equal) != 0 ||
        copy_trimmed(value, sizeof value, equal + 1, end) != 0)
        return 0;

    return config_set(key, value) == 0;
}

int config_parse(const char *text, size_t size)
{
    int taken = 0;
    size_t line = 0;

    if (text == NULL)
        return 0;

    for (size_t i = 0; i <= size; i++)
    {
        if (i == size || text[i] == '\n' || text[i] == '\0')
        {
            taken += parse_line(text + line, text + i);
            line = i + 1;

            if (i < size && text[i] == '\0')
                break;
        }
    }

    return taken;
}

int config_apply_cmdline(const char *cmdline)
{
    int taken = 0;

    if (cmdline == NULL)
        return 0;

    const char *p = cmdline;

    while (*p != '\0')
    {
        while (*p == ' ')
            p++;

        const char *start = p;

        while (*p != '\0' && *p != ' ')
            p++;

        if (p == start)
            break;

        const char *equal = start;

        while (equal < p && *equal != '=')
            equal++;

        if (equal < p)
            taken += parse_line(start, p);
    }

    return taken;
}

const char *config_get(const char *key)
{
    for (int i = 0; key != NULL && i < entry_count; i++)
    {
        if (k_strcmp(entries[i].key, key) == 0)
            return entries[i].value;
    }

    return NULL;
}

const char *config_get_or(const char *key, const char *fallback)
{
    const char *value = config_get(key);

    return value != NULL ? value : fallback;
}

int config_entry(int index, const char **key, const char **value)
{
    if (index < 0 || index >= entry_count)
        return -1;

    *key = entries[index].key;
    *value = entries[index].value;

    return 0;
}
