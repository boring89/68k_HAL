#!/usr/bin/env python3
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
"""Assemble emu/mac/stub.s and write emu/mac/stub_code.h.

Usage: python3 -I tools/gen_mac_stub.py [--check]

Needs m68k-linux-gnu-as and m68k-linux-gnu-objcopy (binutils-m68k-linux-gnu).
With --check the header is not written; the exit status is 1 if it differs
from what the assembler produces (used by make test).
"""

import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, "emu", "mac", "stub.s")
HEADER = os.path.join(ROOT, "emu", "mac", "stub_code.h")


def assemble():
    with tempfile.TemporaryDirectory() as tmp:
        obj = os.path.join(tmp, "stub.o")
        out = os.path.join(tmp, "stub.bin")
        subprocess.check_call(["m68k-linux-gnu-as", "-m68030", "-o", obj, SOURCE])
        subprocess.check_call(
            ["m68k-linux-gnu-objcopy", "-O", "binary", "-j", ".text", obj, out])
        with open(out, "rb") as f:
            return f.read()


def render(code):
    lines = [
        "/*",
        " * Copyright (C) 2026 YuanChi Hsieh",
        " * SPDX-License-Identifier: GPL-3.0-or-later",
        " */",
        "",
        "/* Generated from emu/mac/stub.s by tools/gen_mac_stub.py. Do not edit. */",
        "",
        "#ifndef EMU_MAC_STUB_CODE_H",
        "#define EMU_MAC_STUB_CODE_H",
        "",
        "#include <stdint.h>",
        "",
        "static const uint8_t mac_stub_code[%d] = {" % len(code),
    ]
    for i in range(0, len(code), 12):
        chunk = ", ".join("0x%02X" % b for b in code[i:i + 12])
        lines.append("    %s," % chunk)
    lines += ["};", "", "#endif", ""]
    return "\n".join(lines)


def main():
    text = render(assemble())
    if "--check" in sys.argv:
        with open(HEADER) as f:
            if f.read() != text:
                print("emu/mac/stub_code.h is out of date; run tools/gen_mac_stub.py")
                return 1
        return 0
    with open(HEADER, "w") as f:
        f.write(text)
    return 0


sys.exit(main())
