/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "module.h"

#include "kernel/lib/k_string.h"

const hal_boot_module_t *boot_module_find(const char *name, hal_boot_module_t *storage)
{
    size_t wanted = k_strlen(name);

    for (unsigned int i = 0; hal_boot_module(i, storage) == 0; i++)
    {
        size_t length = k_strlen(storage->name);

        if (length >= wanted && k_strcmp(storage->name + length - wanted, name) == 0 &&
            (length == wanted || storage->name[length - wanted - 1] == ' '))
            return storage;
    }

    return NULL;
}
