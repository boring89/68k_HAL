#!/bin/sh
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build the HFS test volume for tests/host/test_hfs.c with hfsutils.
#
# Usage: tools/make_hfs_image.sh <output.img>
#
# Needs hfsutils (hformat, hmount, hcopy, hmkdir, hdel, humount) and python3.
# hfsutils keeps its "current volume" in $HOME/.hcwd, so the script runs with
# a temporary HOME and never touches the real one.
#
# The volume is 800 KiB (the smallest hformat accepts) with 512-byte
# allocation blocks, filled so that the "Frag" file has to be spread over
# 30 separate extents: more than the 3 a catalog record holds, so reading it
# uses the extents overflow B*-tree. Everything is generated here; no Apple
# software is involved. The expected contents are in tests/host/test_hfs.c.

set -e

out=$1
[ -n "$out" ] || { echo "usage: $0 <output.img>" >&2; exit 1; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export HOME=$work

case $out in
/*) img=$out ;;
*) img=$PWD/$out ;;
esac
here=$(cd "$(dirname "$0")" && pwd)

mkdir "$work/src"

# Test file contents. pattern N SEED: N bytes, byte i = (i * 7 + i / 251 + SEED) mod 256.
python3 -I - "$work/src" <<'PY'
import os
import struct
import sys

d = sys.argv[1]

def pattern(n, seed):
    return bytes((i * 7 + i // 251 + seed) & 255 for i in range(n))

def put(name, data):
    with open(os.path.join(d, name), "wb") as f:
        f.write(data)

put("readme", b"Hello from HFS\r")
put("empty", b"")
put("note", b"A note in Docs\r")
put("leaf", b"Leaf of the deepest folder\r")
put("mixed", b"Mixed case name\r")
put("big", pattern(100000, 1))
put("frag", pattern(30 * 512, 2))

def crc16(data):
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc

def macbinary(name, ftype, creator, data, rsrc):
    h = bytearray(128)
    h[1] = len(name)
    h[2:2 + len(name)] = name
    h[65:69] = ftype
    h[69:73] = creator
    h[83:87] = struct.pack(">I", len(data))
    h[87:91] = struct.pack(">I", len(rsrc))
    h[122] = 129
    h[123] = 129
    h[124:126] = struct.pack(">H", crc16(bytes(h[:124])))
    def pad(b):
        return b + bytes(-len(b) % 128)
    return bytes(h) + pad(data) + pad(rsrc)

put("withrsrc.bin", macbinary(b"WithRsrc", b"TEXT", b"ttxt",
                              b"Data fork of WithRsrc\r", pattern(3000, 3)))
PY

mkdir "$work/fill"
i=0
while [ $i -lt 1400 ]; do
    i=$((i + 1))
    name=$(printf 'f%04d' $i)
    head -c 100 /dev/zero | tr '\0' x > "$work/fill/$name"
done

rm -f "$img"
truncate -s 800K "$img"
hformat -f -l TESTVOL "$img" 0 > /dev/null
hmount "$img" > /dev/null

hmkdir :Docs
hmkdir :Docs:Deep
hmkdir :Docs:Deep:Deeper
hmkdir :Fill

hcopy -t "$work/src/readme" :ReadMe
hcopy -t "$work/src/empty" :Empty
hcopy -t "$work/src/note" :Docs:Note
hcopy -t "$work/src/leaf" :Docs:Deep:Deeper:Leaf
hcopy -t "$work/src/mixed" ":Docs:Deep:Mixed Case Name"
hcopy -m "$work/src/withrsrc.bin" :
hcopy -r "$work/src/big" :Big

# Fill the rest of the volume (it reports "volume full" at the end), then
# free every other filler so only 1-block holes are left for "Frag".
hcopy -r "$work"/fill/* :Fill: 2> /dev/null || true
n=0
for f in $(hls :Fill); do
    n=$((n + 1))
    if [ $((n % 2)) -eq 0 ]; then
        hdel ":Fill:$f"
    fi
done
hcopy -r "$work/src/frag" :Frag

humount > /dev/null
