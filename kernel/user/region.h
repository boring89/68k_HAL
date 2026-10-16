/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_USER_REGION_H
#define KERNEL_USER_REGION_H

#include <stdint.h>

/*
 * Which parts of the address space belong to the running user program.
 * Every pointer a program hands to a system call is checked against
 * this list before the kernel touches it, so a bad pointer gives
 * -EFAULT instead of crashing the kernel.
 *
 * Regions are half-open ranges [start, end) of page-aligned addresses. A
 * region added next to another one is merged with it.
 */

#define USER_MAX_REGIONS 128

typedef struct
{
    uint32_t start;
    uint32_t end;
} user_region_t;

typedef struct
{
    user_region_t region[USER_MAX_REGIONS];
    unsigned int count;
} user_regions_t;

void user_regions_clear(user_regions_t *set);

/* Add [start, end). Returns -1 if it overlaps a region, is empty or the list is full. */
int user_regions_add(user_regions_t *set, uint32_t start, uint32_t end);

/*
 * Remove [start, end) from the regions, cutting or splitting those that
 * it overlaps. Ranges that belong to no region are ignored. Returns -1
 * if a split needs more room than the list has (nothing is changed).
 */
int user_regions_remove(user_regions_t *set, uint32_t start, uint32_t end);

/* 1 if every byte of [address, address + length) is in some region. */
int user_regions_contains(const user_regions_t *set, uint32_t address, uint32_t length);

/* 1 if [start, end) overlaps any region. */
int user_regions_overlaps(const user_regions_t *set, uint32_t start, uint32_t end);

#endif
