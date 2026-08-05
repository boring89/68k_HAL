/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "k_number.h"

#include <stdint.h>

static void k_reverse(char *buffer, int len)
{
    if (!buffer || len <= 1)
        return;

    int i = 0;
    int j = len - 1;

    while (i < j)
    {
        char temp = buffer[i];
        buffer[i] = buffer[j];
        buffer[j] = temp;

        i++;
        j--;
    }
}

void k_utoa(unsigned int value, char *buffer)
{
    int i = 0;

    if (value == 0)
    {
        buffer[0] = '0';
        buffer[1] = '\0';
        return;
    }

    while (value > 0)
    {
        buffer[i++] = (value % 10) + '0';
        value /= 10;
    }

    buffer[i] = '\0';

    k_reverse(buffer, i);
}


void k_itoa(int value, char *buffer)
{
    if (value < 0)
    {
        *buffer++ = '-';
        k_utoa(-(unsigned int)value, buffer);
    }
    else
    {
        k_utoa(value, buffer);
    }
}


void k_utoa_hex(unsigned int value, char *buffer)
{
    const char digits[] = "0123456789ABCDEF";

    int i = 0;

    if (value == 0)
    {
        buffer[0] = '0';
        buffer[1] = '\0';
        return;
    }

    while (value > 0)
    {
        buffer[i++] = digits[value % 16];
        value /= 16;
    }

    buffer[i] = '\0';

    k_reverse(buffer, i);
}

void k_utoa_ptr(uint32_t value, char *buffer)
{
    buffer[0]='0';
    buffer[1]='x';

    k_utoa_hex(value, buffer+2);
}

void k_atoi(const char *str, int *value)
{
    int result = 0;
    int sign = 1;

    if (*str == '-')
    {
        sign = -1;
        str++;
    }

    while (*str >= '0' && *str <= '9')
    {
        result = result * 10 + (*str - '0');
        str++;
    }

    *value = result * sign;
}

uint32_t k_atoh(const char *str)
{
    uint32_t value = 0;

    if (str[0] == '0' &&
        (str[1] == 'x' || str[1] == 'X'))
    {
        str += 2;
    }

    while (*str)
    {
        char c = *str;

        uint32_t digit;


        if (c >= '0' && c <= '9')
        {
            digit = c - '0';
        }
        else if (c >= 'a' && c <= 'f')
        {
            digit = c - 'a' + 10;
        }
        else if (c >= 'A' && c <= 'F')
        {
            digit = c - 'A' + 10;
        }
        else
        {
            break;
        }


        value = value * 16 + digit;

        str++;
    }

    return value;
}