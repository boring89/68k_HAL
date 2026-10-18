#!/usr/bin/env python3
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Boot the ISO in QEMU and type "poweroff" and "reboot", and press the ACPI power button (monitor
# system_powerdown): QEMU must end each time (with -no-reboot a guest reset also ends it). Run on i440fx and Q35 with BIOS, and Q35 with UEFI (OVMF).
# QEMU only: this never touches the machine it runs on.
#
# Usage: tools/power_test.py [iso]   (default build/68k_HAL.iso). Not part of `make test` (it needs OVMF).

import os
import socket
import subprocess
import sys
import tempfile
import time

OVMF_CODE = "/usr/share/OVMF/OVMF_CODE_4M.fd"
OVMF_VARS = "/usr/share/OVMF/OVMF_VARS_4M.fd"
KEYS = {"p": "p", "o": "o", "w": "w", "e": "e", "r": "r", "f": "f", "b": "b", "t": "t"}

MACHINES = [
    ("i440fx BIOS", [os.environ.get("QEMU_SYSTEM", "qemu-system-x86_64")]),
    ("q35 BIOS", [os.environ.get("QEMU_SYSTEM", "qemu-system-x86_64"), "-machine", "q35"]),
    ("q35 UEFI", ["qemu-system-x86_64", "-machine", "q35"]),
]


def run(name, base, command, expected_text, iso):
    with tempfile.TemporaryDirectory() as tmp:
        serial = os.path.join(tmp, "serial.log")
        monitor = os.path.join(tmp, "monitor.sock")
        cmd = list(base)
        if "UEFI" in name:
            variables = os.path.join(tmp, "vars.fd")
            subprocess.run(["cp", OVMF_VARS, variables], check=True)
            cmd += ["-drive", f"if=pflash,format=raw,readonly=on,file={OVMF_CODE}",
                    "-drive", f"if=pflash,format=raw,file={variables}"]
        cmd += ["-m", "256", "-cdrom", iso, "-display", "none", "-no-reboot",
                "-serial", f"file:{serial}", "-monitor", f"unix:{monitor},server,nowait"]
        process = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            deadline = time.monotonic() + 40
            while time.monotonic() < deadline:
                if os.path.exists(serial) and "Shell ready" in open(serial, errors="replace").read():
                    break
                time.sleep(0.3)
            else:
                return False, "never reached the shell"
            time.sleep(1)

            with socket.socket(socket.AF_UNIX) as s:
                s.connect(monitor)
                s.settimeout(2)
                try:
                    s.recv(4096)
                except socket.timeout:
                    pass
                if command == "power button":
                    s.sendall(b"system_powerdown\n")      # the ACPI power button
                else:
                    for ch in command:
                        s.sendall(f"sendkey {KEYS[ch]}\n".encode())
                        time.sleep(0.15)
                    s.sendall(b"sendkey ret\n")

            try:
                process.wait(timeout=20)
            except subprocess.TimeoutExpired:
                return False, "QEMU did not end after the command"

            text = open(serial, errors="replace").read()
            if expected_text not in text:
                return False, f"QEMU ended but the output lacks '{expected_text}'"

            return True, ""
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()


def main():
    iso = sys.argv[1] if len(sys.argv) > 1 else "build/68k_HAL.iso"
    failed = 0
    for name, base in MACHINES:
        for command, text in (("poweroff", "Powering off"), ("reboot", "Restarting"),
                              ("power button", "Power button")):
            ok, why = run(name, base, command, text, iso)
            print(f"power_test: {'ok' if ok else 'FAIL'}: {name} / {command}" + ("" if ok else f": {why}"))
            failed += not ok
    if failed:
        sys.exit(1)
    print("power_test: PASS")


if __name__ == "__main__":
    main()
