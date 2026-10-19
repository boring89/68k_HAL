#!/usr/bin/env python3
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# make test-mac-fsm (local: the user's ROM and System): the external file system (emu/mac/fsm.c,
# docs/fsm.md) in the host runner. A host folder is shown to the Mac as the read-only volume "HAL"
# (mac_run -A); the Finder mounts it, a double-click opens it, a file is dragged onto the system disk
# (the copy is read back from the saved disk with hfsutils) and another file is opened in SimpleText
# (it first asks to write, gets permErr, opens it again for reading). Then an eHFS image (mac_run -e) is
# the volume "Data", which the Mac changes: a new folder (Command-N) renamed by typing, a file duplicated
# (Command-D) and dragged into the folder, the folder dragged to the Trash and the Trash emptied, a text
# file edited and saved in SimpleText (data and resource fork); after each run the Linux tools check the
# image (fsck.ehfs clean, ehfs-cp shows the changes). The mouse positions are those of the 640x480 screen.
#
# Usage: tools/mac_fsm_check.py <mac_run_fast> <rom> <system disk> <work dir> <eHFS tool dir>

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


def drag(x0, y0, x1, y1, frame, steps=20):
    args = ["-J", f"{x0},{y0},1,{frame}"]
    for i in range(1, steps + 1):
        args += ["-J", f"{x0 + (x1 - x0) * i // steps},{y0 + (y1 - y0) * i // steps},1,{frame + 4 + 2 * i}"]
    return args + ["-J", f"{x1},{y1},0,{frame + 3 * steps}"]


# ADB key codes (Inside Macintosh: Macintosh Toolbox Essentials, the standard keyboard's virtual key codes)
ADB = {"d": 0x02, "e": 0x0E, "n": 0x2D, "o": 0x1F, "q": 0x0C, "s": 0x01, "t": 0x11, "h": 0x04, "i": 0x22,
       " ": 0x31, "\n": 0x24}
COMMAND, SHIFT = 0x37, 0x38


def typed(text, frame):
    args = []
    for ch in text:
        code = ADB[ch.lower()]
        if ch.isupper():
            args += ["-K", f"{SHIFT:#x},1,{frame}"]
        args += ["-K", f"{code:#x},1,{frame + 1}", "-K", f"{code:#x},0,{frame + 3}"]
        if ch.isupper():
            args += ["-K", f"{SHIFT:#x},0,{frame + 4}"]
        frame += 6
    return args


def command(key, frame):
    code = ADB[key]
    return ["-K", f"{COMMAND:#x},1,{frame}", "-K", f"{code:#x},1,{frame + 3}", "-K", f"{code:#x},0,{frame + 6}",
            "-K", f"{COMMAND:#x},0,{frame + 9}"]


def main():
    runner, rom, disk, work, tools = sys.argv[1:6]
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

    # The eHFS volume "Data" (the only volume of ours: its icon is where HAL's was).
    mkfs, fsck, cp = (os.path.join(tools, t) for t in ("mkfs.ehfs", "fsck.ehfs", "ehfs-cp"))
    image = os.path.join(work, "data.img")
    text = b"Hello from eHFS.\r"
    host_text = os.path.join(work, "notes.txt")
    with open(host_text, "wb") as f:
        f.write(text)
    try:
        os.setxattr(host_text, "user.com.apple.FinderInfo", b"TEXTttxt" + bytes(24))
    except OSError:
        pass

    def fresh_image():
        with open(image, "wb") as f:
            f.truncate(32 * 1024 * 1024)
        subprocess.run([mkfs, "-L", "Data", image], check=True, capture_output=True)
        subprocess.run([cp, image, "in", host_text, "/readme.txt"], check=True, capture_output=True)

    def mac(frames, events):
        return subprocess.run([runner, "-F", str(frames), "-t", "0", "-r", "-e", image] + open_volume + events +
                              ["-d", disk, rom], capture_output=True, text=True).stdout

    def listing(path):
        return subprocess.run([cp, image, "ls", path], capture_output=True, text=True).stdout

    def clean():
        return subprocess.run([fsck, image], capture_output=True).returncode == 0

    fresh_image()
    out = mac(3800, command("n", 2600) + typed("Notes\n", 2700) + mouse_click(ICON_X, ICON_Y, 2900) +
              command("d", 3000) + drag(55, 100, 102, 95, 3300))
    check('volume "Data": install result 0, mounted' in out, "the Finder mounts the eHFS volume")
    root, notes = listing("/"), listing("/Notes")
    check(" Notes\n" in root and "FNDR/ERIK Desktop" in root, "a folder made and renamed, and the Finder's Desktop file")
    check("readme.txt copy" in notes and "readme.txt copy" not in root, "a duplicate moved into the folder")
    got = os.path.join(work, "copy.txt")
    subprocess.run([cp, image, "out", "/Notes/readme.txt copy", got], capture_output=True)
    check(os.path.exists(got) and open(got, "rb").read() == text, "the duplicate holds the same bytes")
    check(clean(), "fsck.ehfs: clean after the changes")

    fresh_image()
    out = mac(4300, command("n", 2600) + typed("Notes\n", 2700) + drag(102, 95, 593, 440, 3000) +
              drag(231, 10, 250, 48, 3300, steps=10) + ["-K", "0x24,1,3500", "-K", "0x24,0,3503"])
    root = listing("/")
    check(" Notes\n" not in root and " Trash\n" in root, "a folder dragged to the Trash and the Trash emptied")
    check(clean(), "fsck.ehfs: clean after the removal")

    fresh_image()
    out = mac(3700, mouse_click(ICON_X, ICON_Y, 2400, double=True) + typed("Hi ", 3000) + command("s", 3100) +
              command("q", 3300))
    subprocess.run([cp, image, "out", "/readme.txt", got], capture_output=True)
    root = listing("/")
    check(open(got, "rb").read() == b"Hi " + text, "SimpleText saved the edited text")
    check("TEXT/ttxt readme.txt" in root and "         0 TEXT" not in root, "with a resource fork")
    check(clean(), "fsck.ehfs: clean after the save")

    if failures:
        sys.exit(f"mac_fsm_check: {len(failures)} failed")
    print("mac_fsm_check: PASS")


main()
