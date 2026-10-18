#!/usr/bin/env python3
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# make test-mac-remember: "mac" starts in the mode the last interactive run ended in (kernel/mac/machine.c).
# The normal kernel runs under QEMU with the ROM and disk as modules; QEMU types: video 1024 768 (a screen),
# mac res=800x600 depth=8, ` (stop), mac (must start at 800x600 again), `, mac res=auto (the one that fits
# 1024x768: 1024x768 itself), `.
#
# Usage: tools/mac_remember_check.py <kernel.elf> <rom> <disk>       Local only (the user's ROM and disk).

import os
import socket
import subprocess
import sys
import tempfile
import time

KEYS = {" ": "spc", "\n": "ret", "=": "equal", "`": "grave_accent", "x": "x"}


def main():
    kernel, rom, disk = sys.argv[1:4]
    accel = ["-enable-kvm"] if os.access("/dev/kvm", os.W_OK) else []
    with tempfile.TemporaryDirectory() as tmp:
        monitor = os.path.join(tmp, "monitor.sock")
        serial = os.path.join(tmp, "serial.log")
        process = subprocess.Popen(
            ["qemu-system-x86_64", *accel, "-m", "512", "-display", "none",
             "-serial", f"file:{serial}", "-monitor", f"unix:{monitor},server,nowait",
             "-kernel", kernel, "-initrd", f"{rom} mac_rom,{disk} mac_disk"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        def log():
            return open(serial, errors="replace").read() if os.path.exists(serial) else ""

        def screens():
            return [line for line in log().splitlines() if line.startswith("mac: screen 1:")]

        results = []
        try:
            deadline = time.time() + 60
            while time.time() < deadline and "Shell ready" not in log():
                time.sleep(0.5)
            sock = socket.socket(socket.AF_UNIX)
            sock.connect(monitor)

            def type_text(text):
                for ch in text:
                    sock.sendall(f"sendkey {KEYS.get(ch, ch)}\n".encode())
                    time.sleep(0.05)

            type_text("video 1024 768\n")
            time.sleep(2)
            for command in ("mac res=800x600 depth=8\n", "mac\n", "mac res=auto\n"):
                before = len(screens())
                type_text(command)
                deadline = time.time() + 20
                while time.time() < deadline and len(screens()) == before:
                    time.sleep(0.2)
                results.append(screens()[-1] if len(screens()) > before else "(no screen)")
                time.sleep(3)
                type_text("`")
                time.sleep(3)
            sock.close()
        finally:
            process.kill()
            process.wait()

    want = ["800x600", "800x600", "1024x768"]
    failures = []
    for got, size in zip(results, want):
        print(f"mac_remember_check: {got}")
        if f": {size} at" not in got:
            failures.append(f"want {size}, got '{got}'")
    if failures:
        sys.exit("mac_remember_check: FAIL: " + "; ".join(failures))
    print("mac_remember_check: PASS")


if __name__ == "__main__":
    main()
