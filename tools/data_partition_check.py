#!/usr/bin/env python3
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# make test-data-partition: tools/add_data_partition.sh against small synthetic 68k_HAL disk IMAGES (made by
# tools/make_mac_disk.py; build/images/hfs.img stands in for the System). Checks that a dry run writes
# nothing; that the partition is added in the free space with its name and type, holds a clean eHFS volume
# and leaves every other byte of the image as it was (apart from the two copies of the partition table);
# that --size is kept; and that a second run, a disk that is not a 68k_HAL disk and too little space are
# refused. Never touches a device.
#
# Usage: tools/data_partition_check.py <work dir> <stand-in system image> <eHFS tool dir>

import hashlib
import os
import shutil
import subprocess
import sys

MIB = 1024 * 1024
DATA_TYPE = "68AC0002-4441-5441-8D1E-68A1D5C0DE01"


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True)


def partitions(image):
    """name -> (first sector, last sector) from sgdisk -p."""
    result = {}
    for line in run(["sgdisk", "-p", image]).stdout.splitlines():
        f = line.split()
        if f and f[0].isdigit() and len(f) >= 7:
            result[" ".join(f[6:])] = (int(f[1]), int(f[2]))
    return result


def digest(image, skip):
    """SHA-256 of the image with the byte ranges in skip left out."""
    h = hashlib.sha256()
    size = os.path.getsize(image)
    with open(image, "rb") as f:
        pos = 0
        for start, end in sorted(skip) + [(size, size)]:
            f.seek(pos)
            h.update(f.read(max(0, start - pos)))
            pos = max(pos, end)
    return h.hexdigest()


def main():
    work, system, tools = sys.argv[1:4]
    here = os.path.dirname(os.path.abspath(__file__))
    script = os.path.join(here, "add_data_partition.sh")
    failures = []

    def check(cond, what):
        print(f"data_partition_check: {'ok' if cond else 'FAIL'}: {what}")
        if not cond:
            failures.append(what)

    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    image = os.path.join(work, "disk.img")
    r = run([sys.executable, "-I", os.path.join(here, "make_mac_disk.py"), "--out", image, "--system", system,
             "--boot-mib", "34", "--system-mib", "8", "--overlay-mib", "8", "--apps-mib", "8"])
    if r.returncode != 0:
        sys.exit("data_partition_check: make_mac_disk.py failed: " + r.stderr)
    # Free space for the data partition after the others (the backup table moves to the new end).
    with open(image, "r+b") as f:
        f.truncate(os.path.getsize(image) + 100 * MIB)
    run(["sgdisk", "-e", image])
    with open(image, "r+b") as f:     # something in the free space, which must stay where it is not used
        f.seek(os.path.getsize(image) - 3 * MIB)
        f.write(b"\xA5" * MIB)

    size = os.path.getsize(image)
    tables = [(0, 34 * 512), (size - 33 * 512, size)]
    before = open(image, "rb").read()

    r = run(["sh", script, "--dry-run", "--tools", tools, image])
    check(r.returncode == 0 and "dry run" in r.stdout, "a dry run says what it would do")
    check(open(image, "rb").read() == before, "a dry run writes nothing")

    r = run(["sh", script, "--tools", tools, "--size", "64", "--name", "My Data", image])
    check(r.returncode == 0, "the data partition is added: " + r.stderr.strip())
    parts = partitions(image)
    data = parts.get("68k_HAL Data")
    check(data is not None and data[1] - data[0] + 1 == 64 * 2048, "named 68k_HAL Data, 64 MiB (--size)")
    info = run(["sgdisk", "-i", str(len(parts)), image]).stdout
    check(DATA_TYPE in info, "with the data partition's type GUID")
    if data is not None:
        fsck = run([os.path.join(tools, "fsck.ehfs"), "--offset", str(data[0]), image])
        check(fsck.returncode == 0 and '"My Data"' in fsck.stdout, "holding a clean eHFS volume called My Data")
        r = run([os.path.join(tools, "ehfs-cp"), "--offset", str(data[0]), image, "mkdir", "/Works"])
        check(r.returncode == 0, "that ehfs-cp can write")
        skip = tables + [(data[0] * 512, (data[1] + 1) * 512)]
        check(digest(image, skip) == digest_bytes(before, skip), "every other byte is as it was")

    r = run(["sh", script, "--tools", tools, image])
    check(r.returncode != 0 and "already has" in r.stderr, "a second data partition is refused")

    other = os.path.join(work, "other.img")
    with open(other, "wb") as f:
        f.truncate(200 * MIB)
    run(["sgdisk", "-o", "-n", "1:2048:+50M", "-c", "1:Some data", other])
    before_other = open(other, "rb").read()
    r = run(["sh", script, "--tools", tools, other])
    check(r.returncode != 0 and "not a 68k_HAL disk" in r.stderr, "a disk that is not a 68k_HAL disk is refused")
    check(open(other, "rb").read() == before_other, "and not written")

    small = os.path.join(work, "small.img")
    run([sys.executable, "-I", os.path.join(here, "make_mac_disk.py"), "--out", small, "--system", system,
         "--boot-mib", "34", "--system-mib", "8", "--overlay-mib", "8", "--apps-mib", "8"])
    r = run(["sh", script, "--tools", tools, small])
    check(r.returncode != 0 and ("free" in r.stderr), "too little free space is refused: " + r.stderr.strip())
    r = run(["sh", script, "--tools", tools, "/dev/null"])
    check(r.returncode != 0, "a target that is neither a disk nor an image is refused")

    if failures:
        sys.exit(f"data_partition_check: {len(failures)} failed")
    print("data_partition_check: PASS")


def digest_bytes(data, skip):
    h = hashlib.sha256()
    pos = 0
    for start, end in sorted(skip) + [(len(data), len(data))]:
        h.update(data[pos:max(pos, start)])
        pos = max(pos, end)
    return h.hexdigest()


main()
