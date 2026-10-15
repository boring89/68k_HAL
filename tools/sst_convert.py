#!/usr/bin/env python3
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Convert a SingleStepTests 68000 file (https://github.com/SingleStepTests/680x0,
# *.json.gz) to the line format read by tests/host/sst_run.c.
#
# Usage: python3 -I tools/sst_convert.py <file.json.gz>   (writes to stdout)
#
# One test per line, all numbers decimal, whitespace separated:
#   <name with '_' for spaces> <state> <state>
# where <state> is
#   d0..d7 a0..a6 usp ssp sr pc prefetch0 prefetch1 <n> (<addr> <byte>)*n
# The first state is "initial", the second is "final". The test data is only
# parsed here, never executed.

import gzip
import json
import sys

REGS = ["d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7",
        "a0", "a1", "a2", "a3", "a4", "a5", "a6",
        "usp", "ssp", "sr", "pc"]


def state_fields(s):
    out = [str(s[r]) for r in REGS]
    out += [str(s["prefetch"][0]), str(s["prefetch"][1])]
    out.append(str(len(s["ram"])))
    for addr, val in s["ram"]:
        out += [str(addr), str(val)]
    return out


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: sst_convert.py <file.json.gz>")
    with gzip.open(sys.argv[1], "rt") as f:
        tests = json.load(f)
    for t in tests:
        name = "_".join(t["name"].split())
        fields = [name] + state_fields(t["initial"]) + state_fields(t["final"])
        sys.stdout.write(" ".join(fields) + "\n")


main()
