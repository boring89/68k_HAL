#!/bin/sh
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build the SQLite command line shell (public domain, https://sqlite.org)
# with the C SDK: a big real program (a database engine) that exercises
# files, locking calls, memory and time. The "amalgamation" source is
# downloaded from the official site into third_party/downloads/sqlite/ (not
# in the repository) and checked against the SHA3-256 hash that sqlite.org
# publishes on its download page. Needs `make sdk` first.
#
# Usage: tools/build_sqlite.sh <output file>

set -eu

SQLITE_NAME=sqlite-amalgamation-3540100
SQLITE_URL=https://www.sqlite.org/2026/$SQLITE_NAME.zip
SQLITE_SHA3=92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30

root=$(cd "$(dirname "$0")/.." && pwd)
dl=$root/third_party/downloads/sqlite
out=$1

mkdir -p "$dl" "$(dirname "$out")"

if [ ! -f "$dl/$SQLITE_NAME.zip" ]; then
    echo "sqlite: downloading $SQLITE_URL"
    curl -sSfL -o "$dl/$SQLITE_NAME.zip" "$SQLITE_URL"
fi

python3 -I - "$dl/$SQLITE_NAME.zip" "$SQLITE_SHA3" <<'PYTHON' || { echo "sqlite: the download has the wrong checksum"; exit 1; }
import hashlib, sys
digest = hashlib.sha3_256(open(sys.argv[1], "rb").read()).hexdigest()
sys.exit(0 if digest == sys.argv[2] else 1)
PYTHON

if [ ! -d "$dl/$SQLITE_NAME" ]; then
    python3 -I -c "import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" \
        "$dl/$SQLITE_NAME.zip" "$dl"
fi

src=$dl/$SQLITE_NAME

# One thread, no plug-in loading, no write-ahead log (it needs shared memory
# between processes), no readline.
"$root/tools/68khal-cc" -O2 -w \
    -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_OMIT_WAL \
    -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_OS_UNIX=1 \
    "$src/shell.c" "$src/sqlite3.c" -o "$out" -lm
