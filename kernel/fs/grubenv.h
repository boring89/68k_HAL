/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_FS_GRUBENV_H
#define KERNEL_FS_GRUBENV_H

#include <stddef.h>

/*
 * GRUB's environment block (grub-editenv, load_env, save_env): a file of
 * exactly 1024 bytes, "# GRUB Environment Block\n", then "name=value\n"
 * lines, the rest filled with '#'. Values here never hold newlines or
 * backslashes (GRUB would escape them).
 */

#define GRUBENV_SIZE 1024
#define GRUBENV_MAX_VARS 16
#define GRUBENV_NAME_SIZE 32
#define GRUBENV_VALUE_SIZE 64

typedef struct
{
    int count;
    char name[GRUBENV_MAX_VARS][GRUBENV_NAME_SIZE];
    char value[GRUBENV_MAX_VARS][GRUBENV_VALUE_SIZE];
} grubenv_t;

/* Parse a block. Returns 0, or -1 if it is not an environment block. */
int grubenv_parse(grubenv_t *env, const char *block, size_t size);

/* The value of name, or NULL. */
const char *grubenv_get(const grubenv_t *env, const char *name);

/* Set (or add) a variable. Returns 0, or -1 if it does not fit. */
int grubenv_set(grubenv_t *env, const char *name, const char *value);

/* Write the block (GRUBENV_SIZE bytes). Returns 0, or -1 if the variables do not fit. */
int grubenv_format(const grubenv_t *env, char block[GRUBENV_SIZE]);

#endif
