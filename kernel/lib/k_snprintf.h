/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_LIB_K_SNPRINTF_H
#define KERNEL_LIB_K_SNPRINTF_H

#include <stdarg.h>
#include <stddef.h>

/*
 * Format into a buffer of size bytes (always NUL terminated when size > 0;
 * too long output is cut). Returns the length the whole output has (as the
 * C function does). Supports %s %c %d %u %x %% and %lu / %llu for 64-bit
 * values (as unsigned long long), each with an optional field width and
 * the 0 flag (%08x, %5u); nothing else.
 */
size_t k_snprintf(char *buffer, size_t size, const char *format, ...);
size_t k_vsnprintf(char *buffer, size_t size, const char *format, va_list args);

#endif
