#!/bin/sh
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build a small HFS volume that looks like a Mac system disk to
# kernel/mac/sysvm.c, for tests/host/test_sysvm.c: a blessed "System Folder"
# holding a file of type 'zsys' whose resource fork has a 'vm  ' 0 resource
# saying "virtual memory on", plus a few other resources around it.
#
# Usage: tools/make_macsys_image.sh <output.img>
#
# Everything is generated here (the resources hold made-up bytes); no Apple
# software is involved. Needs hfsutils and python3; hfsutils keeps its
# "current volume" in $HOME/.hcwd, so the script runs with a temporary HOME.

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

python3 -I - "$work/system.bin" <<'PY'
import struct
import sys

def resource_fork(resources):
    """resources: list of (type, id, bytes). Inside Macintosh: More Macintosh
    Toolbox, "Resource File Format"."""
    data = bytearray()
    types = {}
    for rtype, rid, body in resources:
        types.setdefault(rtype, []).append((rid, len(data)))
        data += struct.pack(">I", len(body)) + body
    type_list = bytearray(struct.pack(">H", len(types) - 1))
    refs = bytearray()
    ref_base = 2 + 8 * len(types)
    for rtype, entries in types.items():
        type_list += rtype + struct.pack(">HH", len(entries) - 1, ref_base + len(refs))
        for rid, offset in entries:
            refs += struct.pack(">hHI", rid, 0xFFFF, offset) + bytes(4)
    map_body = bytes(16) + bytes(4) + bytes(2) + bytes(2)
    map_body += struct.pack(">HH", 28, 28 + len(type_list) + len(refs))
    map_body += type_list + refs
    data_start = 256
    map_start = data_start + len(data)
    header = struct.pack(">IIII", data_start, map_start, len(data), len(map_body))
    fork = bytearray(header + bytes(data_start - 16) + data + map_body)
    fork[map_start:map_start + 16] = header
    return bytes(fork)

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
    pad = lambda b: b + bytes(-len(b) % 128)
    return bytes(h) + pad(data) + pad(rsrc)

# 'vm  ' 0 as the Memory control panel of System 7.5.3 writes it: on (1),
# 10 MB, the volume's name; the rest zero. Large filler resources before it
# put it past the first allocation blocks of the fork.
vm = struct.pack(">II", 1, 10 * 1024 * 1024) + b"\x00\x00" + b"\x07TestSys" + bytes(18)
resources = [
    (b"STR ", 0, b"\x05first"),
    (b"fill", 128, bytes((i * 13 + 5) & 255 for i in range(3000))),
    (b"vm  ", 1, b"\x00\x00\x00\x01 not the one"),
    (b"vm  ", 0, vm),
    (b"STR ", 1, b"\x06second"),
]
with open(sys.argv[1], "wb") as f:
    f.write(macbinary(b"System", b"zsys", b"MACS", b"", resource_fork(resources)))
PY

rm -f "$img"
truncate -s 800K "$img"
hformat -f -l TestSys "$img" 0 > /dev/null
hmount "$img" > /dev/null
hmkdir ":System Folder"
hcopy -m "$work/system.bin" ":System Folder:"
hattrib -b ":System Folder"
humount > /dev/null
