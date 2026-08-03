/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "serial.h"
#include "../io/io.h"

#define COM1 0x3F8
#define SERIAL_TIMEOUT 100000

void serial_init(void)
{
    outb(COM1 + 1, 0x00); // disable interrupts

    outb(COM1 + 3, 0x80); // enable DLAB

    outb(COM1 + 0, 0x03); // divisor low
    outb(COM1 + 1, 0x00); // divisor high

    outb(COM1 + 3, 0x03); // 8bit, no parity

    outb(COM1 + 2, 0xC7); // FIFO enable

    outb(COM1 + 4, 0x0B); // modem control
}

static int serial_ready(void)
{
    return inb(COM1 + 5) & 0x20;
}

int serial_write_char(char c)
{
    int timeout = SERIAL_TIMEOUT;

    while (!serial_ready())
    {
        if (--timeout == 0)
            return 0;
    }

    outb(COM1, c);

    return 1;
}

void serial_write(const char *str)
{
    if (!str)
        return;
    
    while (*str)
    {
        if (!serial_write_char(*str))
            return;
        str++;
    }
}

void serial_write_line(const char *str)
{
    serial_write(str);
    serial_write("\n");
}