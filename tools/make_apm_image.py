#!/usr/bin/env python3
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Write a synthetic Apple Partition Map disk image for the host tests
# (tests/host/test_apm.c). Everything is generated here; no Apple software is
# involved.
#
# Usage: tools/make_apm_image.py <output>
#
# Layout (512-byte blocks):
#   0       Driver Descriptor Map ("ER")
#   1-4     partition map entries ("PM"): the map itself, a driver, an HFS
#           partition and free space
#   5-...   the partitions; the HFS partition's first block holds a marker
#
# Field layout: Inside Macintosh: Devices, SCSI Manager chapter (Apple
# Partition Map); all numbers are big-endian.

import struct
import sys

BLOCK = 512

# (name, type, start block, block count, status)
PARTITIONS = [
    ("Apple", "Apple_partition_map", 1, 4, 0x3),
    ("Macintosh", "Apple_Driver43", 8, 8, 0x7F),
    ("Test volume", "Apple_HFS", 16, 32, 0xB7),
    ("Extra", "Apple_Free", 48, 16, 0x0),
]

TOTAL_BLOCKS = 64
MARKER = b"FIRST-BLOCK-OF-THE-HFS-PARTITION"


def pad(text, size):
    return text.encode("ascii").ljust(size, b"\0")


def ddm_block():
    # sbSig, sbBlkSize, sbBlkCount, sbDevType, sbDevId, sbData, sbDrvrCount
    block = struct.pack(">2sHIHHIH", b"ER", BLOCK, TOTAL_BLOCKS, 0, 0, 0, 0)
    return block.ljust(BLOCK, b"\0")


def map_entry(index):
    name, ptype, start, count, status = PARTITIONS[index]
    # pmSig, pmSigPad, pmMapBlkCnt, pmPyPartStart, pmPartBlkCnt
    entry = struct.pack(">2sHIII", b"PM", 0, len(PARTITIONS), start, count)
    entry += pad(name, 32) + pad(ptype, 32)
    # pmLgDataStart, pmDataCnt, pmPartStatus
    entry += struct.pack(">III", 0, count, status)
    return entry.ljust(BLOCK, b"\0")


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: make_apm_image.py <output>")

    image = bytearray(TOTAL_BLOCKS * BLOCK)
    image[0:BLOCK] = ddm_block()
    for i in range(len(PARTITIONS)):
        image[(1 + i) * BLOCK:(2 + i) * BLOCK] = map_entry(i)
    hfs_start = PARTITIONS[2][2] * BLOCK
    image[hfs_start:hfs_start + len(MARKER)] = MARKER

    with open(sys.argv[1], "wb") as f:
        f.write(image)


if __name__ == "__main__":
    main()
