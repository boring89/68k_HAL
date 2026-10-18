/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_CONFIG_CONFIG_H
#define KERNEL_CONFIG_CONFIG_H

#include <stddef.h>

/*
 * Settings: the file 68k_hal.cfg on the Boot partition (loaded by GRUB as the
 * boot module "config"), then "key=value" words of the kernel command line,
 * which win. The file is text, one "key = value" per line; '#' starts a
 * comment; spaces around key and value are ignored; a later line replaces an
 * earlier one. The keys are listed in docs/mac_disk.md.
 */

#define CONFIG_MAX_ENTRIES 32
#define CONFIG_KEY_SIZE 32
#define CONFIG_VALUE_SIZE 96

/* Forget every setting. */
void config_reset(void);

/*
 * Read size bytes of configuration text (it need not end in NUL). Lines that
 * are too long or have no '=' are skipped. Returns the number of settings
 * read.
 */
int config_parse(const char *text, size_t size);

/*
 * Take every "key=value" word of a kernel command line (words without '=',
 * such as the kernel's path or "diag", are ignored). Returns the number taken.
 */
int config_apply_cmdline(const char *cmdline);

/* Set one value. Returns 0, or -1 if key or value is too long or the table is full. */
int config_set(const char *key, const char *value);

/* The value of key, or NULL. */
const char *config_get(const char *key);

/* The value of key, or fallback if it is not set. */
const char *config_get_or(const char *key, const char *fallback);

/* Entry index (0 ..), for listing: 0 and the strings, or -1 past the end. */
int config_entry(int index, const char **key, const char **value);

#endif
