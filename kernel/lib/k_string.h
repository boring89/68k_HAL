/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_LIB_K_STRING_H
#define KERNEL_LIB_K_STRING_H

#include <stddef.h>

size_t k_strlen(const char *str);

void k_strcpy(char *dst, const char *src);

int k_strcmp(const char *a, const char *b);

/* Compare at most n characters. */
int k_strncmp(const char *a, const char *b, size_t n);

/* First occurrence of needle in haystack, or NULL (an empty needle is found at the start). */
const char *k_strstr(const char *haystack, const char *needle);

#endif