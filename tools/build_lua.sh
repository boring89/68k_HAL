#!/bin/sh
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build the Lua interpreter (MIT license, https://www.lua.org) with the
# C SDK: the proof that a real program can be ported to 68k_HAL. The
# source is downloaded from the official site into
# third_party/downloads/lua/ (not in the repository) and checked
# against a known SHA-256. Needs `make sdk` first.
#
# Usage: tools/build_lua.sh <output file>

set -eu

LUA_VERSION=5.4.7
LUA_SHA256=9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30
LUA_URL=https://www.lua.org/ftp/lua-$LUA_VERSION.tar.gz

root=$(cd "$(dirname "$0")/.." && pwd)
dl=$root/third_party/downloads/lua
out=$1

mkdir -p "$dl" "$(dirname "$out")"

if [ ! -f "$dl/lua-$LUA_VERSION.tar.gz" ]; then
    echo "lua: downloading $LUA_URL"
    curl -sSfL -o "$dl/lua-$LUA_VERSION.tar.gz" "$LUA_URL"
fi

echo "$LUA_SHA256  $dl/lua-$LUA_VERSION.tar.gz" | sha256sum -c - >/dev/null ||
    { echo "lua: tarball has the wrong checksum"; exit 1; }

[ -d "$dl/lua-$LUA_VERSION" ] || tar -C "$dl" -xzf "$dl/lua-$LUA_VERSION.tar.gz"

src=$dl/lua-$LUA_VERSION/src

# Everything except the compiler front end (luac.c). LUA_USE_POSIX turns
# on the POSIX functions musl has (no dlopen, no readline).
sources=$(ls "$src"/*.c | grep -v '/luac\.c$')

# shellcheck disable=SC2086
"$root/tools/68khal-cc" -O2 -w -DLUA_USE_POSIX $sources -o "$out" -lm
