#!/bin/sh
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build the Tiny C Compiler (LGPL-2.1, https://bellard.org/tcc/) with
# the C SDK, so that the OS can compile C programs itself. The source
# is downloaded from the official download site into
# third_party/downloads/tinycc/ (not in the repository) and checked
# against a known SHA-256. Needs `make sdk` first.
#
# Usage: tools/build_tcc.sh <directory>
#
# Writes <directory>/bin/tcc, <directory>/usr/lib/tcc/ (the compiler's
# headers and runtime library), and copies the SDK's C library
# (<directory>/usr/include, <directory>/usr/lib), which is what the
# compiler needs to find at the paths below once <directory> is the
# root of the file tree.

set -eu

TCC_VERSION=0.9.27
TCC_SHA256=de23af78fca90ce32dff2dd45b3432b2334740bb9bb7b05bf60fdbfc396ceb9c
TCC_URL=https://download.savannah.gnu.org/releases/tinycc/tcc-$TCC_VERSION.tar.bz2

root=$(cd "$(dirname "$0")/.." && pwd)
dl=$root/third_party/downloads/tinycc
out=$1
sdk=$root/build/sdk
work=$root/build/tcc

mkdir -p "$dl" "$out/bin" "$out/usr/lib/tcc/include"

if [ ! -f "$dl/tcc-$TCC_VERSION.tar.bz2" ]; then
    echo "tcc: downloading $TCC_URL"
    curl -sSfL -o "$dl/tcc-$TCC_VERSION.tar.bz2" "$TCC_URL"
fi

echo "$TCC_SHA256  $dl/tcc-$TCC_VERSION.tar.bz2" | sha256sum -c - >/dev/null ||
    { echo "tcc: tarball has the wrong checksum"; exit 1; }

[ -d "$dl/tcc-$TCC_VERSION" ] || tar -C "$dl" -xjf "$dl/tcc-$TCC_VERSION.tar.bz2"

# Build in a copy: tcc's makefile builds in the source tree.
rm -rf "$work"
mkdir -p "$work"
cp -r "$dl/tcc-$TCC_VERSION/." "$work/"

(
    cd "$work"
    ./configure --cc="$root/tools/68khal-cc" --cpu=i386 --prefix=/usr \
        --sysincludepaths=/usr/include:/usr/lib/tcc/include \
        --libpaths=/usr/lib:/usr/lib/tcc --crtprefix=/usr/lib \
        --config-musl --extra-cflags="-O2 -w" >configure.log 2>&1
    make tcc libtcc1.a >make.log 2>&1
)

# musl's objects also call __udivmoddi4 and __divmoddi4, which libtcc1.a lacks.
cp "$work/libtcc1.a" "$out/usr/lib/tcc/libtcc1.a"
gcc -m32 -O2 -fno-pie -fno-stack-protector -ffreestanding -DRT_ONLY_DIVMOD4 -c \
    "$root/sdk/rt/divdi3.c" -o "$work/divmod4.o"
ar rs "$out/usr/lib/tcc/libtcc1.a" "$work/divmod4.o" 2>/dev/null

cp "$work/tcc" "$out/bin/tcc"
cp "$work"/include/*.h "$out/usr/lib/tcc/include/"

mkdir -p "$out/usr/lib"
rm -rf "$out/usr/include"
cp -r "$sdk/sysroot/include" "$out/usr/include"
cp "$sdk/sysroot/lib/crt1.o" "$sdk/sysroot/lib/crti.o" "$sdk/sysroot/lib/crtn.o" \
    "$sdk/sysroot/lib/libc.a" "$out/usr/lib/"
