#!/bin/sh
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Check the include and layer boundaries between kernel/, hal/, arch/,
# platform/ and drivers/.
#
# Usage: tools/check_boundary.sh [root]
#
# Scans the C sources and headers under root (default: the current
# directory) and prints every line that breaks a rule. Exits 1 if any
# rule is broken, 0 otherwise.

root=${1:-.}
status=0

# check <rule> <regex> <dir>...
# Report every line in the .c/.h files under <dir> that matches <regex>.
# Directories that do not exist are skipped.
check()
{
    rule=$1
    regex=$2
    shift 2

    for dir in "$@"; do
        [ -d "$root/$dir" ] || continue

        matches=$(grep -rnE --include='*.c' --include='*.h' \
            "$regex" "$root/$dir")

        if [ -n "$matches" ]; then
            printf '%s\n' "$matches" | sed "s|^|boundary: $rule: |"
            status=1
        fi
    done
}

SOURCE_DIRS="arch boot drivers emu hal kernel platform"

check "relative include" '^[[:space:]]*#[[:space:]]*include[[:space:]]*"\.\./' \
    $SOURCE_DIRS

exit $status
