/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "macdisk.h"

#include "gpt.h"

#include "kernel/block/cache.h"
#include "fat.h"

#include "kernel/lib/k_crc32.h"
#include "kernel/lib/k_string.h"

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

        /* Everything the Mac reads comes from this disk: cache it (a no-op without a cache). */
        block_cache_enable(disk);
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

#define ENV_PATH "68k_hal/grubenv"

int macdisk_env_read(grubenv_t *env)
{
    fat_file_t file;
    char block[GRUBENV_SIZE];

    if (!present || parts[PART_BOOT] == NULL || fat_find(parts[PART_BOOT], ENV_PATH, &file) != 0 ||
        file.size != GRUBENV_SIZE || fat_read(parts[PART_BOOT], &file, block, sizeof block) != GRUBENV_SIZE)
        return -1;

    return grubenv_parse(env, block, sizeof block);
}

int macdisk_env_write(const grubenv_t *env)
{
    fat_file_t file;
    char block[GRUBENV_SIZE];

    if (!present || parts[PART_BOOT] == NULL || fat_find(parts[PART_BOOT], ENV_PATH, &file) != 0 ||
        file.size != GRUBENV_SIZE || grubenv_format(env, block) != 0)
        return -1;

    return fat_overwrite(parts[PART_BOOT], &file, block);
}

int macdisk_confirm_kernel_slot(const char *kernel_slot, int confirm)
{
    grubenv_t env;

    if (!confirm || kernel_slot == NULL || (k_strcmp(kernel_slot, "a") != 0 && k_strcmp(kernel_slot, "b") != 0))
        return 0;

    if (macdisk_env_read(&env) != 0)
        return -1;

    const char *now = grubenv_get(&env, "boot_slot");

    if (now != NULL && k_strcmp(now, kernel_slot) == 0)
        return 0;

    if (grubenv_set(&env, "boot_slot", kernel_slot) != 0 || macdisk_env_write(&env) != 0)
        return -1;

    return 1;
}

/* Remember the System slot for the next boots (GRUB passes it as system_slot=). */
static int save_system_slot(uint32_t slot)
{
    grubenv_t env;

    if (macdisk_env_read(&env) != 0 || grubenv_set(&env, "system_slot", slot == MACDISK_SLOT_B ? "b" : "a") != 0)
        return -1;

    return macdisk_env_write(&env);
}

int macdisk_switch(uint32_t slot, int force)
{
    block_device_t *base = macdisk_slot_device(slot);
    block_device_t *store = parts[PART_OVERLAY];

    if (!present || base == NULL || store == NULL)
        return -1;

    if (overlay_ok && overlay_used_blocks(&overlay) != 0 && !force)
        return MACDISK_OVERLAY_NOT_EMPTY;

    if (overlay_ok && overlay_rebind(&overlay, base, slot) != 0)
        return -1;

    if (!overlay_ok)
    {
        if (overlay_format(store, base, slot) != 0)
            return -1;

        status = overlay_open(&overlay, "system", base, store, slot);
        overlay_ok = status == OVERLAY_OK;

        if (!overlay_ok)
            return -1;
    }

    status = OVERLAY_OK;
    active_slot = slot;

    return save_system_slot(slot) == 0 ? 0 : MACDISK_NOT_SAVED;
}

#define PROMOTE_CHUNK_BLOCKS 64

int macdisk_promote(void (*progress)(unsigned int percent))
{
    static uint8_t chunk[PROMOTE_CHUNK_BLOCKS * 512];
    uint32_t other = active_slot == MACDISK_SLOT_A ? MACDISK_SLOT_B : MACDISK_SLOT_A;
    block_device_t *from = macdisk_system();
    block_device_t *to = macdisk_slot_device(other);

    if (from == NULL || to == NULL || to->block_size != from->block_size || from->block_size != 512 ||
        to->block_count < from->block_count)
        return -1;

    uint32_t crc_from = 0;
    unsigned int shown = 101;

    /* The copy: the merged view, chunk by chunk. */
    for (uint64_t lba = 0; lba < from->block_count; lba += PROMOTE_CHUNK_BLOCKS)
    {
        uint32_t n = from->block_count - lba < PROMOTE_CHUNK_BLOCKS ? (uint32_t)(from->block_count - lba)
                                                                   : PROMOTE_CHUNK_BLOCKS;

        if (block_read(from, lba, n, chunk) != 0 || block_write(to, lba, n, chunk) != 0)
            return -1;

        crc_from = k_crc32(crc_from, chunk, (size_t)n * 512);

        unsigned int percent = (unsigned int)(lba * 90 / from->block_count);

        if (progress != NULL && percent / 10 != shown / 10)
            progress(shown = percent);
    }

    /* The check: read the new slot back. */
    uint32_t crc_to = 0;

    for (uint64_t lba = 0; lba < from->block_count; lba += PROMOTE_CHUNK_BLOCKS)
    {
        uint32_t n = from->block_count - lba < PROMOTE_CHUNK_BLOCKS ? (uint32_t)(from->block_count - lba)
                                                                   : PROMOTE_CHUNK_BLOCKS;

        if (block_read(to, lba, n, chunk) != 0)
            return -1;

        crc_to = k_crc32(crc_to, chunk, (size_t)n * 512);
    }

    if (crc_to != crc_from)
        return -1;

    if (progress != NULL)
        progress(95);

    /* The other slot now holds everything: switch to it with an empty overlay. */
    int result = macdisk_switch(other, 1);

    if (progress != NULL && result == 0)
        progress(100);

    return result;
}

void macdisk_forget(void)
{
    present = 0;
    overlay_ok = 0;
    status = OVERLAY_ERROR;

    for (int i = 0; i < PART_COUNT; i++)
        parts[i] = NULL;
}
