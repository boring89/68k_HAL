/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "page_fault.h"

#include "exception.h"
#include "kernel/lib/k_string.h"

/* Append ", part" (or just "part" at the start); -1 if it does not fit. */
static int page_fault_append(char *buf, size_t size, size_t *len, const char *part)
{
    size_t separator = *len == 0 ? 0 : 2;
    size_t part_len = k_strlen(part);

    if (*len + separator + part_len >= size)
        return -1;

    if (separator)
        k_strcpy(buf + *len, ", ");

    k_strcpy(buf + *len + separator, part);
    *len += separator + part_len;

    return 0;
}

int page_fault_describe(uint32_t err_code, char *buf, size_t size)
{
    if (buf == NULL || size == 0)
        return -1;

    const char *parts[5];
    size_t count = 0;

    parts[count++] = (err_code & EXCEPTION_PF_PRESENT)
        ? "protection violation" : "not present";
    parts[count++] = (err_code & EXCEPTION_PF_WRITE) ? "write" : "read";
    parts[count++] = (err_code & EXCEPTION_PF_USER) ? "user" : "kernel";

    if (err_code & EXCEPTION_PF_RESERVED)
        parts[count++] = "reserved bit";

    if (err_code & EXCEPTION_PF_FETCH)
        parts[count++] = "instruction fetch";

    size_t len = 0;

    buf[0] = '\0';

    for (size_t i = 0; i < count; i++)
    {
        if (page_fault_append(buf, size, &len, parts[i]) != 0)
        {
            buf[0] = '\0';
            return -1;
        }
    }

    return 0;
}
