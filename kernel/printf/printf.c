/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "printf.h"

#include <stdarg.h>

#include "../console/console.h"
#include "../lib/k_string.h"
#include "../lib/k_number.h"

void kernel_vprintf(const char *fmt, va_list args)
{
    if (!fmt)
        return;

    char buffer[32];

    while (*fmt)
    {
        if (*fmt != '%')
        {
            console_write_char(*fmt);
            fmt++;
            continue;
        }

        fmt++;

        switch (*fmt)
        {
        case '%':
        {
            console_write_char('%');
            break;
        }
        case 'c':
        {
            char c = va_arg(args, int);
            console_write_char(c);
            break;
        }

        case 's':
        {
            const char *str = va_arg(args, const char *);
            console_write(str);
            break;
        }

        case 'd':
        {
            int value = va_arg(args, int);

            k_itoa(value, buffer);

            console_write(buffer);
            break;
        }

        case 'u':
        {
            unsigned int value = va_arg(args, int);

            k_utoa(value, buffer);
            console_write(buffer);
            break;
        }

        case 'x':
        {
            unsigned int value = va_arg(args, unsigned int);

            k_utoa_hex(value, buffer);
            console_write(buffer);
            break;
        }
        default:
        {
            console_write_char('?');
            break;
        }
        }
        fmt++;
    }
}

void kernel_printf(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);

    kernel_vprintf(fmt, args);

    va_end(args);
}