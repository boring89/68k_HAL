/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "stack.h"

#include "kernel/lib/k_memory.h"
#include "kernel/lib/k_string.h"

#define MAX_STRINGS 256

static size_t count_strings(const char *const *list)
{
    size_t n = 0;

    while (list != NULL && list[n] != NULL)
        n++;

    return n;
}

static void put32(uint8_t *buffer, size_t capacity, size_t at_from_top, uint32_t value)
{
    uint8_t *p = buffer + capacity - at_from_top;

    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

size_t user_stack_build(uint8_t *buffer, size_t capacity, uint32_t top,
                        const char *const *argv, const char *const *envp,
                        const user_stack_params_t *params, uint32_t *sp)
{
    size_t argc = count_strings(argv);
    size_t envc = count_strings(envp);

    if (argc > MAX_STRINGS || envc > MAX_STRINGS)
        return 0;

    /* Strings first, from the top down; remember each one's offset from the top. */
    size_t used = 0;
    size_t argv_at[MAX_STRINGS];
    size_t envp_at[MAX_STRINGS];

    for (size_t i = 0; i < argc + envc; i++)
    {
        const char *s = i < argc ? argv[i] : envp[i - argc];
        size_t length = k_strlen(s) + 1;

        if (length > capacity - used)
            return 0;

        used += length;
        k_memcpy(buffer + capacity - used, s, length);

        if (i < argc)
            argv_at[i] = used;
        else
            envp_at[i - argc] = used;
    }

    if (4 + 16 > capacity - used)
        return 0;

    used += 5;
    k_memcpy(buffer + capacity - used, "i686", 5);
    size_t platform_at = used;

    used += 16;
    k_memcpy(buffer + capacity - used, params->random, 16);
    size_t random_at = used;

    /* The auxiliary vector, argv, envp and argc, all 32-bit words. */
    uint32_t auxv[][2] = {
        {AT_PHDR, params->phdr},
        {AT_PHENT, params->phent},
        {AT_PHNUM, params->phnum},
        {AT_PAGESZ, 4096},
        {AT_ENTRY, params->entry},
        {AT_UID, 0},
        {AT_EUID, 0},
        {AT_GID, 0},
        {AT_EGID, 0},
        {AT_PLATFORM, top - (uint32_t)platform_at},
        {AT_HWCAP, 0},
        {AT_SECURE, 0},
        {AT_RANDOM, top - (uint32_t)random_at},
        {AT_NULL, 0},
    };
    size_t aux_count = sizeof(auxv) / sizeof(auxv[0]);
    size_t words = 1 + (argc + 1) + (envc + 1) + 2 * aux_count;

    /* Pad so that the stack pointer (the address of argc) is 16-byte aligned. */
    size_t words_bytes = words * 4;
    size_t sp_from_top = used + words_bytes;

    while (((top - sp_from_top) & 15) != 0)
        sp_from_top++;

    if (sp_from_top > capacity)
        return 0;

    for (size_t i = used; i < sp_from_top; i++)
        buffer[capacity - i - 1] = 0;

    size_t at = sp_from_top;

    put32(buffer, capacity, at, (uint32_t)argc);
    at -= 4;

    for (size_t i = 0; i < argc; i++, at -= 4)
        put32(buffer, capacity, at, top - (uint32_t)argv_at[i]);

    put32(buffer, capacity, at, 0);
    at -= 4;

    for (size_t i = 0; i < envc; i++, at -= 4)
        put32(buffer, capacity, at, top - (uint32_t)envp_at[i]);

    put32(buffer, capacity, at, 0);
    at -= 4;

    for (size_t i = 0; i < aux_count; i++)
    {
        put32(buffer, capacity, at, auxv[i][0]);
        at -= 4;
        put32(buffer, capacity, at, auxv[i][1]);
        at -= 4;
    }

    *sp = top - (uint32_t)sp_from_top;

    return sp_from_top;
}
