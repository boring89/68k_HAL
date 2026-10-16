#!/usr/bin/env python3
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check a recorded .wav file for a tone.

Usage: tools/check_wav.py FILE.wav FREQUENCY_HZ

Passes if the file has a stretch of clearly audible signal whose
frequency (counted from sign changes) is within 5% of FREQUENCY_HZ and
the rest is not louder than that. Exit status 0 = pass.
"""

import struct
import sys


def read_wav(path):
    data = open(path, "rb").read()

    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("not a WAV file")

    pos = 12
    fmt = None

    while pos + 8 <= len(data):
        chunk = data[pos:pos + 4]
        size = struct.unpack("<I", data[pos + 4:pos + 8])[0]

        if chunk == b"fmt ":
            fmt = struct.unpack("<HHIIHH", data[pos + 8:pos + 24])
        elif chunk == b"data":
            # QEMU's wav writer may leave a 0 or 0xFFFFFFFF size if it was killed.
            end = len(data) if size in (0, 0xFFFFFFFF) or pos + 8 + size > len(data) else pos + 8 + size
            return fmt, data[pos + 8:end]

        pos += 8 + size + (size & 1)

    raise ValueError("no data chunk")


def main():
    path, frequency = sys.argv[1], float(sys.argv[2])
    fmt, pcm = read_wav(path)
    channels, rate, bits = fmt[1], fmt[2], fmt[5]

    if bits != 16:
        print("check_wav: expected 16-bit samples, got %d" % bits)
        return 1

    count = len(pcm) // 2
    samples = struct.unpack("<%dh" % count, pcm[:count * 2])[::channels]

    peak = max(abs(s) for s in samples) if samples else 0
    loud = [i for i, s in enumerate(samples) if abs(s) > peak // 4]

    if peak < 2000 or not loud:
        print("check_wav: FAIL: silent (peak %d over %d frames)" % (peak, len(samples)))
        return 1

    first, last = loud[0], loud[-1]
    span = samples[first:last + 1]
    crossings = sum(1 for a, b in zip(span, span[1:]) if (a < 0) != (b < 0))
    seconds = len(span) / rate
    measured = crossings / 2 / seconds if seconds > 0 else 0

    print("check_wav: peak %d, tone %.3f s, about %.0f Hz" % (peak, seconds, measured))

    if abs(measured - frequency) > frequency * 0.05 or seconds < 0.3:
        print("check_wav: FAIL: expected %.0f Hz for at least 0.3 s" % frequency)
        return 1

    return 0


sys.exit(main())
