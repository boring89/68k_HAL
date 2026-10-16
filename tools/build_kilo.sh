#!/bin/sh
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build the Kilo text editor (BSD 2-clause license, Salvatore Sanfilippo,
# https://github.com/antirez/kilo) with the C SDK: a program that needs the
# terminal (raw mode, escape sequences, window size). The source is one file,
# downloaded at a fixed commit into third_party/downloads/kilo/ (not in the
# repository) and checked against a known SHA-256. Needs `make sdk` first.
#
# Usage: tools/build_kilo.sh <output file>

set -eu

KILO_COMMIT=323d93b29bd89a2cb446de90c4ed4fea1764176e
KILO_SHA256=4a44dd0e41670a9e49ecccb338ee199334f0dd472fc7f86467569cf99c391abe
KILO_URL=https://raw.githubusercontent.com/antirez/kilo/$KILO_COMMIT/kilo.c

root=$(cd "$(dirname "$0")/.." && pwd)
dl=$root/third_party/downloads/kilo
out=$1

mkdir -p "$dl" "$(dirname "$out")"

if [ ! -f "$dl/kilo.c" ]; then
    echo "kilo: downloading $KILO_URL"
    curl -sSfL -o "$dl/kilo.c" "$KILO_URL"
fi

echo "$KILO_SHA256  $dl/kilo.c" | sha256sum -c - >/dev/null ||
    { echo "kilo: kilo.c has the wrong checksum"; exit 1; }

"$root/tools/68khal-cc" -O2 -w "$dl/kilo.c" -o "$out"
