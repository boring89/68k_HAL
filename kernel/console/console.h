/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_CONSOLE_CONSOLE_H
#define KERNEL_CONSOLE_CONSOLE_H

#include "color.h"
void console_init(void);

void console_write(const char *str);
void console_write_char(char c);
void console_write_line(const char *str);
void console_write_color(const char *str, color_t fg, color_t bg);
void console_write_color_line(const char *str, color_t fg, color_t bg);
void console_clear(void);
#endif