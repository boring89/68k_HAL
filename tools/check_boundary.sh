#!/bin/sh
#
# Copyright (C) 2026 YuanChi Hsieh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Check the include and layer boundaries between kernel/, hal/, arch/,
# platform/ and drivers/ (see docs/hal.md).
#
# Usage: tools/check_boundary.sh [root]
#
# Scans the C sources and headers under root (default: the current
# directory) and prints every line that breaks a rule as
# "boundary: <rule>: <file>:<line>:<text>". Exits 1 if any rule is
# broken, 0 otherwise. Each rule has a fixture tree under
# tests/boundary/<rule>/ that must break it.

root=${1:-.}
status=0

INCLUDE='^[[:space:]]*#[[:space:]]*include[[:space:]]*"'

# The same, for <allowed-regex>: it is matched against grep's
# "<file>:<line>:<text>" output.
ALLOWED_INCLUDE=':[0-9]+:[[:space:]]*#[[:space:]]*include[[:space:]]*"'

# report <rule> <matches>
report()
{
    if [ -n "$2" ]; then
        printf '%s\n' "$2" | sed "s|^|boundary: $1: |"
        status=1
    fi
}

# check <rule> <regex> <allowed-regex> <dir>...
# Report every line in the .c/.h files under <dir> that matches <regex>
# unless it also matches <allowed-regex> (empty: nothing is allowed).
# Directories that do not exist are skipped.
check()
{
    rule=$1
    regex=$2
    allowed=$3
    shift 3

    for dir in "$@"; do
        [ -d "$root/$dir" ] || continue

        matches=$(grep -rnE --include='*.c' --include='*.h' \
            "$regex" "$root/$dir")

        if [ -n "$allowed" ] && [ -n "$matches" ]; then
            matches=$(printf '%s\n' "$matches" | grep -vE "$allowed")
        fi

        report "$rule" "$matches"
    done
}

SOURCE_DIRS="arch boot drivers emu hal kernel platform"

# Everywhere: include paths start at the repository root.
check relative_include "$INCLUDE\.\./" "" $SOURCE_DIRS

# kernel/ sees the hardware only through hal/.
check kernel_includes_lower "$INCLUDE(arch|platform|drivers)/" "" kernel
check kernel_hardware_access \
    '(\b(asm|__asm__)\b[[:space:]]*(volatile|__volatile__)?[[:space:]]*\(|\b(inb|inw|inl|outb|outw|outl)[[:space:]]*\()' \
    "" kernel

# hal/ is only the contract: headers that depend on nothing else.
check hal_includes "$INCLUDE(arch|platform|drivers|kernel)/" "" hal

if [ -d "$root/hal" ]; then
    report hal_headers_only "$(find "$root/hal" -type f ! -name '*.h')"
fi

# arch/ is the CPU: no platform or devices, and only the kernel's
# libraries, memory allocators, panic, printf and self test framework.
check arch_includes "$INCLUDE(platform|drivers)/" "" arch
check arch_includes_kernel "${INCLUDE}kernel/" \
    "${ALLOWED_INCLUDE}kernel/(lib|memory|panic|printf|test)/" arch

# drivers/ use the kernel's device interfaces, hal/ and port I/O only;
# they never know the platform or each other.
check driver_includes "${INCLUDE}(platform/|arch/)" \
    "${ALLOWED_INCLUDE}arch/[a-z0-9_]+/io/" drivers

if [ -d "$root/drivers" ]; then
    for driver in "$root"/drivers/*/; do
        name=$(basename "$driver")
        check driver_includes_driver "${INCLUDE}drivers/" \
            "${ALLOWED_INCLUDE}drivers/$name/" "drivers/$name"
    done
fi

# emu/ is the 68k emulator: independent of the machine, so no hal/, arch/,
# platform/ or drivers/, and from kernel/ only the libraries. Its own core is
# third_party/musashi. kernel/ may use emu/.
check emu_includes "$INCLUDE(hal|arch|platform|drivers)/" "" emu
check emu_includes_kernel "${INCLUDE}kernel/" \
    "${ALLOWED_INCLUDE}kernel/lib/" emu

exit $status
