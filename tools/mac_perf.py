#!/usr/bin/env python3
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# make test-mac-perf: run the Mac in the kernel under QEMU (KVM when available) on a large screen and
# report what it cost. The kernel (built by the Makefile with -DKERNEL_MAC_TEST...) sets the screen mode,
# runs a fixed number of frames flat out (no real-time pacing: frames per second = what the machine can do)
# and prints its performance figures (kernel/mac/machine.c, report_performance) and the screen's CRC. A
# screenshot is taken at the end.
#
# Usage: tools/mac_perf.py <kernel.elf> <rom> <disk> <screenshot.ppm>
# Local only: needs the user's Mac ROM and disk image.

import os
import socket
import subprocess
import sys
import tempfile
import time


def main():
    kernel, rom, disk, shot = sys.argv[1:5]
    accel = ["-enable-kvm"] if os.access("/dev/kvm", os.W_OK) else []
    with tempfile.TemporaryDirectory() as tmp:
        monitor = os.path.join(tmp, "monitor.sock")
        serial = os.path.join(tmp, "serial.log")
        process = subprocess.Popen(
            ["qemu-system-x86_64", *accel, "-m", "512", "-display", "none",
             "-device", "VGA,vgamem_mb=64", "-vga", "none",
             "-serial", f"file:{serial}", "-monitor", f"unix:{monitor},server,nowait",
             "-kernel", kernel, "-initrd", f"{rom} mac_rom,{disk} mac_disk"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            deadline = time.time() + 900
            text = ""
            while time.time() < deadline:
                time.sleep(1)
                with open(serial, errors="replace") as f:
                    text = f.read()
                if "mac: screen crc" in text or "mac: could not run" in text:
                    break
            time.sleep(1)
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.connect(monitor)
            sock.sendall(f"screendump {shot}\n".encode())
            time.sleep(2)
            sock.close()
        finally:
            process.kill()
            process.wait()
    lines = [line for line in text.splitlines() if line.startswith("mac:")]
    print("\n".join(lines))
    if not any("screen crc" in line for line in lines):
        print("test-mac-perf: FAIL: the Mac did not finish")
        return 1
    print(f"test-mac-perf: screenshot {shot}")
    return 0


sys.exit(main())
