#!/bin/sh
# Copyright (c) 2026 AmigaChrome Team. MIT licence (see LICENSE at the repository root).
# GFOpenDatatypes (datatypes.library) core: libraries/opendatatypes/dt_core.c,
# built with an m68k cross GCC at -O0 and -O2 for the 68040,
# checked to call nothing but libgcc, and run under qemu-m68k against
# tests/m68k/opendatatypes_test.c.
#   sh tests/m68k/test_opendatatypes.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
lib=$here/../../libraries/opendatatypes
cc=${M68K_CC:-m68k-linux-gnu-gcc}
prefix=${M68K_PREFIX:-m68k-linux-gnu-}
qemu=${QEMU_M68K:-qemu-m68k}
command -v "$cc" >/dev/null || { echo "skipped: no $cc"; exit 0; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
fail=0

for build in "-O0 -m68040" "-O2 -m68040"; do
    set -- $build
    opt=$1 cpu=$2
    # shellcheck disable=SC2086
    "$cc" -std=gnu99 $opt $cpu -fno-lto -fno-builtin -fno-tree-loop-distribute-patterns \
        -Wall -Wextra -Werror -c "$lib/dt_core.c" -o "$tmp/dt_core.o"
    und=$("${prefix}nm" -u "$tmp/dt_core.o" | awk 'NF == 2 {print $2}' |
          grep -v '^__\(mul\|div\|udiv\|mod\|umod\)[sd]i3$' | tr '\n' ' ' || true)
    [ -z "$und" ] || { echo "FAIL $build: the core calls $und"; fail=1; }
    # shellcheck disable=SC2086
    "$cc" -std=gnu99 $opt $cpu -static -Wall -Wextra -Werror -I"$lib" \
        "$here/opendatatypes_test.c" "$tmp/dt_core.o" -o "$tmp/t"
    if command -v "$qemu" >/dev/null; then
        if out=$("$qemu" -cpu m68040 "$tmp/t"); then
            echo "ok   $build: $(echo "$out" | tail -1)"
        else
            echo "FAIL $build:"; echo "$out" | head -20; fail=1
        fi
    else
        echo "ok   $build: built (no $qemu to run it)"
    fi
done
exit $fail
