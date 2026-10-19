/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_MAC_PRAM_H
#define KERNEL_MAC_PRAM_H

#include <stdint.h>

#include "kernel/block/block.h"

/*
 * The Mac's parameter RAM (the 256 bytes of the clock chip: control panel
 * settings such as mouse speed, volume, startup disk, the video card's mode)
 * kept between starts in a file on the 68k_HAL disk's Boot partition,
 * 68k_hal/pram (docs/mac_disk.md). The file exists from the start
 * (tools/make_mac_disk.py, tools/update_68khal.sh) and keeps its size: it is
 * rewritten in place like the GRUB environment block (kernel/fs/fat.h).
 *
 * The file is one 512-byte sector, so that a write is all or nothing (the
 * same assumption the overlay makes, kernel/block/block.h). Little-endian:
 *   0  "68kHALPR"
 *   8  version (1)
 *   12 length of the parameter RAM (256)
 *   16 CRC-32 of the parameter RAM (kernel/lib/k_crc32.h)
 *   32 the parameter RAM
 * All zeros = empty (a new disk, or after macsys pram reset): the Mac starts
 * with fresh parameter RAM, as it did before it was kept.
 */

#define MAC_PRAM_SIZE 256
#define MAC_PRAM_FILE_SIZE 512
#define MAC_PRAM_PATH "68k_hal/pram"

typedef enum
{
    MAC_PRAM_OK,        /* read */
    MAC_PRAM_EMPTY,     /* the file is there and empty */
    MAC_PRAM_NO_FILE,   /* no Boot partition, no file, or not 512 bytes */
    MAC_PRAM_DAMAGED,   /* the file holds something else (wrong magic, version or CRC) */
    MAC_PRAM_IO_ERROR
} mac_pram_status_t;

/* The file's bytes for xpram. */
void mac_pram_encode(const uint8_t xpram[MAC_PRAM_SIZE], uint8_t file[MAC_PRAM_FILE_SIZE]);

/* Take xpram out of the file's bytes: MAC_PRAM_OK, MAC_PRAM_EMPTY or MAC_PRAM_DAMAGED. */
mac_pram_status_t mac_pram_decode(const uint8_t file[MAC_PRAM_FILE_SIZE], uint8_t xpram[MAC_PRAM_SIZE]);

/* Read the file on boot (the FAT32 Boot partition). xpram is changed only for MAC_PRAM_OK. */
mac_pram_status_t mac_pram_load(block_device_t *boot, uint8_t xpram[MAC_PRAM_SIZE]);

/* Write xpram to the file. Returns 0, or -1 (no usable file, or an I/O error). */
int mac_pram_save(block_device_t *boot, const uint8_t xpram[MAC_PRAM_SIZE]);

/* Make the file empty (the next Mac starts with fresh parameter RAM). Returns 0, or -1. */
int mac_pram_clear(block_device_t *boot);

/* A short English description of a status, for messages. */
const char *mac_pram_status_name(mac_pram_status_t status);

#endif
