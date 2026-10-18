#!/usr/bin/env python3
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# make test-mac-hotkeys: the enlargement hot keys of the Mac (kernel/mac/machine.c) end to end. The kernel
# of make test-mac-perf (2560x1440 screen, the Mac at 1280x720 enlarged 2x) runs under QEMU; while the Mac
# runs, QEMU types Ctrl+Alt+Page Down (smaller: 1.5x), Ctrl+Alt+Page Up (2x again), Ctrl+Alt+Page Down
# twice (1x) and Ctrl+Alt+Home (back to the largest, 2x). The kernel reports every new layout on the serial
# port ("mac: screen 1: ... enlarged 1.5x").
#
# Usage: tools/mac_hotkey_check.py <kernel.elf> <rom> <disk>       Local only (the user's ROM and disk).

import os
import socket
import subprocess
import sys
import tempfile
import time

STEPS = [("ctrl-alt-pgdn", "1.5x"), ("ctrl-alt-pgup", "2.0x"), ("ctrl-alt-pgdn", "1.5x"),
         ("ctrl-alt-pgdn", "1.0x"), ("ctrl-alt-home", "2.0x")]


def main():
    kernel, rom, disk = sys.argv[1:4]
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

        def log():
            return open(serial, errors="replace").read() if os.path.exists(serial) else ""

        def layouts():
            return [line.rsplit(" ", 1)[-1] for line in log().splitlines() if line.startswith("mac: screen 1:")]

        failures = []
        try:
            deadline = time.time() + 60
            while time.time() < deadline and "mac: running" not in log():
                time.sleep(0.5)
            time.sleep(3)
            sock = socket.socket(socket.AF_UNIX)
            sock.connect(monitor)
            for key, want in STEPS:
                before = len(layouts())
                sock.sendall(f"sendkey {key}\n".encode())
                deadline = time.time() + 10
                while time.time() < deadline and len(layouts()) == before:
                    time.sleep(0.2)
                got = layouts()[-1] if len(layouts()) > before else "(no new layout)"
                print(f"mac_hotkey_check: {key}: {got}")
                if got != want:
                    failures.append(f"{key}: {got}, want {want}")
            sock.close()
        finally:
            process.kill()
            process.wait()
    if failures:
        sys.exit("mac_hotkey_check: FAIL: " + "; ".join(failures))
    print("mac_hotkey_check: PASS")


if __name__ == "__main__":
    main()
