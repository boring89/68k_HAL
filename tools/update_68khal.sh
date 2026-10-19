#!/bin/sh
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Update a 68k_HAL disk (docs/mac_disk.md section 8) without touching what the Mac wrote: the new kernel
# goes into the kernel slot that is NOT in use (/68k_hal/slot_a or slot_b on the Boot partition) and is
# booted once as a trial (grubenv trial=); the kernel makes it the confirmed slot when it starts, and
# GRUB goes back to the old slot if it does not. Optionally a new Mac system image goes into the System
# slot that is not in use. The overlay, Applications and the free space are never written. A disk made
# before the Mac's parameter RAM was kept (P11) gets an empty /68k_hal/pram; an existing one is never
# overwritten (it holds the Mac's control panel settings).
#
# Usage: sudo tools/update_68khal.sh [--dry-run] [--kernel-dir DIR] [--system IMAGE] TARGET
#
#   TARGET          the 68k_HAL disk (/dev/sdX) or an image of one (build/68k_HAL_mac.img)
#   --kernel-dir    where kernel-gfx.elf, kernel-gfx1080.elf, kernel-gfx1440.elf and initrd.tar are
#                   (default: build); --no-kernel leaves the kernel slots alone
#   --system IMAGE  also write IMAGE into the System slot not in use; afterwards, in the maintenance shell,
#                   "macsys switch <slot> force" starts using it (the Mac's changes to the old system are
#                   dropped; they stay in the old slot's overlay only until then)
#   --dry-run       show what would be written, write nothing
#
# Every file is checked (SHA-256) before and after writing. Needs sgdisk, mtools, grub-editenv, sha256sum.

set -eu

dry_run=0
kernel_dir=build
system_image=
update_kernel=1
target=

while [ $# -gt 0 ]; do
    case $1 in
    --dry-run) dry_run=1 ;;
    --kernel-dir) kernel_dir=$2; shift ;;
    --no-kernel) update_kernel=0 ;;
    --system) system_image=$2; shift ;;
    -h|--help) sed -n '2,27p' "$0"; exit 0 ;;
    -*) echo "update_68khal: unknown option $1" >&2; exit 2 ;;
    *) target=$1 ;;
    esac
    shift
done

die() { echo "update_68khal: $*" >&2; exit 1; }

[ -n "$target" ] || die "no target (a 68k_HAL disk such as /dev/sdX, or an image file)"
[ -e "$target" ] || die "$target does not exist"
for tool in sgdisk mcopy mdir grub-editenv sha256sum dd; do
    command -v "$tool" > /dev/null 2>&1 || die "needs $tool"
done

if [ -b "$target" ]; then
    # A real disk: nothing on it may be mounted while it is written. /proc/mounts names the device
    # itself (/dev/sda1), so a link such as /dev/disk/by-id/... is resolved first.
    device=$(readlink -f "$target")
    if grep -q "^$device" /proc/mounts; then
        die "$target (or one of its partitions) is mounted; unmount it first"
    fi
    [ "$(id -u)" = 0 ] || [ "$dry_run" = 1 ] || die "writing $target needs root (sudo)"
elif [ ! -f "$target" ]; then
    die "$target is neither a block device nor a regular file"
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# The partitions, by name: "number start end size unit code name..." lines of sgdisk -p.
sgdisk -p "$target" > "$work/table" 2> /dev/null || die "$target has no readable GPT"
sector=$(sed -n 's/^Sector size (logical): \([0-9]*\) bytes.*/\1/p' "$work/table")
[ -n "$sector" ] || sector=512

part_field() {   # part_field <name> <field>: 2 = first sector, 3 = last sector
    awk -v name="$1" -v f="$2" '$1 ~ /^[0-9]+$/ {
        n = ""; for (i = 7; i <= NF; i++) n = n (n == "" ? "" : " ") $i
        if (n == name) { print $f; exit } }' "$work/table"
}

for name in "68k_HAL Boot" "68k_HAL System A" "68k_HAL System B" "68k_HAL Overlay" "Applications"; do
    [ -n "$(part_field "$name" 2)" ] || die "$target is not a 68k_HAL disk (no partition \"$name\")"
done

boot_offset=$(( $(part_field "68k_HAL Boot" 2) * sector ))
esp="$target@@$boot_offset"

