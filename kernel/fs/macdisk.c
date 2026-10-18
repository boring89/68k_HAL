/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "macdisk.h"

#include "gpt.h"

enum
{
    PART_BOOT,
    PART_SYSTEM_A,
    PART_SYSTEM_B,
    PART_OVERLAY,
    PART_APPS,
    PART_COUNT
};

static const char *const part_names[PART_COUNT] = {
    GPT_NAME_BOOT, GPT_NAME_SYSTEM_A, GPT_NAME_SYSTEM_B, GPT_NAME_OVERLAY,
    GPT_NAME_APPLICATIONS};

static const char *const device_names[PART_COUNT] = {
    "boot", "sys_a", "sys_b", "ovl_store", "apps"};

static gpt_view_t views[PART_COUNT];
static block_device_t *parts[PART_COUNT];
static overlay_t overlay;
static overlay_status_t status = OVERLAY_ERROR;
static int present;
static int overlay_ok;
static uint32_t active_slot;

/* Register every partition of the layout found on disk. 0 if System A and the overlay are there. */
static int register_partitions(block_device_t *disk)
{
    gpt_partition_t found[PART_COUNT];

    for (int i = 0; i < PART_COUNT; i++)
    {
        if (gpt_find_by_name(disk, part_names[i], &found[i]) != 0)
            found[i].block_count = 0;
    }

    if (found[PART_SYSTEM_A].block_count == 0 ||
        found[PART_OVERLAY].block_count == 0)
        return -1;

    for (int i = 0; i < PART_COUNT; i++)
    {
        parts[i] = NULL;

        if (found[i].block_count != 0 &&
            gpt_view_register(&views[i], device_names[i], disk, found[i].start,
                              found[i].block_count) == 0)
            parts[i] = &views[i].device;
    }

    return 0;
}

int macdisk_init(uint32_t slot)
{
    macdisk_forget();

    for (unsigned int i = 0; i < block_device_count(); i++)
    {
        block_device_t *disk = block_device_get(i);

        if (gpt_find_by_name(disk, GPT_NAME_SYSTEM_A, &(gpt_partition_t){0}) != 0)
            continue;

        if (register_partitions(disk) != 0)
            continue;

        present = 1;
        active_slot = slot == MACDISK_SLOT_B ? MACDISK_SLOT_B : MACDISK_SLOT_A;

        block_device_t *base = macdisk_slot_device(active_slot);
        block_device_t *store = parts[PART_OVERLAY];

        if (base == NULL || store == NULL)
        {
            status = OVERLAY_ERROR;
            return 0;
        }

        status = overlay_open(&overlay, "system", base, store, active_slot);

        if (status == OVERLAY_EMPTY)
        {
            /* A fresh overlay partition: bind it to this System slot. */
            if (overlay_format(store, base, active_slot) == 0)
                status = overlay_open(&overlay, "system", base, store,
                                      active_slot);
            else
                status = OVERLAY_ERROR;
        }

        overlay_ok = status == OVERLAY_OK;

        return 0;
    }

    return -1;
}

uint32_t macdisk_slot_from_name(const char *name)
{
    if (name != NULL && (name[0] == 'b' || name[0] == 'B') && name[1] == '\0')
        return MACDISK_SLOT_B;

    return MACDISK_SLOT_A;
}

int macdisk_present(void)
{
    return present;
}

uint32_t macdisk_slot(void)
{
    return active_slot;
}

block_device_t *macdisk_system(void)
{
    return overlay_ok ? &overlay.device : NULL;
}

block_device_t *macdisk_apps(void)
{
    return parts[PART_APPS];
}

block_device_t *macdisk_boot(void)
{
    return parts[PART_BOOT];
}

block_device_t *macdisk_slot_device(uint32_t slot)
{
    return parts[slot == MACDISK_SLOT_B ? PART_SYSTEM_B : PART_SYSTEM_A];
}

block_device_t *macdisk_overlay_store(void)
{
    return parts[PART_OVERLAY];
}

overlay_status_t macdisk_overlay_status(void)
{
    return status;
}

const overlay_t *macdisk_overlay(void)
{
    return overlay_ok ? &overlay : NULL;
}

int macdisk_reset(void)
{
    if (!present)
        return -1;

    if (overlay_ok)
        return overlay_reset(&overlay);

    block_device_t *base = macdisk_slot_device(active_slot);
    block_device_t *store = parts[PART_OVERLAY];

    if (base == NULL || store == NULL || overlay_format(store, base, active_slot) != 0)
        return -1;

    status = overlay_open(&overlay, "system", base, store, active_slot);
    overlay_ok = status == OVERLAY_OK;

    return overlay_ok ? 0 : -1;
}

void macdisk_forget(void)
{
    present = 0;
    overlay_ok = 0;
    status = OVERLAY_ERROR;

    for (int i = 0; i < PART_COUNT; i++)
        parts[i] = NULL;
}
