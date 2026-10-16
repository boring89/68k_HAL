#!/usr/bin/env python3
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Boot the "graphics" GRUB entry in QEMU and check that the framebuffer shows
# the colour bars (kernel/display/fb_pattern.c) above the text console
# (kernel/display/fbcon.c, which shows the shell prompt).
#
# Usage: tools/fb_check.py [kernel.elf]   (default: build/kernel-gfx.elf)
#
# Needs grub-mkrescue and python3. Not part of `make test`. It builds a
# temporary ISO whose menu starts the second entry of grub.cfg, boots it with
# a standard VGA card, saves a QEMU screendump (PPM) and checks the colour
# bars and that the console drew something. Exits 0 on success.

import os
import re
import subprocess
import sys
import tempfile
import time

TIMEOUT = 20.0  # seconds to wait for the kernel to draw
BAR_COLORS = [(255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0),
              (255, 0, 255), (255, 0, 0), (0, 0, 255), (0, 0, 0)]


def read_ppm(path):
    """Return (width, height, pixel(x, y) -> (r, g, b)) of a binary PPM."""
    with open(path, "rb") as f:
        data = f.read()
    match = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", data)
    if not match:
        raise RuntimeError("not a P6 PPM file")
    width, height = int(match.group(1)), int(match.group(2))
    body = data[match.end():]

    def pixel(x, y):
        i = (y * width + x) * 3
        return tuple(body[i:i + 3])

    return width, height, pixel


def main():
    kernel = sys.argv[1] if len(sys.argv) > 1 else "build/kernel-gfx.elf"
    if not os.path.exists(kernel):
        sys.exit(f"{kernel} not found; run make first")

    with tempfile.TemporaryDirectory() as tmp:
        root = os.path.join(tmp, "iso")
        os.makedirs(os.path.join(root, "boot", "grub"))
        subprocess.run(["cp", kernel, os.path.join(root, "boot", "kernel-gfx.elf")],
                       check=True)
        with open("grub.cfg") as f:
            cfg = f.read().replace("set default=0", "set default=1", 1)
        with open(os.path.join(root, "boot", "grub", "grub.cfg"), "w") as f:
            f.write(cfg)
        iso = os.path.join(tmp, "test.iso")
        subprocess.run(["grub-mkrescue", "-o", iso, root], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        serial_path = os.path.join(tmp, "serial.log")
        monitor = os.path.join(tmp, "monitor.sock")
        shot = os.path.join(tmp, "screen.ppm")
        shot2 = os.path.join(tmp, "screen2.ppm")
        process = subprocess.Popen(
            ["qemu-system-i386", "-cdrom", iso, "-vga", "std",
             "-display", "none", "-no-reboot",
             "-serial", f"file:{serial_path}",
             "-monitor", f"unix:{monitor},server,nowait"])
        try:
            deadline = time.monotonic() + TIMEOUT
            while time.monotonic() < deadline:
                if os.path.exists(serial_path):
                    with open(serial_path, errors="replace") as f:
                        if "Framebuffer: " in f.read():
                            break
                time.sleep(0.3)
            else:
                sys.exit("FAIL: kernel did not report a framebuffer")
            time.sleep(1.0)  # let it finish drawing

            import socket
            with socket.socket(socket.AF_UNIX) as s:
                s.connect(monitor)
                s.settimeout(5)
                s.recv(4096)
                s.sendall(f"screendump {shot}\n".encode())
                time.sleep(1.0)
                # Type a command: its output must show up on the screen too.
                for key in ["c", "p", "u", "i", "n", "f", "o", "ret"]:
                    s.sendall(f"sendkey {key}\n".encode())
                    time.sleep(0.1)
                time.sleep(1.5)
                s.sendall(f"screendump {shot2}\n".encode())
                time.sleep(1.0)
        finally:
            process.kill()
            process.wait()

        with open(serial_path, errors="replace") as f:
            log = f.read()
        print(log.strip().splitlines()[-3:])
        if os.environ.get("FB_CHECK_SAVE"):
            subprocess.run(["cp", shot, os.environ["FB_CHECK_SAVE"]], check=True)
        width, height, pixel = read_ppm(shot)
        print(f"screen {width}x{height}")
        bar = width // 8
        for i, color in enumerate(BAR_COLORS):
            got = pixel(i * bar + bar // 2, 4)
            if got != color:
                sys.exit(f"FAIL: bar {i} is {got}, expected {color}")
        # The console: the shell prompt is the only text on the screen, below the bars.
        lit = sum(1 for y in range(24, 48) for x in range(0, 64) if pixel(x, y) != (0, 0, 0))
        if lit < 20:
            sys.exit(f"FAIL: no text below the bars ({lit} lit pixels)")

        # After typing "cpuinfo" the console shows several lines of text under the prompt.
        width, height, pixel = read_ppm(shot2)
        if os.environ.get("FB_CHECK_SAVE"):
            subprocess.run(["cp", shot2, os.environ["FB_CHECK_SAVE"]], check=True)
        lit = sum(1 for y in range(64, 200) for x in range(0, 320) if pixel(x, y) != (0, 0, 0))
        if lit < 300:
            sys.exit(f"FAIL: the command's output is not on the screen ({lit} lit pixels)")
        print("fb_check: PASS")


if __name__ == "__main__":
    main()
