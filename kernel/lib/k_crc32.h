/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_LIB_K_CRC32_H
#define KERNEL_LIB_K_CRC32_H

#include <stddef.h>
#include <stdint.h>

/*
 * CRC-32 (IEEE 802.3, polynomial 0xEDB88320 reflected; the one GPT uses).
 * Start with crc = 0 and pass the previous result to continue a stream:
 * k_crc32(k_crc32(0, a, na), b, nb) == k_crc32(0, a||b, na+nb).
 */
uint32_t k_crc32(uint32_t crc, const void *data, size_t size);

#endif
