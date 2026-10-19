#!/usr/bin/env python3
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build a 68k_HAL disk image (docs/mac_disk.md): a GPT disk with
#
#   68k_HAL Boot      FAT32, the EFI system partition (GRUB, kernels, ROM, config)
#   68k_HAL System A  the Mac system image, never modified
#   68k_HAL System B  a copy of System A (the other slot)
#   68k_HAL Overlay   the Mac's writes to the system disk (empty: the kernel formats it)
#   Applications      an HFS volume
#   68k_HAL Data      an eHFS volume (only with --data-mib; on a real disk tools/add_data_partition.sh
#                     adds it in the free space)
#
# Only ever writes the image FILE it is given; refuses block devices.
#
# Usage: make_mac_disk.py --out IMAGE --system SYSTEM_IMAGE
#            [--esp-dir DIR] [--boot-mib N] [--system-mib N] [--overlay-mib N]
#            [--apps-mib N] [--data-mib N] [--ehfs-tools DIR]
#
# --esp-dir: files copied into the FAT32 partition (mcopy -s). Needs mtools
# and hfsutils (hformat). The partition table is written with sgdisk.
# --data-mib: the data partition's size (default 0: none); its volume is made
# with mkfs.ehfs from --ehfs-tools (default build/host/ehfs).

import argparse
import os
import shutil
import stat
import subprocess
import sys
import tempfile

MIB = 1024 * 1024
SECTOR = 512
ALIGN = MIB

ESP = "C12A7328-F81F-11D2-BA4B-00A0C93EC93B"
APPLE_HFS = "48465300-0000-11AA-AA11-00306543ECAC"
OVERLAY = "68AC0001-4F56-4C59-8D1E-68A1D5C0DE01"
DATA = "68AC0002-4441-5441-8D1E-68A1D5C0DE01"

# Fixed partition GUIDs, so the same inputs give the same image.
UNIQUE = {
    "boot": "68AC1001-0000-4000-8000-000000000001",
    "sysa": "68AC1001-0000-4000-8000-000000000002",
    "sysb": "68AC1001-0000-4000-8000-000000000003",
    "ovl": "68AC1001-0000-4000-8000-000000000004",
    "apps": "68AC1001-0000-4000-8000-000000000005",
    "data": "68AC1001-0000-4000-8000-000000000006",
}
DISK_GUID = "68AC1001-0000-4000-8000-000000000000"


def run(cmd, **kw):
    subprocess.run(cmd, check=True, **kw)


def copy_sparse(src_path, dst, offset, limit):
    """Copy a file into dst (an open file) at offset, skipping all-zero blocks."""
    size = os.path.getsize(src_path)
    if size > limit:
        sys.exit("%s (%d bytes) does not fit in %d bytes" % (src_path, size, limit))
    with open(src_path, "rb") as src:
        pos = 0
        while True:
            chunk = src.read(MIB)
            if not chunk:
                break
            if chunk.count(0) != len(chunk):
                dst.seek(offset + pos)
                dst.write(chunk)
            pos += len(chunk)


def make_esp(path, size, esp_dir):
    run(["mformat", "-i", path, "-C", "-F", "-T", str(size // SECTOR), "-v", "68KHALBOOT", "::"])
    if esp_dir:
        entries = [os.path.join(esp_dir, e) for e in sorted(os.listdir(esp_dir))]
        if entries:
            run(["mcopy", "-i", path, "-s", "-m"] + entries + ["::"])


def make_apps(path, size):
    with open(path, "wb") as f:
        f.truncate(size)
    home = tempfile.mkdtemp()
    try:
        env = dict(os.environ, HOME=home)  # hfsutils keeps state in ~/.hcwd
        run(["hformat", "-f", "-l", "Applications", path, "0"], env=env,
            stdout=subprocess.DEVNULL)
    finally:
        shutil.rmtree(home)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--system", required=True, help="the Mac system disk image (a bare HFS volume)")
    ap.add_argument("--esp-dir")
    ap.add_argument("--boot-mib", type=int, default=1024)
    ap.add_argument("--system-mib", type=int, default=1024)
    ap.add_argument("--overlay-mib", type=int, default=4096)
    ap.add_argument("--apps-mib", type=int, default=2048)
    ap.add_argument("--data-mib", type=int, default=0)
    ap.add_argument("--ehfs-tools", default="build/host/ehfs")
    args = ap.parse_args()

    if os.path.exists(args.out) and not stat.S_ISREG(os.stat(args.out).st_mode):
        sys.exit("refusing to write %s: not a regular file" % args.out)

    parts = [
        ("boot", "68k_HAL Boot", ESP, args.boot_mib * MIB),
        ("sysa", "68k_HAL System A", APPLE_HFS, args.system_mib * MIB),
        ("sysb", "68k_HAL System B", APPLE_HFS, args.system_mib * MIB),
        ("ovl", "68k_HAL Overlay", OVERLAY, args.overlay_mib * MIB),
        ("apps", "Applications", APPLE_HFS, args.apps_mib * MIB),
    ]
    if args.data_mib > 0:
        parts.append(("data", "68k_HAL Data", DATA, args.data_mib * MIB))

    offset = ALIGN
    layout = []
    for key, name, ptype, size in parts:
        layout.append((key, name, ptype, offset, size))
        offset += size
    total = offset + ALIGN      # room for the backup GPT

    if os.path.exists(args.out):
        os.remove(args.out)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "wb") as f:
        f.truncate(total)

    cmd = ["sgdisk", "-o", "-U", DISK_GUID]
    for i, (key, name, ptype, off, size) in enumerate(layout, 1):
        cmd += ["-n", "%d:%d:%d" % (i, off // SECTOR, (off + size) // SECTOR - 1),
                "-t", "%d:%s" % (i, ptype), "-c", "%d:%s" % (i, name),
                "-u", "%d:%s" % (i, UNIQUE[key])]
    run(cmd + [args.out], stdout=subprocess.DEVNULL)

    with tempfile.TemporaryDirectory() as tmp, open(args.out, "r+b") as img:
        for key, name, ptype, off, size in layout:
            if key == "boot":
                part = os.path.join(tmp, "boot.img")
                make_esp(part, size, args.esp_dir)
            elif key == "apps":
                part = os.path.join(tmp, "apps.img")
                make_apps(part, size)
            elif key in ("sysa", "sysb"):
                part = args.system
            else:
                continue
            copy_sparse(part, img, off, size)

    for key, name, ptype, off, size in layout:
        if key == "data":
            run([os.path.join(args.ehfs_tools, "mkfs.ehfs"), "-L", "Data", "--offset", str(off // SECTOR),
                 "--sectors", str(size // SECTOR), args.out], stdout=subprocess.DEVNULL)


main()
