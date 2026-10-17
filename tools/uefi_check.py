#!/usr/bin/env python3
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Boot the ISO with UEFI firmware (OVMF, Q35, standard VGA card) and look at the screen: the
# kernel must report the framebuffer GRUB gave it, draw the colour bars, and draw the console
# with the right text size for that resolution (kernel/display/fbcon.c: fbcon_scale_for).
#
# Usage: tools/uefi_check.py     Needs qemu, the ovmf package and grub-mkrescue.
# Not part of `make test` (it needs OVMF); `make test-uefi` runs it. Two runs: the ISO as it is
# (GRUB keeps the firmware's mode; OVMF's is 1280x800: text scale 1), and a temporary ISO whose
# Multiboot2 header asks for 1920x1080 (build/kernel-gfx1080.elf): text twice as big.

import os
import re
import socket
import subprocess
import sys
import tempfile
import time

# The build directory of the kernel under test (make exports BUILD for ARCH=x86_64 builds).
BUILD = os.environ.get("BUILD", "build")
OVMF_CODE = "/usr/share/OVMF/OVMF_CODE_4M.fd"
OVMF_VARS = "/usr/share/OVMF/OVMF_VARS_4M.fd"
BAR_COLORS = [(255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0),
              (255, 0, 255), (255, 0, 0), (0, 0, 255), (0, 0, 0)]
BARS_HEIGHT = 16


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    match = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", data)
    if not match:
        raise RuntimeError("not a P6 PPM file")
    width, height = int(match.group(1)), int(match.group(2))
    body = data[match.end():]
    return width, height, body


def expected_scale(width, height):
    # kernel/display/fbcon.c, fbcon_scale_for
    wanted = min(width // 960, height // 540)
    return max(1, min(4, wanted))


def make_1080_iso(tmp):
    root = os.path.join(tmp, "iso")
    os.makedirs(os.path.join(root, "boot", "grub"))
    for name in ("kernel-gfx1080.elf", "initrd.tar"):
        subprocess.run(["cp", os.path.join(BUILD, name) if name != "initrd.tar" else os.path.join("build", name),
                        os.path.join(root, "boot", name)], check=True)
    with open(os.path.join(root, "boot", "grub", "grub.cfg"), "w") as f:
        f.write("set default=0\nset timeout=0\ninsmod efi_gop\n"
                'menuentry "1080" {\n  multiboot2 /boot/kernel-gfx1080.elf\n'
                "  module2 /boot/initrd.tar initrd\n  boot\n}\n")
    iso = os.path.join(tmp, "1080.iso")
    subprocess.run(["grub-mkrescue", "-o", iso, root], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return iso


def check(iso, want_size=None):
    with tempfile.TemporaryDirectory() as tmp:
        serial = os.path.join(tmp, "serial.log")
        monitor = os.path.join(tmp, "monitor.sock")
        shot = os.path.join(tmp, "screen.ppm")
        variables = os.path.join(tmp, "vars.fd")
        subprocess.run(["cp", OVMF_VARS, variables], check=True)

        process = subprocess.Popen(
            ["qemu-system-x86_64", "-machine", "q35", "-m", "512",
             "-drive", f"if=pflash,format=raw,readonly=on,file={OVMF_CODE}",
             "-drive", f"if=pflash,format=raw,file={variables}",
             "-cdrom", iso, "-display", "none", "-no-reboot",
             "-serial", f"file:{serial}", "-monitor", f"unix:{monitor},server,nowait"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            deadline = time.monotonic() + 40
            log = ""
            while time.monotonic() < deadline:
                if os.path.exists(serial):
                    with open(serial, errors="replace") as f:
                        log = f.read()
                    if "Shell ready" in log:
                        break
                time.sleep(0.3)
            else:
                sys.exit("FAIL: the kernel never reached the shell\n" + log[-600:])
            time.sleep(1.5)

            with socket.socket(socket.AF_UNIX) as s:
                s.connect(monitor)
                s.settimeout(5)
                s.recv(4096)
                s.sendall(f"screendump {shot}\n".encode())
                time.sleep(1.0)
        finally:
            process.kill()
            process.wait()

        match = re.search(r"Framebuffer: (\d+)x(\d+)", log)
        if not match:
            sys.exit("FAIL: the kernel reported no framebuffer\n" + log[-600:])
        fb_width, fb_height = int(match.group(1)), int(match.group(2))

        if want_size is not None and (fb_width, fb_height) != want_size:
            sys.exit(f"FAIL: expected a {want_size[0]}x{want_size[1]} framebuffer")

        width, height, body = read_ppm(shot)
        print(f"uefi_check: framebuffer {fb_width}x{fb_height}, screen {width}x{height}")
        if (width, height) != (fb_width, fb_height):
            sys.exit("FAIL: the screen and the reported framebuffer differ")

        def pixel(x, y):
            i = (y * width + x) * 3
            return tuple(body[i:i + 3])

        bar = width // 8
        for i, color in enumerate(BAR_COLORS):
            got = pixel(i * bar + bar // 2, 4)
            if got != color:
                sys.exit(f"FAIL: bar {i} is {got}, expected {color}")

        # The console: the kernel says how many text cells it fits; that must follow the text
        # scale rule. And on the screen every lit horizontal run in the first text row (letters
        # drawn from the 8x8 font, each pixel scale x scale) is a whole number of scale pixels.
        scale = expected_scale(width, height)
        match = re.search(r"Console: (\d+) x (\d+) cells", log)
        if not match:
            sys.exit("FAIL: the kernel reported no console size")
        columns, rows_of_cells = int(match.group(1)), int(match.group(2))
        if columns != width // (8 * scale) or rows_of_cells != (height - BARS_HEIGHT) // (16 * scale):
            sys.exit(f"FAIL: {columns}x{rows_of_cells} cells on {width}x{height}, text scale {scale} would give "
                     f"{width // (8 * scale)}x{(height - BARS_HEIGHT) // (16 * scale)}")
        rows = [y for y in range(BARS_HEIGHT, BARS_HEIGHT + 16 * scale + 4)
                if any(pixel(x, y) != (0, 0, 0) for x in range(0, 40 * 8 * scale))]
        if not rows:
            sys.exit("FAIL: no text below the colour bars")
        for y in rows:
            run = 0
            for x in range(0, 40 * 8 * scale + 1):
                if x < 40 * 8 * scale and pixel(x, y) != (0, 0, 0):
                    run += 1
                else:
                    if run % scale:
                        sys.exit(f"FAIL: a lit run of {run} pixels is not a multiple of the text scale {scale}")
                    run = 0
        print(f"uefi_check: text scale {scale}, {columns}x{rows_of_cells} cells")
        print("uefi_check: ok")


def main():
    iso = os.path.join(BUILD, "68k_HAL.iso")
    if not os.path.exists(iso) or not os.path.exists(os.path.join(BUILD, "kernel-gfx1080.elf")):
        sys.exit("run make iso and make kernel-gfx1080.elf first")

    check(iso)

    with tempfile.TemporaryDirectory() as tmp:
        check(make_1080_iso(tmp), (1920, 1080))

    print("uefi_check: PASS")


if __name__ == "__main__":
    main()
