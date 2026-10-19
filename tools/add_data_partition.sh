#!/bin/sh
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Add the data partition to a 68k_HAL disk (docs/mac_disk.md section 8.4): a GPT partition "68k_HAL Data"
# in the free space after the other partitions, holding an empty eHFS volume (docs/ehfs.md). The kernel
# shows it at /ehfs and gives it to the Mac as a volume of its desktop. Nothing else on the disk is
# written: only the partition table (both copies) and the new partition.
#
# Usage: sudo tools/add_data_partition.sh [--dry-run] [--size MIB] [--name NAME] [--tools DIR] TARGET
#
#   TARGET      the 68k_HAL disk (/dev/sdX) or an image of one (build/68k_HAL_mac.img)
#   --size MIB  how large (default: the largest free space, normally the rest of the disk)
#   --name      the volume's name (default: Data)
#   --tools     where mkfs.ehfs and fsck.ehfs are (default: build/host/ehfs, from make ehfs-tools)
#   --dry-run   show what would be done, write nothing
#
# Refused: a disk that is not a 68k_HAL disk, one that already has the data partition, sectors other than
# 512 bytes, less than 64 MiB of free space, a disk with something mounted. Needs sgdisk.

set -eu

dry_run=0
size_mib=
name=Data
tools=build/host/ehfs
target=

while [ $# -gt 0 ]; do
    case $1 in
    --dry-run) dry_run=1 ;;
    --size) size_mib=$2; shift ;;
    --name) name=$2; shift ;;
    --tools) tools=$2; shift ;;
    -h|--help) sed -n '2,21p' "$0"; exit 0 ;;
    -*) echo "add_data_partition: unknown option $1" >&2; exit 2 ;;
    *) target=$1 ;;
    esac
    shift
done

die() { echo "add_data_partition: $*" >&2; exit 1; }

DATA_NAME="68k_HAL Data"
DATA_TYPE=68AC0002-4441-5441-8D1E-68A1D5C0DE01     # kernel/fs/gpt.h GPT_TYPE_DATA

[ -n "$target" ] || die "no target (a 68k_HAL disk such as /dev/sdX, or an image file)"
[ -e "$target" ] || die "$target does not exist"
command -v sgdisk > /dev/null 2>&1 || die "needs sgdisk"
[ -x "$tools/mkfs.ehfs" ] && [ -x "$tools/fsck.ehfs" ] || die "no mkfs.ehfs and fsck.ehfs in $tools (make ehfs-tools)"
case $size_mib in
    ''|*[!0-9]*) [ -z "$size_mib" ] || die "--size takes a number of MiB" ;;
esac

device_flag=
if [ -b "$target" ]; then
    # A real disk: nothing on it may be mounted (/proc/mounts names /dev/sda1, so links are resolved first).
    device=$(readlink -f "$target")
    if grep -q "^$device" /proc/mounts; then
        die "$target (or one of its partitions) is mounted; unmount it first"
    fi
    [ "$(id -u)" = 0 ] || [ "$dry_run" = 1 ] || die "writing $target needs root (sudo)"
    device_flag=--device
elif [ ! -f "$target" ]; then
    die "$target is neither a block device nor a regular file"
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

sgdisk -p "$target" > "$work/table" 2> /dev/null || die "$target has no readable GPT"
sector=$(sed -n 's/^Sector size (logical): \([0-9]*\) bytes.*/\1/p' "$work/table")
[ "${sector:-512}" = 512 ] || die "$target has $sector-byte sectors; eHFS needs 512"

has_part() {
    awk -v name="$1" '$1 ~ /^[0-9]+$/ {
        n = ""; for (i = 7; i <= NF; i++) n = n (n == "" ? "" : " ") $i
        if (n == name) found = 1 } END { exit !found }' "$work/table"
}

for part in "68k_HAL Boot" "68k_HAL System A" "68k_HAL System B"; do
    has_part "$part" || die "$target is not a 68k_HAL disk (no \"$part\" partition)"
done
has_part "$DATA_NAME" && die "$target already has the \"$DATA_NAME\" partition"

# The largest free space: its first (aligned) and last sector.
first=$(sgdisk -F "$target" 2> /dev/null | tail -n 1)
last=$(sgdisk -E "$target" 2> /dev/null | tail -n 1)
case "$first$last" in
    ''|*[!0-9]*) die "cannot find free space on $target" ;;
esac
[ "$last" -gt "$first" ] || die "no free space on $target"
if [ -n "$size_mib" ]; then
    want=$((size_mib * 2048))
    [ $((first + want - 1)) -le "$last" ] || die "$size_mib MiB do not fit (at most $(((last - first + 1) / 2048)) MiB free)"
    last=$((first + want - 1))
fi
sectors=$((last - first + 1))
[ "$sectors" -ge $((64 * 2048)) ] || die "only $((sectors / 2048)) MiB free; the data partition needs 64 MiB or more"

echo "add_data_partition: $target: \"$DATA_NAME\", sectors $first-$last ($((sectors / 2048)) MiB), eHFS volume \"$name\""
if [ "$dry_run" = 1 ]; then
    echo "add_data_partition: dry run: nothing written"
    exit 0
fi

sgdisk -n "0:$first:$last" -t "0:$DATA_TYPE" -c "0:$DATA_NAME" "$target" > /dev/null || die "sgdisk failed"
"$tools/mkfs.ehfs" $device_flag -L "$name" --offset "$first" --sectors "$sectors" "$target" || die "mkfs.ehfs failed"
"$tools/fsck.ehfs" $device_flag --offset "$first" "$target" > /dev/null || die "fsck.ehfs finds the new volume damaged"
echo "add_data_partition: done"
