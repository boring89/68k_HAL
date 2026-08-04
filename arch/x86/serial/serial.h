/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ARCH_X86_SERIAL_SERIAL_H
#define ARCH_X86_SERIAL_SERIAL_H

#include <stdint.h>

void serial_init(void);
int serial_write_char(char c);
void serial_write(const char *str);
void serial_write_line(const char *str);

#endif