/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ARCH_X86_IO_IO_H
#define ARCH_X86_IO_IO_H

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

static inline uint16_t inw(uint16_t port)
{
    uint16_t result;

    asm volatile(
        "inw %1, %0"
        : "=a"(result)
        : "Nd"(port)
        : "memory"
    );

    return result;
}

static inline void outw(uint16_t port, uint16_t value)
{
    asm volatile(
        "outw %0, %1"
        :
        : "a"(value),
          "Nd"(port)
        : "memory"
    );
}

static inline void outl(uint16_t port, uint32_t value)
{
    asm volatile(
        "outl %0, %1"
        :
        : "a"(value),
          "Nd"(port)
        : "memory"
    );
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t result;

    asm volatile(
        "inl %1, %0"
        : "=a"(result)
        : "Nd"(port)
        : "memory"
    );

    return result;
}

#endif