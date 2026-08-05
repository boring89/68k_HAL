/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_LIB_K_NUMBER_H
#define KERNEL_LIB_K_NUMBER_H

#include <stdint.h>

static void k_reverse(char *buffer, int len);

void k_utoa(unsigned int value, char *buffer);
void k_itoa(int value, char *buffer);

void k_utoa_hex(unsigned int value, char *buffer);
void k_utoa_ptr(uint32_t value, char *buffer);

void k_atoi(const char *str, int *value);

uint32_t k_atoh(const char *str);

#endif