#!/usr/bin/env python3
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# make test-mac-fsm (local: the user's ROM and System): the external file system (emu/mac/fsm.c,
# docs/fsm.md) in the host runner. A host folder is shown to the Mac as the read-only volume "HAL"
# (mac_run -A); the Finder mounts it, a double-click opens it, a file is dragged onto the system disk
# (the copy is read back from the saved disk with hfsutils) and another file is opened in SimpleText
# (it first asks to write, gets permErr, opens it again for reading). The mouse positions are those of
# the 640x480 screen.
#
# Usage: tools/mac_fsm_check.py <mac_run_fast> <rom> <system disk> <work dir>

import os
import shutil
import subprocess
import sys


ICON_X, ICON_Y = 40, 95


def mouse_click(x, y, frame, double=False):
    args = ["-J", f"{x},{y},1,{frame}", "-J", f"{x},{y},0,{frame + 3}"]
    if double:
        args += ["-J", f"{x},{y},1,{frame + 6}", "-J", f"{x},{y},0,{frame + 9}"]
    return args


def drag(x0, y0, x1, y1, frame):
    args = ["-J", f"{x0},{y0},1,{frame}"]
    for i in range(1, 21):
        args += ["-J", f"{x0 + (x1 - x0) * i // 20},{y0 + (y1 - y0) * i // 20},1,{frame + 4 + 2 * i}"]
    return args + ["-J", f"{x1},{y1},0,{frame + 60}"]


def main():
    runner, rom, disk, work = sys.argv[1:5]
    failures = []

    def check(cond, what):
        print(f"mac_fsm_check: {'ok' if cond else 'FAIL'}: {what}")
        if not cond:
            failures.append(what)

    shutil.rmtree(work, ignore_errors=True)
    folder = os.path.join(work, "folder")
    os.makedirs(folder)
    text = b"Hello from the HAL host folder.\r"
    with open(os.path.join(folder, "readme.txt"), "wb") as f:
        f.write(text)
    with open(os.path.join(folder, ".hidden"), "wb") as f:
        f.write(b"not shown")

    # The HAL volume's icon is below the system disk's; readme.txt is the only icon of its window (one
    # visible item, so that its place does not depend on the host's order of names).
    open_volume = mouse_click(598, 95, 1900, double=True)
    saved = os.path.join(work, "copied.img")
    out = subprocess.run([runner, "-F", "3200", "-t", "0", "-r", "-A", folder] + open_volume +
                         drag(ICON_X, ICON_Y, 598, 45, 2400) + ["-O", saved, "-d", disk, rom],
                         capture_output=True, text=True).stdout
    check("install result 0, mounted" in out, "the Finder mounts the volume")
    env = dict(os.environ, HOME=work)
    subprocess.run(["hmount", saved], env=env, capture_output=True)
    listing = subprocess.run(["hls", "-l", ":"], env=env, capture_output=True, text=True).stdout
    got = os.path.join(work, "readme.txt")
    subprocess.run(["hcopy", "-r", ":readme.txt", got], env=env, capture_output=True)
    subprocess.run(["humount"], env=env, capture_output=True)
    check("TEXT/ttxt" in listing and "readme.txt" in listing, "readme.txt was copied onto the system disk as TEXT/ttxt")
    check(os.path.exists(got) and open(got, "rb").read() == text, "the copy holds the host file's bytes")

    out = subprocess.run([runner, "-F", "3000", "-t", "0", "-r", "-A", folder] + open_volume +
                         mouse_click(ICON_X, ICON_Y, 2400, double=True) + ["-d", disk, rom],
                         capture_output=True, text=True).stdout
    check("select A200 perm 3 -> -54" in out, "SimpleText asks to write and gets permErr")
    check("select A200 perm 1 -> 0" in out, "then opens the file for reading")
    check("select A402" in out or "select A002" in out, "and reads it")

    if failures:
        sys.exit(f"mac_fsm_check: {len(failures)} failed")
    print("mac_fsm_check: PASS")


main()
