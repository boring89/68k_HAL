#!/usr/bin/env python3
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Boot every menu entry of the USB image (make usb-image) in QEMU, once with BIOS firmware and once
# with UEFI (OVMF): pick the entry with the keyboard (down, enter) in GRUB's menu and look at
# the serial output for what that entry must do.
#
# Usage: tools/usb_image_test.py [image]   (default build/68k_HAL_usb.img)
# Needs qemu and the ovmf package. Not part of `make test`; `make test-usb-image` runs it.

import os
import socket
import subprocess
import sys
import tempfile
import time

OVMF_CODE = "/usr/share/OVMF/OVMF_CODE_4M.fd"
OVMF_VARS = "/usr/share/OVMF/OVMF_VARS_4M.fd"

# (menu index, name, texts the serial output must show)
ENTRIES = [
    (0, "normal", ["Shell ready", "Interrupts: I/O APIC", "CPUs online: 2"]),  # UEFI: also 2560x1440, see below
    (1, "safe mode", ["Shell ready", "using the 8259", "CPUs online: 1"]),
    (2, "diagnostics", ["diagnostics 1/9", "Vendor"]),
    (3, "mac", ["Shell ready"]),
    (4, "diagnostics without USB", ["diagnostics 1/9", "driver disabled by noxhci"]),
    (5, "graphics (BIOS only)", ["Shell ready", "Framebuffer: 640x480"]),
]


# UEFI only: the entry's Multiboot2 header asks for 2560x1440; then an entry for 1920x1080 and the one that
# keeps the firmware's mode.
UEFI_EXTRA = {0: "Framebuffer: 2560x1440", 3: "Framebuffer: 2560x1440"}
UEFI_ONLY = [(5, "1920x1080", ["Shell ready", "Framebuffer: 1920x1080"]),
             (6, "firmware resolution", ["Shell ready", "Framebuffer: 1280x800"])]


def run_entry(image, uefi, index, name, expect):
    with tempfile.TemporaryDirectory() as tmp:
        serial = os.path.join(tmp, "serial.log")
        monitor = os.path.join(tmp, "monitor.sock")
        if uefi:
            variables = os.path.join(tmp, "vars.fd")
            subprocess.run(["cp", OVMF_VARS, variables], check=True)
            cmd = ["qemu-system-x86_64", "-machine", "q35",
                   "-drive", f"if=pflash,format=raw,readonly=on,file={OVMF_CODE}",
                   "-drive", f"if=pflash,format=raw,file={variables}"]
        else:
            cmd = [os.environ.get("QEMU_SYSTEM", "qemu-system-i386")]
        cmd += ["-m", "512", "-smp", "2", "-device", "qemu-xhci", "-cdrom", image, "-display", "none", "-no-reboot",
                "-serial", f"file:{serial}", "-monitor", f"unix:{monitor},server,nowait"]
        process = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            for _ in range(100):
                if os.path.exists(monitor):
                    break
                time.sleep(0.1)
            time.sleep(8 if uefi else 4)  # firmware, then GRUB's menu

            with socket.socket(socket.AF_UNIX) as s:
                s.connect(monitor)
                s.settimeout(2)
                try:
                    s.recv(4096)
                except socket.timeout:
                    pass
                for key in ["down"] * index + ["ret"]:
                    s.sendall(f"sendkey {key}\n".encode())
                    time.sleep(0.3)

            deadline = time.monotonic() + 40
            log = ""
            while time.monotonic() < deadline:
                if os.path.exists(serial):
                    with open(serial, errors="replace") as f:
                        log = f.read()
                    if all(text in log for text in expect):
                        return True, log
                time.sleep(0.5)
            return False, log
        finally:
            process.kill()
            process.wait()


def main():
    image = sys.argv[1] if len(sys.argv) > 1 else "build/68k_HAL_usb.img"
    if not os.path.exists(image):
        sys.exit(f"{image} not found; run make usb-image")

    failed = 0
    for uefi in (False, True):
        for index, name, expect in ENTRIES + (UEFI_ONLY if uefi else []):
            if uefi and index == 5 and name.startswith("graphics"):
                continue
            if uefi and index in UEFI_EXTRA:
                expect = expect + [UEFI_EXTRA[index]]
            ok, log = run_entry(image, uefi, index, name, expect)
            label = f"{'UEFI' if uefi else 'BIOS'} / {name}"
            if ok:
                print(f"usb_image_test: ok: {label}")
            else:
                failed += 1
                print(f"usb_image_test: FAIL: {label}: expected {expect}")
                print(log[-500:])

    if failed:
        sys.exit(1)
    print("usb_image_test: PASS")


if __name__ == "__main__":
    main()
