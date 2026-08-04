/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_SHELL_SHELL_H
#define KERNEL_SHELL_SHELL_H

void shell_init(void);

void shell_execute(const char *input);

#endif