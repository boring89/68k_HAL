#!/bin/sh
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build dash (the Debian Almquist shell, BSD license, Herbert Xu, http://gondor.apana.org.au/~herbert/dash/)
# with the C SDK: a POSIX shell that needs fork, execve, wait, pipe and signals. The release tarball is
# downloaded into third_party/downloads/dash/ (not in the repository) and checked against a known
# SHA-256 (computed when it was first downloaded; the site publishes no checksum). Needs `make sdk` first.
#
# Usage: tools/build_dash.sh <output file>

set -eu

DASH_VERSION=0.5.12
DASH_SHA256=6a474ac46e8b0b32916c4c60df694c82058d3297d8b385b74508030ca4a8f28a
DASH_URL=http://gondor.apana.org.au/~herbert/dash/files/dash-$DASH_VERSION.tar.gz

root=$(cd "$(dirname "$0")/.." && pwd)
dl=$root/third_party/downloads/dash
work=$root/build/dash
out=$1

mkdir -p "$dl" "$(dirname "$out")"

if [ ! -f "$dl/dash-$DASH_VERSION.tar.gz" ]; then
    echo "dash: downloading $DASH_URL"
    curl -sSfL -o "$dl/dash-$DASH_VERSION.tar.gz" "$DASH_URL"
fi

echo "$DASH_SHA256  $dl/dash-$DASH_VERSION.tar.gz" | sha256sum -c - >/dev/null ||
    { echo "dash: the tarball has the wrong checksum"; exit 1; }

[ -d "$dl/dash-$DASH_VERSION" ] || tar -C "$dl" -xzf "$dl/dash-$DASH_VERSION.tar.gz"

# Out of tree; the build helpers (mkinit, mknodes...) are static i386 programs the host can run too.
mkdir -p "$work"

if [ ! -f "$work/Makefile" ]; then
    (cd "$work" && CC="$root/tools/68khal-cc" CFLAGS="-O2 -w" \
        "$dl/dash-$DASH_VERSION/configure" --enable-static --host=i686-linux >/dev/null)
fi

make -C "$work" -j4 >/dev/null
cp "$work/src/dash" "$out"
