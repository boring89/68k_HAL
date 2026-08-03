/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_PRINTF_H
#define KERNEL_PRINTF_H

#include <stdarg.h>

void kernel_printf(const char *fmt, ...);
void kernel_vprintf(const char *fmt, va_list args);

#endif