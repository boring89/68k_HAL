/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_INTERRUPT_PAGE_FAULT_H
#define KERNEL_INTERRUPT_PAGE_FAULT_H

#include <stddef.h>
#include <stdint.h>

/* Bytes needed for the longest description (all five bits) plus NUL. */
#define PAGE_FAULT_DESCRIBE_MAX 67

/*
 * Write a readable form of a page fault error code into buf, e.g.
 * "not present, write, kernel". Returns 0, or -1 if buf is NULL or too
 * small; buf then holds an empty string when size > 0.
 */
int page_fault_describe(uint32_t err_code, char *buf, size_t size);

#endif
