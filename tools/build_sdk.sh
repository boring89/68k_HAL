#!/bin/sh
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build the C SDK into build/sdk/sysroot: musl (MIT) as a static i386
# libc, plus the runtime helpers in sdk/rt/ that the host's 32-bit-less
# libgcc cannot provide. See docs/userland.md.
#
# musl's source is downloaded from its official site into
# third_party/downloads/musl/ (not in the repository) and checked
# against a known SHA-256.

set -eu

MUSL_VERSION=1.2.5
MUSL_SHA256=a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4
MUSL_URL=https://musl.libc.org/releases/musl-$MUSL_VERSION.tar.gz

root=$(cd "$(dirname "$0")/.." && pwd)
dl=$root/third_party/downloads/musl
out=$root/build/sdk
sysroot=$out/sysroot

mkdir -p "$dl" "$out"

if [ ! -f "$dl/musl-$MUSL_VERSION.tar.gz" ]; then
    echo "sdk: downloading $MUSL_URL"
    curl -sSfL -o "$dl/musl-$MUSL_VERSION.tar.gz" "$MUSL_URL"
fi

echo "$MUSL_SHA256  $dl/musl-$MUSL_VERSION.tar.gz" | sha256sum -c - >/dev/null ||
    { echo "sdk: musl tarball has the wrong checksum"; exit 1; }

[ -d "$dl/musl-$MUSL_VERSION" ] || tar -C "$dl" -xzf "$dl/musl-$MUSL_VERSION.tar.gz"

if [ ! -f "$sysroot/lib/libc.a" ]; then
    rm -rf "$out/musl-build" "$sysroot"
    mkdir -p "$out/musl-build"
    (
        cd "$out/musl-build"
        CC="gcc -m32" AR=ar RANLIB=ranlib CFLAGS="-O2 -fno-pie" \
            "$dl/musl-$MUSL_VERSION/configure" --target=i386 \
            --prefix="$sysroot" --disable-shared --enable-static >configure.log 2>&1
        make -j"$(nproc)" >make.log 2>&1
        make install >install.log 2>&1
    )
fi

# Runtime helpers (libgcc replacement).
gcc -m32 -O2 -fno-pie -fno-stack-protector -ffreestanding -c \
    "$root/sdk/rt/divdi3.c" -o "$out/divdi3.o"
rm -f "$sysroot/lib/librt68k.a"
ar rc "$sysroot/lib/librt68k.a" "$out/divdi3.o"
ranlib "$sysroot/lib/librt68k.a"

echo "sdk: ready in $sysroot"
