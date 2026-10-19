#!/usr/bin/env python3
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# make test-ehfs-tools: the Linux tools of eHFS (docs/ehfs.md) on an image file: mkfs.ehfs, ehfs-cp in
# (a folder tree with a UTF-8 name, a 3 MB file, a resource fork, Finder information and another extended
# attribute), ls, fsck.ehfs, ehfs-cp out (the same bytes and attributes back), rm, mkdir, a partition
# offset, a damaged image found by fsck, and a target that is not a regular file refused. The extended
# attribute part is skipped where the file system has no user xattrs.
#
# Usage: tools/ehfs_tools_check.py <tool dir> <work dir>

import filecmp
import os
import shutil
import subprocess
import sys

MIB = 1024 * 1024


def main():
    tools, work = sys.argv[1:3]
    mkfs, fsck, cp = (os.path.join(tools, t) for t in ("mkfs.ehfs", "fsck.ehfs", "ehfs-cp"))
    failures = []

    def check(cond, what):
        print(f"ehfs_tools_check: {'ok' if cond else 'FAIL'}: {what}")
        if not cond:
            failures.append(what)

    def run(*args):
        return subprocess.run(list(args), capture_output=True, text=True)

    shutil.rmtree(work, ignore_errors=True)
    src = os.path.join(work, "src")
    os.makedirs(os.path.join(src, "Docs", "Deep"))
    with open(os.path.join(src, "readme.txt"), "w") as f:
        f.write("hello eHFS\n")
    data = bytes((i * 7 + i // 4093) & 255 for i in range(3 * MIB))
    with open(os.path.join(src, "Docs", "big.bin"), "wb") as f:
        f.write(data)
    with open(os.path.join(src, "Docs", "Deep", "café.txt"), "w") as f:
        f.write("Café")
    readme = os.path.join(src, "readme.txt")
    try:
        os.setxattr(readme, "user.com.apple.ResourceFork", bytes(range(256)) * 3)
        os.setxattr(readme, "user.com.apple.FinderInfo", b"TEXTttxt" + bytes(24))
        os.setxattr(readme, "user.comment", b"a note")
        xattrs = True
    except OSError:
        xattrs = False
        print("ehfs_tools_check: no user xattrs here: that part is skipped")

    image = os.path.join(work, "data.img")
    with open(image, "wb") as f:
        f.truncate(64 * MIB)
    r = run(mkfs, "-L", "My Data", image)
    check(r.returncode == 0 and "16384 blocks" in r.stdout, "mkfs.ehfs makes a 64 MiB volume")
    r = run(cp, image, "in", src, "/")
    check(r.returncode == 0, "ehfs-cp copies a folder tree in: " + r.stderr.strip())
    r = run(cp, image, "ls", "/Docs")
    check(r.returncode == 0 and "3145728" in r.stdout and "Deep" in r.stdout, "ehfs-cp ls lists a folder")
    if xattrs:
        r = run(cp, image, "ls", "/")
        check("TEXT/ttxt" in r.stdout and "768" in r.stdout, "the Finder information and resource fork are in")
    r = run(fsck, image)
    check(r.returncode == 0 and "3 files, 2 folders" in r.stdout, "fsck.ehfs finds it clean: " + r.stdout.strip())

    out = os.path.join(work, "out")
    r = run(cp, image, "out", "/", out)
    check(r.returncode == 0, "ehfs-cp copies everything out")
    cmp = filecmp.dircmp(src, out)
    same = not cmp.diff_files and not cmp.left_only and not cmp.right_only
    for sub in ("Docs", os.path.join("Docs", "Deep")):
        c = filecmp.dircmp(os.path.join(src, sub), os.path.join(out, sub))
        same = same and not c.left_only and not c.right_only
    check(same and open(os.path.join(out, "Docs", "big.bin"), "rb").read() == data, "the same files come back")
    if xattrs:
        back = os.path.join(out, "readme.txt")
        check(all(os.getxattr(readme, n) == os.getxattr(back, n) for n in os.listxattr(readme)),
              "the same extended attributes come back")

    r = run(cp, image, "rm", "/Docs/big.bin")
    check(r.returncode == 0, "ehfs-cp rm removes a file")
    r = run(cp, image, "rm", "/Docs")
    check(r.returncode != 0 and "not empty" in r.stderr, "a folder with things in it is not removed")
    r = run(cp, image, "mkdir", "/New Folder")
    check(r.returncode == 0, "ehfs-cp mkdir makes a folder")
    r = run(fsck, image)
    check(r.returncode == 0 and "2 files, 3 folders" in r.stdout, "still clean after the changes")

    # A volume inside a disk image, at a partition's offset.
    disk = os.path.join(work, "disk.img")
    with open(disk, "wb") as f:
        f.truncate(40 * MIB)
    r = run(mkfs, "--offset", "2048", "--sectors", str(32 * 2048), disk)
    check(r.returncode == 0, "mkfs.ehfs at an offset")
    r = run(cp, "--offset", "2048", disk, "in", readme, "/readme.txt")
    check(r.returncode == 0, "ehfs-cp at an offset")
    check(run(fsck, "--offset", "2048", disk).returncode == 0, "fsck.ehfs at an offset")
    with open(disk, "rb") as f:
        check(f.read(2048 * 512) == bytes(2048 * 512), "nothing written before the offset")

    # Damage: a byte of the catalog's first node after the header node changed.
    with open(image, "r+b") as f:
        # The catalog starts after the superblock, the bitmap (1 block) and the journal (260 blocks).
        f.seek((1 + 1 + 260) * 4096 + 4 * 4096 + 100)
        b = f.read(1)
        f.seek(-1, 1)
        f.write(bytes([b[0] ^ 0x80]))
    r = run(fsck, image)
    check(r.returncode == 1, "fsck.ehfs finds a damaged catalog node: " + r.stdout.strip().splitlines()[-1])

    check(run(mkfs, "/dev/null").returncode != 0, "mkfs.ehfs refuses what is not a regular file")

    if failures:
        sys.exit(f"ehfs_tools_check: {len(failures)} failed")
    print("ehfs_tools_check: PASS")


main()