# The GRUB environment block: which slots are in use.
mcopy -n -i "$esp" ::68k_hal/grubenv "$work/grubenv" 2> /dev/null || die "no 68k_hal/grubenv on the Boot partition"
env_get() { grub-editenv "$work/grubenv" list | sed -n "s/^$1=//p"; }
boot_slot=$(env_get boot_slot)
system_slot=$(env_get system_slot)
trial=$(env_get trial)
[ -n "$boot_slot" ] || boot_slot=a
[ -n "$system_slot" ] || system_slot=a
other() { if [ "$1" = a ]; then echo b; else echo a; fi; }
kernel_target=$(other "$boot_slot")
system_target=$(other "$system_slot")

echo "update_68khal: $target: kernel slot in use $boot_slot, System slot in use $system_slot"
[ -z "$trial" ] || echo "update_68khal: note: a trial of kernel slot $trial is still pending; it is replaced"

files="kernel-gfx.elf kernel-gfx1080.elf kernel-gfx1440.elf initrd.tar"

if [ "$update_kernel" = 1 ]; then
    for f in $files; do
        [ -f "$kernel_dir/$f" ] || die "missing $kernel_dir/$f (make all initrd, or --kernel-dir)"
    done
    echo "update_68khal: kernel files -> Boot partition /68k_hal/slot_$kernel_target/, then a trial boot of slot $kernel_target:"
    (cd "$kernel_dir" && sha256sum $files) | sed 's/^/    /'
fi

if [ -n "$system_image" ]; then
    [ -f "$system_image" ] || die "no such file $system_image"
    name="68k_HAL System $(echo "$system_target" | tr ab AB)"
    first=$(part_field "$name" 2)
    last=$(part_field "$name" 3)
    room=$(( (last - first + 1) * sector ))
    size=$(stat -c %s "$system_image")
    [ "$size" -le "$room" ] || die "$system_image ($size bytes) is larger than $name ($room bytes)"
    echo "update_68khal: $system_image ($size bytes) -> \"$name\" (sectors $first-$last, not in use):"
    sha256sum "$system_image" | sed 's/^/    /'
fi

[ "$update_kernel" = 1 ] || [ -n "$system_image" ] || die "nothing to do (--no-kernel without --system)"

# The Mac's parameter RAM (kernel/mac/pram.h): created empty when missing, otherwise left alone.
need_pram=0
if ! mdir -i "$esp" ::68k_hal/pram > /dev/null 2>&1; then
    need_pram=1
    echo "update_68khal: /68k_hal/pram (the Mac's parameter RAM) is missing: an empty one is created"
fi

if [ "$dry_run" = 1 ]; then
    echo "update_68khal: dry run: nothing written"
    exit 0
fi

if [ "$update_kernel" = 1 ]; then
    for f in $files; do
        mcopy -o -i "$esp" "$kernel_dir/$f" "::68k_hal/slot_$kernel_target/$f"
        mcopy -n -i "$esp" "::68k_hal/slot_$kernel_target/$f" "$work/check"
        [ "$(sha256sum < "$work/check")" = "$(sha256sum < "$kernel_dir/$f")" ] || die "slot_$kernel_target/$f differs after writing"
    done

    grub-editenv "$work/grubenv" set trial="$kernel_target"
    mcopy -o -i "$esp" "$work/grubenv" ::68k_hal/grubenv
    mcopy -n -i "$esp" ::68k_hal/grubenv "$work/check"
    cmp -s "$work/check" "$work/grubenv" || die "grubenv differs after writing"
    echo "update_68khal: kernel slot $kernel_target written and checked; the next boot tries it"
fi

if [ "$need_pram" = 1 ]; then
    head -c 512 /dev/zero > "$work/pram"
    mcopy -D s -i "$esp" "$work/pram" ::68k_hal/pram
    mcopy -n -i "$esp" ::68k_hal/pram "$work/check"
    cmp -s "$work/check" "$work/pram" || die "68k_hal/pram differs after writing"
fi

if [ -n "$system_image" ]; then
    dd if="$system_image" of="$target" bs=1M seek=$(( first * sector )) oflag=seek_bytes conv=notrunc,fsync status=none
    dd if="$target" bs=1M skip=$(( first * sector )) count="$size" iflag=skip_bytes,count_bytes status=none > "$work/check"
    [ "$(sha256sum < "$work/check")" = "$(sha256sum < "$system_image")" ] || die "the System slot differs after writing"
    echo "update_68khal: System slot $system_target written and checked."
    echo "update_68khal: to use it: choose the maintenance shell, then: macsys switch $system_target force"
fi

sync
