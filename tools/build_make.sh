#!/bin/sh
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build GNU make 4.4.1 (GPL-3.0-or-later, https://www.gnu.org/software/make/) with the C SDK: it needs
# fork, execve, wait, pipes and signals, and runs its recipes with /bin/sh (dash). The release
# tarball is downloaded into third_party/downloads/make/ (not in the repository) and checked against
# the SHA-256 published for it. Needs `make sdk` first.
#
# Usage: tools/build_make.sh <output file>

set -eu

MAKE_VERSION=4.4.1
MAKE_SHA256=dd16fb1d67bfab79a72f5e8390735c49e3e8e70b4945a15ab1f81ddb78658fb3
MAKE_URL=https://ftp.gnu.org/gnu/make/make-$MAKE_VERSION.tar.gz

root=$(cd "$(dirname "$0")/.." && pwd)
dl=$root/third_party/downloads/make
work=$root/build/make-4.4.1
out=$1

mkdir -p "$dl" "$(dirname "$out")"

if [ ! -f "$dl/make-$MAKE_VERSION.tar.gz" ]; then
    echo "make: downloading $MAKE_URL"
    curl -sSfL -o "$dl/make-$MAKE_VERSION.tar.gz" "$MAKE_URL"
fi

echo "$MAKE_SHA256  $dl/make-$MAKE_VERSION.tar.gz" | sha256sum -c - >/dev/null ||
    { echo "make: the tarball has the wrong checksum"; exit 1; }

[ -d "$dl/make-$MAKE_VERSION" ] || tar -C "$dl" -xzf "$dl/make-$MAKE_VERSION.tar.gz"

mkdir -p "$work"

# -std=gnu11: gnulib's K&R style declarations do not compile with the default C23 of newer GCC.
if [ ! -f "$work/Makefile" ]; then
    (cd "$work" && CC="$root/tools/68khal-cc" CFLAGS="-O2 -w -std=gnu11" LDFLAGS=-static \
        "$dl/make-$MAKE_VERSION/configure" --host=i686-linux --disable-nls --without-guile --disable-load >/dev/null)
fi

make -C "$work" -j4 >/dev/null
cp "$work/make" "$out"
