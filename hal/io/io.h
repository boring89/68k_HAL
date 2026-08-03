/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef HAL_IO_IO_H
#define HAL_IO_IO_H

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t value)
{
    asm volatile(
        "outb %0, %1"
        :
        : "a"(value),
          "Nd"(port)
        : "memory"
    );
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t result;

    asm volatile(
        "inb %1, %0"
        : "=a"(result)
        : "Nd"(port)
        : "memory"
    );

    return result;
}

#endif