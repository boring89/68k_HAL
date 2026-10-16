/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "k_snprintf.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    char *buffer;
    size_t size;
    size_t length; /* what the whole output needs */
} sink_t;

static void put(sink_t *sink, char c)
{
    if (sink->size > 0 && sink->length < sink->size - 1)
        sink->buffer[sink->length] = c;

    sink->length++;
}

/* A number in the given base with the field width (padded with zeros or blanks), negative sign first. */
static void put_number(sink_t *sink, unsigned long long value, unsigned int base, bool negative,
                       int width, bool zero)
{
    char digits[24];
    int count = 0;

    do
    {
        digits[count++] = "0123456789abcdef"[value % base];
        value /= base;
    } while (value != 0);

    int total = count + (negative ? 1 : 0);

    if (zero && negative)
        put(sink, '-');

    for (int i = total; i < width; i++)
        put(sink, zero ? '0' : ' ');

    if (!zero && negative)
        put(sink, '-');

    while (count > 0)
        put(sink, digits[--count]);
}

size_t k_vsnprintf(char *buffer, size_t size, const char *format, va_list args)
{
    sink_t sink = {buffer, size, 0};

    for (; *format != '\0'; format++)
    {
        if (*format != '%')
        {
            put(&sink, *format);
            continue;
        }

        format++;

        bool zero = false;
        int width = 0;
        int longs = 0;

        if (*format == '0')
        {
            zero = true;
            format++;
        }

        while (*format >= '0' && *format <= '9')
            width = width * 10 + (*format++ - '0');

        while (*format == 'l')
        {
            longs++;
            format++;
        }

        switch (*format)
        {
        case '%':
            put(&sink, '%');
            break;

        case 'c':
            put(&sink, (char)va_arg(args, int));
            break;

        case 's':
        {
            const char *text = va_arg(args, const char *);

            if (text == NULL)
                text = "(null)";

            int length = 0;

            while (text[length] != '\0')
                length++;

            for (int i = length; i < width; i++)
                put(&sink, ' ');

            while (*text != '\0')
                put(&sink, *text++);

            break;
        }

        case 'd':
        {
            int value = va_arg(args, int);

            put_number(&sink, value < 0 ? 0ull - (unsigned long long)(long long)value : (unsigned long long)value, 10,
                       value < 0, width, zero);
            break;
        }

        case 'u':
        case 'x':
        {
            unsigned long long value = longs >= 2 ? va_arg(args, unsigned long long)
                                       : longs == 1 ? va_arg(args, unsigned long)
                                                    : va_arg(args, unsigned int);

            put_number(&sink, value, *format == 'x' ? 16 : 10, false, width, zero);
            break;
        }

        case '\0':
            format--; /* a lone % at the end */
            break;

        default:
            put(&sink, '?');
            break;
        }
    }

    if (size > 0)
        buffer[sink.length < size - 1 ? sink.length : size - 1] = '\0';

    return sink.length;
}

size_t k_snprintf(char *buffer, size_t size, const char *format, ...)
{
    va_list args;

    va_start(args, format);

    size_t length = k_vsnprintf(buffer, size, format, args);

    va_end(args);

    return length;
}
