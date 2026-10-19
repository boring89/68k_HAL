#!/bin/sh
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# A small FAT32 volume for the host tests (tests/host/test_fat.c, test_pram.c): a folder 68k_hal with a
# GRUB environment block (grub-editenv), a text file and an empty Mac parameter RAM file (512 zero bytes). Needs mtools and grub-editenv (grub2-common).
#
# Usage: tools/make_fat_image.sh <output>

set -e

out=$1
[ -n "$out" ] || { echo "usage: $0 <output.img>" >&2; exit 1; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

rm -f "$out"
# 70000 sectors of 512 bytes with one-sector clusters: just enough clusters for FAT32.
mformat -i "$out" -C -F -T 70000 -c 1 -v TESTBOOT ::
mmd -i "$out" ::68k_hal
grub-editenv "$work/grubenv" create
grub-editenv "$work/grubenv" set boot_slot=a system_slot=a trial=
printf 'autostart = mac\n' > "$work/68k_hal.cfg"
head -c 512 /dev/zero > "$work/pram"
mcopy -i "$out" "$work/68k_hal.cfg" "$work/grubenv" "$work/pram" ::68k_hal/
