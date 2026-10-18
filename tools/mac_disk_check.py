#!/usr/bin/env python3
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Drive the Mac in the kernel under QEMU with a 68k_HAL disk image (tools/make_mac_disk.py) on IDE:
# boot, type shell commands, take screenshots. Local only (needs the user's Mac ROM); every run works
# on the image file it is given (make a copy first).
#
# Usage: mac_disk_check.py --kernel K --rom ROM --disk IMG [--append CMDLINE] [--config CFG] [--run STEPS...]
#
# STEPS, in order:
#   shell:TEXT       type TEXT and Enter at the shell prompt (e.g. "shell:video 1024 768", "shell:mac")
#   wait:SECONDS     wait
#   shot:FILE.ppm    QEMU screendump
#   key:NAME         a QEMU key name (sendkey), e.g. key:alt-n, key:grave_accent (stops the Mac)
#   click:X,Y        move the (absolute-looking) mouse by relative steps to the pixel and click is not
#                    supported: use mouse:DX,DY and button:down|up
#   mouse:DX,DY      relative mouse move
#   button:down|up   left button
#   monitor:TEXT     a QEMU monitor command, e.g. monitor:system_powerdown (the ACPI power button)
#   expect:TEXT      the serial output must contain TEXT (fails the run otherwise)
#   quit             wait for QEMU to exit by itself (the Mac's Shut Down), at most 120 s; fails if it does not

import argparse
import os
import socket
import subprocess
import sys
import tempfile
import time

OVMF_CODE = "/usr/share/OVMF/OVMF_CODE_4M.fd"
OVMF_VARS = "/usr/share/OVMF/OVMF_VARS_4M.fd"

KEYS = {" ": "spc", "\n": "ret", "=": "equal", "`": "grave_accent", "/": "slash", ".": "dot", "-": "minus",
        "_": "shift-minus", ":": "shift-semicolon", ",": "comma"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kernel")
    ap.add_argument("--rom")
    ap.add_argument("--disk", required=True)
    ap.add_argument("--machine", default="")
    ap.add_argument("--mem", default="512")
    ap.add_argument("--serial-out")
    ap.add_argument("--append", default="", help="kernel command line")
    ap.add_argument("--config", help="a 68k_hal.cfg, given to the kernel as the boot module config")
    ap.add_argument("--uefi", action="store_true",
                    help="boot the disk image itself with OVMF (q35, the disk on AHCI): no --kernel/--rom")
    ap.add_argument("--vga", default="1024x768", help="--uefi: the VGA card's resolution")
    ap.add_argument("--no-reboot", action="store_true", help="QEMU exits instead of rebooting")
    ap.add_argument("--run", nargs="*", default=[])
    args = ap.parse_args()

    accel = ["-enable-kvm"] if os.access("/dev/kvm", os.W_OK) else []
    machine = args.machine.split() if args.machine else []
    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        monitor = os.path.join(tmp, "monitor.sock")
        serial = os.path.join(tmp, "serial.log")
        common = ["qemu-system-x86_64", *accel, "-m", args.mem, "-display", "none",
                  "-serial", f"file:{serial}", "-monitor", f"unix:{monitor},server,nowait"]
        if args.uefi:
            variables = os.path.join(tmp, "vars.fd")
            subprocess.run(["cp", OVMF_VARS, variables], check=True)
            xres, yres = args.vga.split("x")
            command = [*common, "-machine", "q35", *(["-no-reboot"] if args.no_reboot else []),
                       "-drive", f"if=pflash,format=raw,readonly=on,file={OVMF_CODE}",
                       "-drive", f"if=pflash,format=raw,file={variables}",
                       "-drive", f"file={args.disk},format=raw,if=none,id=d0",
                       "-device", "ide-hd,drive=d0,bus=ide.0",
                       "-vga", "none", "-device", f"VGA,xres={xres},yres={yres},vgamem_mb=64"]
        else:
            command = [*common, *machine,
                       "-drive", f"file={args.disk},format=raw,if=ide,index=0,media=disk",
                       "-kernel", args.kernel, "-append", args.append,
                       "-initrd", f"{args.rom} mac_rom" + (f",{args.config} config" if args.config else "")]
        process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        def log():
            return open(serial, errors="replace").read() if os.path.exists(serial) else ""

        try:
            deadline = time.time() + 120
            while time.time() < deadline and "Shell ready" not in log():
                time.sleep(0.5)
            for _ in range(50):
                if os.path.exists(monitor):
                    break
                time.sleep(0.1)
            sock = socket.socket(socket.AF_UNIX)
            sock.connect(monitor)

            def cmd(text):
                sock.sendall((text + "\n").encode())
                time.sleep(0.2)

            def type_text(text):
                for ch in text:
                    if ch.isupper():
                        cmd("sendkey shift-" + ch.lower())
                    else:
                        cmd("sendkey " + KEYS.get(ch, ch))

            for step in args.run:
                kind, _, value = step.partition(":")
                if kind == "shell":
                    type_text(value + "\n")
                elif kind == "wait":
                    time.sleep(float(value))
                elif kind == "shot":
                    cmd("screendump " + value)
                    time.sleep(1.5)
                elif kind == "monitor":
                    cmd(value)
                elif kind == "key":
                    cmd("sendkey " + value)
                elif kind == "mouse":
                    dx, dy = value.split(",")
                    cmd(f"mouse_move {dx} {dy}")
                elif kind == "button":
                    cmd("mouse_button 1" if value == "down" else "mouse_button 0")
                elif kind == "expect":
                    if value not in log():
                        failures.append(f"missing in serial output: {value}")
                elif kind == "quit":
                    try:
                        process.wait(timeout=120)
                    except subprocess.TimeoutExpired:
                        failures.append("QEMU did not exit")
                else:
                    failures.append(f"unknown step {step}")
            sock.close()
        finally:
            if process.poll() is None:
                process.kill()
            process.wait()
            if args.serial_out:
                with open(args.serial_out, "w") as f:
                    f.write(log())

    for failure in failures:
        print("mac_disk_check: FAIL: " + failure)
    if failures:
        sys.exit(1)
    print("mac_disk_check: ok")


main()
