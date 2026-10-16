/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "region.h"

void user_regions_clear(user_regions_t *set)
{
    set->count = 0;
}

int user_regions_overlaps(const user_regions_t *set, uint32_t start, uint32_t end)
{
    for (unsigned int i = 0; i < set->count; i++)
    {
        if (start < set->region[i].end && set->region[i].start < end)
            return 1;
    }

    return 0;
}

int user_regions_add(user_regions_t *set, uint32_t start, uint32_t end)
{
    if (start >= end || set->count == USER_MAX_REGIONS)
        return -1;

    if (user_regions_overlaps(set, start, end))
        return -1;

    set->region[set->count].start = start;
    set->region[set->count].end = end;
    set->count++;

    return 0;
}

int user_regions_remove(user_regions_t *set, uint32_t start, uint32_t end)
{
    if (start >= end)
        return 0;

    /* A split adds one region: make sure there is room for every split first. */
    unsigned int splits = 0;

    for (unsigned int i = 0; i < set->count; i++)
    {
        if (set->region[i].start < start && end < set->region[i].end)
            splits++;
    }

    if (set->count + splits > USER_MAX_REGIONS)
        return -1;

    unsigned int count = set->count;

    for (unsigned int i = 0; i < count; i++)
    {
        user_region_t *r = &set->region[i];

        if (end <= r->start || r->end <= start)
            continue;

        if (start <= r->start && r->end <= end)
        {
            /* Gone entirely: move the last region into the hole. */
            *r = set->region[--count];
            i--;
        }
        else if (start <= r->start)
        {
            r->start = end;
        }
        else if (r->end <= end)
        {
            r->end = start;
        }
        else
        {
            set->region[count].start = end;
            set->region[count].end = r->end;
            count++;
            r->end = start;
        }
    }

    set->count = count;

    return 0;
}

int user_regions_contains(const user_regions_t *set, uint32_t address, uint32_t length)
{
    if (length == 0)
        return 1;

    if (address + length < address)
        return 0;

    uint32_t end = address + length;

    /* Extend the covered prefix one region at a time. */
    uint32_t covered = address;

    for (;;)
    {
        int advanced = 0;

        for (unsigned int i = 0; i < set->count; i++)
        {
            if (set->region[i].start <= covered && covered < set->region[i].end)
            {
                covered = set->region[i].end;
                advanced = 1;
                break;
            }
        }

        if (!advanced)
            return 0;

        if (covered >= end)
            return 1;
    }
}
