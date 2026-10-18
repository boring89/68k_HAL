#!/usr/bin/env python3
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# make mac-uefi-shot: the Mac on a UEFI machine with a 2560x1440 screen. An ISO with GRUB (Multiboot2),
# the kernel (built with -DKERNEL_MAC_TEST..., its header asking for 2560x1440), the ROM and the disk as
# modules is booted with OVMF and QEMU's VGA (2560x1440); when the kernel reports the screen's CRC the
# screen is saved.
#
# Usage: tools/mac_uefi_shot.py <kernel.elf> <rom> <disk> <screenshot.ppm>
# Local only (the user's ROM and disk; copies stay in a temporary directory). Needs grub-mkrescue and OVMF.

import os
import socket
import subprocess
import sys
import tempfile
import time

OVMF_CODE = "/usr/share/OVMF/OVMF_CODE_4M.fd"
OVMF_VARS = "/usr/share/OVMF/OVMF_VARS_4M.fd"


def main():
    kernel, rom, disk, shot = sys.argv[1:5]
    accel = ["-enable-kvm"] if os.access("/dev/kvm", os.W_OK) else []
    with tempfile.TemporaryDirectory() as tmp:
        root = os.path.join(tmp, "iso")
        os.makedirs(os.path.join(root, "boot", "grub"))
        for source, name in ((kernel, "boot/kernel.elf"), (rom, "mac_rom"), (disk, "mac_disk")):
            subprocess.run(["cp", source, os.path.join(root, name)], check=True)
        with open(os.path.join(root, "boot", "grub", "grub.cfg"), "w") as f:
            f.write("set default=0\nset timeout=0\ninsmod efi_gop\nmenuentry mac {\n"
                    "  multiboot2 /boot/kernel.elf\n  module2 /mac_rom mac_rom\n"
                    "  module2 /mac_disk mac_disk\n  boot\n}\n")
        iso = os.path.join(tmp, "mac.iso")
        subprocess.run(["grub-mkrescue", "-o", iso, root], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        variables = os.path.join(tmp, "vars.fd")
        subprocess.run(["cp", OVMF_VARS, variables], check=True)
        serial = os.path.join(tmp, "serial.log")
        monitor = os.path.join(tmp, "monitor.sock")
        process = subprocess.Popen(
            ["qemu-system-x86_64", *accel, "-machine", "q35", "-m", "512",
             "-drive", f"if=pflash,format=raw,readonly=on,file={OVMF_CODE}",
             "-drive", f"if=pflash,format=raw,file={variables}",
             "-cdrom", iso, "-display", "none", "-vga", "none",
             "-device", "VGA,xres=2560,yres=1440,vgamem_mb=64",
             "-serial", f"file:{serial}", "-monitor", f"unix:{monitor},server,nowait"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        text = ""
        try:
            deadline = time.time() + 900
            while time.time() < deadline:
                time.sleep(1)
                if os.path.exists(serial):
                    with open(serial, errors="replace") as f:
                        text = f.read()
                if "mac: screen crc" in text or "mac: could not run" in text:
                    break
            time.sleep(1)
            with socket.socket(socket.AF_UNIX) as s:
                s.connect(monitor)
                s.sendall(f"screendump {shot}\n".encode())
                time.sleep(2)
        finally:
            process.kill()
            process.wait()
    lines = [line for line in text.splitlines() if line.startswith("mac:") or line.startswith("Framebuffer:")]
    print("\n".join(lines))
    if not any("screen crc" in line for line in lines):
        print("mac-uefi-shot: FAIL: the Mac did not finish")
        return 1
    print(f"mac-uefi-shot: screenshot {shot}")
    return 0


sys.exit(main())
