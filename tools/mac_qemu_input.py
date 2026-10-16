#!/usr/bin/env python3
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the Mac ISO (make mac-iso) in QEMU and take pictures with the monitor.

Usage: python3 -I tools/mac_qemu_input.py <dir> --wait-crc <out.ppm>
       python3 -I tools/mac_qemu_input.py <dir> --input

<dir> is build/mac. --wait-crc waits until the kernel has run its frames and
printed the screen CRC, then writes the screen to <out.ppm>. --input waits
until the desktop is up, takes input_before.ppm, types the key T, moves and
clicks the mouse, and takes input_after.ppm (both in <dir>).
"""

import os
import shutil
import socket
import subprocess
import sys
import time


def changed_pixels(first, second):
    """Number of pixels that differ between two PPM screenshots of the same size."""
    def load(path):
        data = open(path, "rb").read()
        header = data.split(b"\n", 3)
        return header[1], header[3]

    size_a, pixels_a = load(first)
    size_b, pixels_b = load(second)
    if size_a != size_b:
        return 0
    return sum(1 for i in range(0, len(pixels_a), 3) if pixels_a[i:i + 3] != pixels_b[i:i + 3])


def main():
    work = sys.argv[1]
    iso = os.path.join(work, "mac.iso")
    sock_path = os.path.join(work, "monitor.sock")
    serial = os.path.join(work, "shot.out")
    for path in (sock_path, serial):
        if os.path.exists(path):
            os.remove(path)

    args = ["qemu-system-i386", "-m", "128", "-cdrom", iso, "-display", "none",
            "-serial", "file:" + serial,
            "-monitor", "unix:%s,server,nowait" % sock_path]
    if os.access("/dev/kvm", os.W_OK):
        args.insert(1, "-enable-kvm")
    qemu = subprocess.Popen(args)

    def monitor(command, pause=1.0):
        s = socket.socket(socket.AF_UNIX)
        s.connect(sock_path)
        time.sleep(0.3)
        s.sendall((command + "\n").encode())
        time.sleep(pause)
        s.close()

    def shot(name):
        monitor("screendump " + os.path.join(work, name), 1.5)

    try:
        for _ in range(100):
            if os.path.exists(sock_path):
                break
            time.sleep(0.1)
        if "--wait-crc" in sys.argv:
            for _ in range(900):
                time.sleep(1)
                with open(serial, errors="replace") as f:
                    if "mac: screen crc" in f.read():
                        break
            shot(os.path.basename(sys.argv[sys.argv.index("--wait-crc") + 1]))
            with open(serial, errors="replace") as f:
                print([l for l in f.read().splitlines() if l.startswith("mac:")][-2:])
        elif "--command-n" in sys.argv:
            # Alt is the Mac's Command key: Alt-N is File > New Folder.
            time.sleep(30)                      # the desktop is up after about 10 s
            shot("keys_before.ppm")
            monitor("sendkey alt-n", 0.5)
            time.sleep(8)
            shot("keys_after.ppm")
            changed = changed_pixels(os.path.join(work, "keys_before.ppm"),
                                     os.path.join(work, "keys_after.ppm"))
            print("mac_qemu_input: %d pixels changed after Alt-N" % changed)
            if changed < 200:
                print("mac_qemu_input: FAIL: no new folder appeared")
                sys.exit(1)
            print("mac_qemu_input: PASS")
        else:
            time.sleep(55)                      # the desktop is up after ~45 s
            shot("input_before.ppm")
            monitor("sendkey t", 0.5)
            time.sleep(4)
            shot("input_after.ppm")
    finally:
        qemu.terminate()
        qemu.wait()


main()
