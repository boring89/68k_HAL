#!/usr/bin/env python3
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Print the table of emu/mac/mac_roman.c: the Unicode character of each Mac Roman byte from 0x80
# to 0xFF, taken from Python's own "mac_roman" codec (not from Apple's ROMAN.TXT). One change:
# 0xDB is the currency sign U+00A4, as in System 7.5's fonts; the codec has the euro sign there,
# which Apple put at 0xDB only in Mac OS 8.5.
#
# Usage: python3 -I tools/gen_mac_roman.py

table = [ord(bytes([b]).decode("mac_roman")) for b in range(0x80, 0x100)]
table[0xDB - 0x80] = 0x00A4
assert len(set(table)) == 128

for row in range(0, 128, 8):
    print("    " + " ".join("0x%04X," % c for c in table[row:row + 8]) + "   /* 0x%02X */" % (0x80 + row))
