#!/usr/bin/env python3
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Wrap a bare HFS volume image in an Apple Partition Map, giving a disk
# image like a real Mac hard disk (see docs/hfs.md, section 6).
#
# Usage: tools/make_disk_image.py <hfs.img> <output>
#
# Layout (512-byte blocks): 0 = Driver Descriptor Map, 1-2 = two map
# entries (the map itself and the HFS partition), HFS volume from block 64.

import struct
import sys

BLOCK = 512
HFS_START = 64


def pad(text, size):
    return text.encode("ascii").ljust(size, b"\0")


def entry(name, ptype, start, count, status):
    data = struct.pack(">2sHIII", b"PM", 0, 2, start, count)
    data += pad(name, 32) + pad(ptype, 32)
    data += struct.pack(">III", 0, count, status)
    return data.ljust(BLOCK, b"\0")


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: make_disk_image.py <hfs.img> <output>")

    with open(sys.argv[1], "rb") as f:
        hfs = f.read()
    if len(hfs) % BLOCK:
        sys.exit("HFS image size is not a multiple of 512")

    hfs_blocks = len(hfs) // BLOCK
    total = HFS_START + hfs_blocks

    disk = bytearray(total * BLOCK)
    ddm = struct.pack(">2sHIHHIH", b"ER", BLOCK, total, 0, 0, 0, 0)
    disk[0:len(ddm)] = ddm
    disk[BLOCK:2 * BLOCK] = entry("Apple", "Apple_partition_map", 1, 63, 0x3)
    disk[2 * BLOCK:3 * BLOCK] = entry("TESTVOL", "Apple_HFS", HFS_START,
                                      hfs_blocks, 0xB7)
    disk[HFS_START * BLOCK:] = hfs

    with open(sys.argv[2], "wb") as f:
        f.write(disk)


if __name__ == "__main__":
    main()
