#!/usr/bin/env python3
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
"""Boot the ISO in QEMU, type shell commands, and check the serial output.

Usage: tools/shell_test.py ISO COMMAND=EXPECTED_TEXT [COMMAND=EXPECTED_TEXT ...]

Each COMMAND is typed at the "HAL> " prompt through the QEMU monitor
(sendkey); the run passes if EXPECTED_TEXT then shows up on the serial
port (only what goes through the kernel log and the console sinks, i.e.
the output of user programs; the shell prints to the screen only). Extra QEMU arguments can be given in the QEMU_ARGS environment
variable.
"""

import os
import socket
import subprocess
import sys
import tempfile
import time

KEYS = {
    " ": "spc", "\n": "ret", "/": "slash", ".": "dot", "-": "minus", "_": "shift-minus",
    "=": "equal", ",": "comma", ":": "shift-semicolon", "\"": "shift-apostrophe",
}


def keys_for(text):
    for ch in text:
        if ch in KEYS:
            yield KEYS[ch]
        elif ch.isupper():
            yield "shift-" + ch.lower()
        else:
            yield ch


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2

    iso = sys.argv[1]
    steps = [arg.split("=", 1) for arg in sys.argv[2:]]
    tmp = tempfile.mkdtemp()
    serial = os.path.join(tmp, "serial.log")
    monitor = os.path.join(tmp, "mon.sock")
    cmd = ["qemu-system-i386", "-cdrom", iso, "-display", "none",
           "-serial", "file:" + serial, "-monitor", "unix:%s,server,nowait" % monitor,
           "-m", "128"] + os.environ.get("QEMU_ARGS", "").split()
    qemu = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    try:
        for _ in range(100):
            if os.path.exists(monitor):
                break
            time.sleep(0.1)

        sock = socket.socket(socket.AF_UNIX)
        sock.connect(monitor)
        sock.settimeout(1)

        def send(line):
            sock.sendall((line + "\n").encode())
            time.sleep(0.04)
            try:
                sock.recv(65536)
            except socket.timeout:
                pass

        def output():
            try:
                return open(serial, errors="replace").read()
            except FileNotFoundError:
                return ""

        def wait_for(text, seconds, start=0):
            end = time.time() + seconds
            while time.time() < end:
                if text in output()[start:]:
                    return True
                time.sleep(0.2)
            return False

        # The prompt itself goes to the screen only; the kernel logs when it is ready.
        if not wait_for("Shell ready", 30):
            print("shell_test: the kernel did not get to the shell")
            return 1

        time.sleep(0.5)

        for command, expected in steps:
            mark = len(output())
            for key in keys_for(command + "\n"):
                send("sendkey " + key)
            if not wait_for(expected, 30, mark):
                print("shell_test: FAIL: '%s' did not print '%s'" % (command, expected))
                print(output()[mark:][-600:])
                return 1
            print("shell_test: ok: %s" % command)

        return 0
    finally:
        qemu.kill()
        qemu.wait()


sys.exit(main())
