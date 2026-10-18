#!/usr/bin/env python3
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
"""Assemble the 68k code of the Mac model and write the C headers:
emu/mac/stub.s -> emu/mac/stub_code.h (mac_stub_code) and
emu/mac/nubus_driver.s -> emu/mac/nubus_driver_code.h (mac_nubus_driver_code).

Usage: python3 -I tools/gen_mac_stub.py [--check]

Needs m68k-linux-gnu-as and m68k-linux-gnu-objcopy (binutils-m68k-linux-gnu).
With --check the headers are not written; the exit status is 1 if one differs
from what the assembler produces (used by make test).
"""

import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TARGETS = [
    ("stub.s", "stub_code.h", "mac_stub_code", "EMU_MAC_STUB_CODE_H"),
    ("nubus_driver.s", "nubus_driver_code.h", "mac_nubus_driver_code",
     "EMU_MAC_NUBUS_DRIVER_CODE_H"),
]


def assemble(source):
    with tempfile.TemporaryDirectory() as tmp:
        obj = os.path.join(tmp, "stub.o")
        out = os.path.join(tmp, "stub.bin")
        subprocess.check_call(["m68k-linux-gnu-as", "-m68030", "-o", obj, source])
        subprocess.check_call(
            ["m68k-linux-gnu-objcopy", "-O", "binary", "-j", ".text", obj, out])
        with open(out, "rb") as f:
            return f.read()


def render(code, source, name, guard):
    lines = [
        "/*",
        " * Copyright (C) 2026 YuanChi Hsieh",
        " * SPDX-License-Identifier: GPL-3.0-or-later",
        " */",
        "",
        "/* Generated from emu/mac/%s by tools/gen_mac_stub.py. Do not edit. */" % source,
        "",
        "#ifndef %s" % guard,
        "#define %s" % guard,
        "",
        "#include <stdint.h>",
        "",
        "static const uint8_t %s[%d] = {" % (name, len(code)),
    ]
    for i in range(0, len(code), 12):
        chunk = ", ".join("0x%02X" % b for b in code[i:i + 12])
        lines.append("    %s," % chunk)
    lines += ["};", "", "#endif", ""]
    return "\n".join(lines)


def main():
    status = 0
    for source, header, name, guard in TARGETS:
        text = render(assemble(os.path.join(ROOT, "emu", "mac", source)),
                      source, name, guard)
        path = os.path.join(ROOT, "emu", "mac", header)
        if "--check" in sys.argv:
            with open(path) as f:
                if f.read() != text:
                    print("emu/mac/%s is out of date; run tools/gen_mac_stub.py" % header)
                    status = 1
            continue
        with open(path, "w") as f:
            f.write(text)
    return status


sys.exit(main())
