#!/usr/bin/env python3
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Write a small synthetic GPT disk image for the host tests
# (tests/host/test_gpt.c): protective MBR, primary header and entries, backup
# entries and header, and a marker in the first block of each partition.
#
# Usage: tools/make_gpt_image.py <output> [boot.img]
#
# With boot.img (e.g. a FAT32 volume from tools/make_fat_image.sh) the Boot partition is that image and
# the other partitions follow it.
#
# Layout and fields: UEFI Specification 2.10, section 5.3 (GPT) and 5.2.3
# (protective MBR). 512-byte blocks.

import binascii
import struct
import sys
import uuid

BLOCK = 512
ENTRY_SIZE = 128
ENTRY_COUNT = 128

APPLE_HFS = "48465300-0000-11AA-AA11-00306543ECAC"
OVERLAY = "68AC0001-4F56-4C59-8D1E-68A1D5C0DE01"
ESP = "C12A7328-F81F-11D2-BA4B-00A0C93EC93B"

# (name, type guid, block count); the partitions follow each other from block 34.
SIZES = [
    ("68k_HAL Boot", ESP, 30),
    ("68k_HAL System A", APPLE_HFS, 40),
    ("68k_HAL System B", APPLE_HFS, 40),
    ("68k_HAL Overlay", OVERLAY, 50),
    ("Applications", APPLE_HFS, 28),
]
PARTITIONS = []
TOTAL = 0


def layout(boot_blocks):
    global TOTAL
    first = 34
    for name, ptype, count in SIZES:
        if boot_blocks and name == "68k_HAL Boot":
            count = boot_blocks
        PARTITIONS.append((name, ptype, first, count))
        first += count
    TOTAL = first + 34


def crc(data):
    return binascii.crc32(data) & 0xFFFFFFFF


def entries_blob():
    blob = b""
    for i, (name, ptype, first, count) in enumerate(PARTITIONS):
        e = uuid.UUID(ptype).bytes_le
        e += uuid.UUID(int=0x1000 + i).bytes_le
        e += struct.pack("<QQQ", first, first + count - 1, 0)
        e += name.encode("utf-16-le").ljust(72, b"\0")
        blob += e
    return blob.ljust(ENTRY_SIZE * ENTRY_COUNT, b"\0")


def header(my_lba, other_lba, entries_lba, entries_crc):
    h = b"EFI PART" + struct.pack("<IIIIQQQQ", 0x10000, 92, 0, 0, my_lba,
                                  other_lba, 34, TOTAL - 34)
    h += uuid.UUID(int=0x68).bytes_le
    h += struct.pack("<QIII", entries_lba, ENTRY_COUNT, ENTRY_SIZE,
                     entries_crc)
    # The CRC covers the 92 bytes with its own field zero.
    h = h[:16] + struct.pack("<I", crc(h)) + h[20:]
    return h.ljust(BLOCK, b"\0")


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit("usage: make_gpt_image.py <output> [boot.img]")

    boot = open(sys.argv[2], "rb").read() if len(sys.argv) == 3 else None
    layout(len(boot) // BLOCK if boot else 0)
    img = bytearray(TOTAL * BLOCK)

    # Protective MBR: one 0xEE partition covering the disk.
    mbr = bytearray(BLOCK)
    mbr[446 + 4] = 0xEE
    struct.pack_into("<II", mbr, 446 + 8, 1, TOTAL - 1)
    mbr[510:512] = b"\x55\xAA"
    img[0:BLOCK] = mbr

    blob = entries_blob()
    blob_crc = crc(blob)
    blocks = len(blob) // BLOCK

    backup_entries = TOTAL - 1 - blocks
    img[BLOCK:2 * BLOCK] = header(1, TOTAL - 1, 2, blob_crc)
    img[2 * BLOCK:2 * BLOCK + len(blob)] = blob
    img[backup_entries * BLOCK:(backup_entries + blocks) * BLOCK] = blob
    img[(TOTAL - 1) * BLOCK:TOTAL * BLOCK] = header(
        TOTAL - 1, 1, backup_entries, blob_crc)

    for name, _, first, _ in PARTITIONS:
        if boot and name == "68k_HAL Boot":
            img[first * BLOCK:first * BLOCK + len(boot)] = boot
            continue
        marker = ("FIRST-BLOCK-OF-" + name).encode("ascii")
        img[first * BLOCK:first * BLOCK + len(marker)] = marker

    with open(sys.argv[1], "wb") as f:
        f.write(img)


main()
