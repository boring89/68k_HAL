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
import re
import socket
import subprocess
import sys
import tempfile
import time

KEYS = {
    " ": "spc", "\n": "ret", "/": "slash", ".": "dot", "-": "minus", "_": "shift-minus",
    "=": "equal", "+": "shift-equal", "*": "shift-8", "(": "shift-9", ")": "shift-0",
    ",": "comma", ":": "shift-semicolon", "\"": "shift-apostrophe",
    "|": "shift-backslash", "<": "shift-comma", ">": "shift-dot",
}


def keys_for(text):
    # "^D" stands for Ctrl-D (no Enter after it).
    if text == "^D\n":
        yield "ctrl-d"
        return

    # "{up}" is a key by its QEMU name (sendkey): {up}, {f1}, {ctrl-c}, {delete}...
    # "{mouse_move dx dy}" moves the mouse.
    pending = None

    for ch in text:
        if pending is not None:
            if ch == "}":
                yield pending
                pending = None
            else:
                pending += ch
        elif ch == "{":
            pending = ""
        elif ch in KEYS:
            yield KEYS[ch]
        elif ch.isupper():
            yield "shift-" + ch.lower()
        else:
            yield ch


BAR_COLORS = [(255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0),
              (255, 0, 255), (255, 0, 0), (0, 0, 255), (0, 0, 0)]


def check_screen(path, width, height):
    """The QEMU screendump must be width x height and show the test pattern's colour bars."""
    data = open(path, "rb").read()
    match = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", data)
    if not match:
        print("shell_test: the screendump is not a PPM file")
        return False
    w, h = int(match.group(1)), int(match.group(2))
    if (w, h) != (width, height):
        print("shell_test: FAIL: the screen is %dx%d, expected %dx%d" % (w, h, width, height))
        return False
    body = data[match.end():]
    y = height // 4
    for i, want in enumerate(BAR_COLORS):
        x = (2 * i + 1) * width // 16
        got = tuple(body[(y * w + x) * 3:(y * w + x) * 3 + 3])
        if got != want:
            print("shell_test: FAIL: bar %d is %s, expected %s" % (i, got, want))
            return False
    print("shell_test: ok: screen is %dx%d with the colour bars" % (w, h))
    return True


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
                # "{mouse_move 7 3}" is a monitor command, not a key.
                send(key if key.startswith("mouse_") else "sendkey " + key)
            if expected == "":
                # Nothing to wait for on the serial port (the command prints
                # nothing): give it time before the next one is typed.
                time.sleep(5)
            elif not wait_for(expected, 30, mark):
                print("shell_test: FAIL: '%s' did not print '%s'" % (command, expected))
                print(output()[mark:][-600:])
                return 1
            print("shell_test: ok: %s" % command)

        # SHELL_TEST_DUMP=file.ppm: save a screendump after the last step (to look at by hand).
        dump_path = os.environ.get("SHELL_TEST_DUMP")
        if dump_path:
            time.sleep(0.5)
            send("screendump " + dump_path)
            time.sleep(1.0)

        # SHELL_TEST_SCREEN=WxH: finally take a screendump and check the test pattern.
        screen = os.environ.get("SHELL_TEST_SCREEN")
        if screen:
            width, height = (int(v) for v in screen.split("x"))
            dump = os.path.join(tmp, "screen.ppm")
            time.sleep(0.5)
            send("screendump " + dump)
            time.sleep(1.0)
            if not check_screen(dump, width, height):
                return 1

        return 0
    finally:
        qemu.kill()
        qemu.wait()


sys.exit(main())
