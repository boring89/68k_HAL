/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_LIB_K_CRC32C_H
#define KERNEL_LIB_K_CRC32C_H

#include <stddef.h>
#include <stdint.h>

/*
 * CRC-32C (Castagnoli, polynomial 0x82F63B78 reflected; RFC 3720 appendix B,
 * the one ext4 and iSCSI use; eHFS checks its metadata with it). Chained like
 * k_crc32: k_crc32c(k_crc32c(0, a, na), b, nb) == k_crc32c(0, a||b, na+nb).
 */
uint32_t k_crc32c(uint32_t crc, const void *data, size_t size);

#endif
