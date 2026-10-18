#!/usr/bin/env python3
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# make test-vbe: switching the screen mode through the video BIOS (VBE, run in x86emu:
# platform/pc/video/vbe.c). QEMU with SeaBIOS and its VGA BIOS boots the kernel with "nobga" on the
# command line, so that the kernel cannot use the Bochs/QEMU display registers and must ask the
# video BIOS. The shell lists the modes, reads the EDID and switches to two modes; after each switch the
# screen (QEMU's screendump) must have the new size and the test pattern's colour bars.
#
# Usage: tools/vbe_check.py <kernel.elf>       Needs qemu.

import os
import re
import socket
import subprocess
import sys
import tempfile
import time

KEYS = {" ": "spc", "\n": "ret", "-": "minus", "=": "equal", ".": "dot", "/": "slash"}
BARS = [(255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0),
        (255, 0, 255), (255, 0, 0), (0, 0, 255), (0, 0, 0)]


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    match = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", data)
    return int(match.group(1)), int(match.group(2)), data[match.end():]


def main():
    kernel = sys.argv[1]
    accel = ["-enable-kvm"] if os.access("/dev/kvm", os.W_OK) else []
    with tempfile.TemporaryDirectory() as tmp:
        monitor = os.path.join(tmp, "monitor.sock")
        serial = os.path.join(tmp, "serial.log")
        process = subprocess.Popen(
            ["qemu-system-x86_64", *accel, "-m", "512", "-display", "none", "-vga", "none",
             "-device", "VGA,vgamem_mb=32", "-kernel", kernel, "-append", "nobga",
             "-serial", f"file:{serial}", "-monitor", f"unix:{monitor},server,nowait"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        failures = []
        try:
            deadline = time.time() + 60
            while time.time() < deadline:
                time.sleep(0.5)
                if os.path.exists(serial) and "Shell ready" in open(serial, errors="replace").read():
                    break
            else:
                sys.exit("vbe_check: FAIL: no shell")
            sock = socket.socket(socket.AF_UNIX)
            sock.connect(monitor)

            def type_line(text):
                for ch in text + "\n":
                    sock.sendall(f"sendkey {KEYS.get(ch, ch)}\n".encode())
                    time.sleep(0.05)
                time.sleep(3)

            for command in ("video list", "video edid"):
                type_line(command)
            for width, height in ((800, 600), (1920, 1080)):
                type_line(f"video {width} {height}")
                shot = os.path.join(tmp, f"{width}.ppm")
                sock.sendall(f"screendump {shot}\n".encode())
                time.sleep(1.5)
                w, h, body = read_ppm(shot)
                if (w, h) != (width, height):
                    failures.append(f"video {width} {height}: the screen is {w}x{h}")
                    continue
                for i, color in enumerate(BARS):
                    x, y = i * w // 8 + w // 16, h // 4
                    k = (y * w + x) * 3
                    if tuple(body[k:k + 3]) != color:
                        failures.append(f"{width}x{height}: bar {i} is {tuple(body[k:k + 3])}, want {color}")
                        break
                else:
                    print(f"vbe_check: ok: {width}x{height} through the video BIOS")
            sock.close()
        finally:
            process.kill()
            process.wait()
        log = open(serial, errors="replace").read()

    for want in ("VBE modes with 32 bits per pixel", "1920x1080", "video: native resolution",
                 "video: 800x600 32 bpp", "video: 1920x1080 32 bpp"):
        if want not in log:
            failures.append(f"the shell did not print '{want}'")
    if failures:
        print(log[-2000:])
        sys.exit("vbe_check: FAIL: " + "; ".join(failures))
    print("vbe_check: " + re.search(r"video: native resolution \S+", log).group(0))
    print("vbe_check: PASS")


if __name__ == "__main__":
    main()
