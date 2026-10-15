#!/usr/bin/env python3
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Boot the kernel in QEMU, type a fixed list of shell commands and print
# the serial log plus the VGA text screen after each command.
#
# Usage: tools/boot_log.py [kernel.elf]   (default: build/kernel.elf)
#
# Used to check that a refactor does not change behaviour: save the
# output before the change, then diff it against the output after.
# Values that move whenever the kernel image changes size (addresses,
# frame counts) are masked; see MASKS.

import json
import os
import re
import subprocess
import sys
import tempfile
import time

# Each command runs on a cleared screen so its output fits in 25 lines.
COMMANDS = [
    "help",
    "info",
    "echo hello world",
    "memory",
    "alloc 64",
    "heap",
    "mmap",
    "m68k",
]

# (pattern, replacement) applied to every output line.
MASKS = [
    (re.compile(r"(Kernel end: |Kernel End: )\S+"), r"\1<addr>"),
    (re.compile(r"(Heap Start: |Heap Current: )\S+"), r"\1<addr>"),
    (re.compile(r"PMM: \d+ / \d+ frames free"), "PMM: <n> / <n> frames free"),
    (re.compile(r"(Allocated \d+ bytes at )\S+"), r"\1<addr>"),
    (re.compile(r"^(Header: |Address: )\S+"), r"\1<addr>"),
]

# QEMU key names for characters that are not a letter or a digit.
KEY_NAMES = {" ": "spc", "-": "minus", ".": "dot", "/": "slash"}

VGA_TEXT = 0xB8000  # VGA text buffer, 80x25 cells of (char, attribute)
VGA_WIDTH = 80
VGA_HEIGHT = 25
PROMPT = "HAL>"
TIMEOUT = 10.0  # seconds to wait for the prompt


class Qmp:
    """Minimal QMP client over QEMU's stdin/stdout."""

    def __init__(self, process):
        self.process = process
        self.read()  # greeting
        self.command("qmp_capabilities")

    def read(self):
        while True:
            line = self.process.stdout.readline()
            if not line:
                raise RuntimeError("QEMU exited")
            message = json.loads(line)
            if "event" not in message:
                return message

    def command(self, name, **arguments):
        request = {"execute": name}
        if arguments:
            request["arguments"] = arguments
        self.process.stdin.write(json.dumps(request) + "\n")
        self.process.stdin.flush()
        reply = self.read()
        if "error" in reply:
            raise RuntimeError(f"{name}: {reply['error']}")
        return reply["return"]

    def hmp(self, command_line):
        return self.command("human-monitor-command", **{"command-line": command_line})


def read_screen(qmp):
    """Return the VGA text screen as a list of lines, trailing spaces removed."""
    cells = VGA_WIDTH * VGA_HEIGHT
    dump = qmp.hmp(f"xp /{cells}xh {VGA_TEXT:#x}")
    chars = []
    for line in dump.splitlines():
        for word in line.split(":", 1)[1].split():
            byte = int(word, 16) & 0xFF
            chars.append(chr(byte) if 0x20 <= byte < 0x7F else " ")
    rows = ["".join(chars[i:i + VGA_WIDTH]).rstrip()
            for i in range(0, cells, VGA_WIDTH)]
    while rows and not rows[-1]:
        rows.pop()
    return rows


def wait_for_prompt(qmp):
    """Wait until the screen ends with an idle prompt and stops changing."""
    deadline = time.monotonic() + TIMEOUT
    previous = None
    while time.monotonic() < deadline:
        screen = read_screen(qmp)
        if screen and screen[-1] == PROMPT and screen == previous:
            return screen
        previous = screen
        time.sleep(0.2)
    raise RuntimeError("timed out waiting for the shell prompt")


def type_line(qmp, text):
    for ch in text:
        key = KEY_NAMES.get(ch, ch)
        qmp.hmp(f"sendkey {key}")
    qmp.hmp("sendkey ret")


def mask(lines):
    for line in lines:
        for pattern, replacement in MASKS:
            line = pattern.sub(replacement, line)
        yield line


def main():
    kernel = sys.argv[1] if len(sys.argv) > 1 else "build/kernel.elf"
    if not os.path.exists(kernel):
        sys.exit(f"{kernel} not found; run make first")

    with tempfile.TemporaryDirectory() as tmp:
        serial_path = os.path.join(tmp, "serial.log")
        process = subprocess.Popen(
            ["qemu-system-i386", "-kernel", kernel,
             "-display", "none", "-no-reboot",
             "-serial", f"file:{serial_path}",
             "-monitor", "none", "-qmp", "stdio"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        try:
            qmp = Qmp(process)
            wait_for_prompt(qmp)

            screens = []
            for command in COMMANDS:
                type_line(qmp, "clear")
                wait_for_prompt(qmp)
                type_line(qmp, command)
                screens.append((command, wait_for_prompt(qmp)))
        finally:
            process.kill()
            process.wait()

        with open(serial_path, errors="replace") as f:
            serial = f.read().splitlines()

    print("=== serial")
    for line in mask(serial):
        print(line)
    for command, screen in screens:
        print(f"=== screen: {command}")
        for line in mask(screen):
            print(line)


if __name__ == "__main__":
    main()
