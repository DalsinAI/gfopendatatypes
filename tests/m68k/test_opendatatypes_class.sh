#!/bin/sh
# Copyright (c) 2026 AmigaChrome Team. MIT licence (see LICENSE at the repository root).
# GFOpenDatatypes' datatypesclass (libraries/opendatatypes/dtclass_core.c,
# with dt_core.c): built with an m68k cross GCC at -O0 and -O2 for the
# 68000, 68020 and 68040; checked with nm to call nothing but libgcc's
# arithmetic and to keep no state of its own; then hosted on
# GFOpenIntuition's BOOPSI core (rootclass and gadgetclass, from
# libraries/openintuition as it is on main) with a probe subclass and run
# under qemu-m68k on a script python3 writes at test time
# (opendatatypes_class_ref.py), against the log python's own model of the
# class expects: sources opened and closed, scroll numbers and their
# notifications, layouts, drag scrolling and drag selecting, the layout
# and printing processes; LaunchToolA's command lines, DTST_RAM's group
# classes, and sys/datatypes.catalog's descriptor against the built-in
# strings.
#   sh tests/m68k/test_opendatatypes_class.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
lib=$here/../../libraries/opendatatypes
# GFOpenIntuition's BOOPSI and GFOpenUtility's hooks are amigachrome-guest's:
# GUEST= a checkout (default amigachrome-guest, else guest, beside this
# repository).
g=${GUEST:-$here/../../../amigachrome-guest}
[ -d "$g" ] || g=$here/../../../guest
oi=$g/libraries/openintuition
ou=$g/libraries/openutility
[ -d "$oi" ] && [ -d "$ou" ] || { echo "skipped: no amigachrome-guest checkout at $g (set GUEST)"; exit 0; }
cc=${M68K_CC:-m68k-linux-gnu-gcc}
prefix=${M68K_PREFIX:-m68k-linux-gnu-}
qemu=${QEMU_M68K:-qemu-m68k}
command -v "$cc" >/dev/null || { echo "skipped: no $cc"; exit 0; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
fail=0

python3 -I "$here/opendatatypes_class_ref.py" "$tmp/script.txt" "$tmp/expected.txt" "${SEED:-3210}" \
    "$lib/catalogs/datatypes.cd"

for build in "-O0 -m68040" "-O2 -m68040"; do
    set -- $build
    opt=$1 cpu=$2
    for c in dtclass_core dt_core; do
        # shellcheck disable=SC2086
        "$cc" -std=gnu99 $opt $cpu -fno-lto -fno-builtin -fno-tree-loop-distribute-patterns \
            -fno-delete-null-pointer-checks -Wall -Wextra -Werror -c "$lib/$c.c" -o "$tmp/$c.o"
    done
    und=$("${prefix}nm" -u "$tmp/dtclass_core.o" | awk '{print $NF}' |
          grep -v '^__\(mul\|div\|udiv\|mod\|umod\)[sd]i3$' | grep -v '^odt_\(next_tag\|FindMethod\)$' |
          tr '\n' ' ' || true)
    [ -z "$und" ] || { echo "FAIL $build: datatypesclass calls $und"; fail=1; }
    data=$("${prefix}size" -A "$tmp/dtclass_core.o" | awk '$1 ~ /^\.(data|bss)/ && $2 > 0 {print $1}' |
           tr '\n' ' ' || true)
    [ -z "$data" ] || { echo "FAIL $build: datatypesclass keeps state in $data"; fail=1; }
    for f in "$oi/boopsi_core.c" "$oi/classes_core.c" "$oi/images_core.c" "$oi/look_core.c" "$oi/drawing_core.c" \
             "$oi/string_core.c" "$oi/pointer_core.c" "$ou/utility_core.c"; do
        # shellcheck disable=SC2086
        "$cc" -std=gnu99 $opt $cpu -fno-builtin -Wall -Wextra -Werror -c "$f" -o "$tmp/$(basename "$f" .c).o"
    done
    # shellcheck disable=SC2086
    "$cc" -std=gnu99 $opt $cpu -static -Wall -Wextra -Werror -I"$oi" -I"$lib" \
        "$here/opendatatypes_class_test.c" "$tmp/dtclass_core.o" "$tmp/dt_core.o" "$tmp/boopsi_core.o" \
        "$tmp/classes_core.o" "$tmp/images_core.o" "$tmp/look_core.o" "$tmp/drawing_core.o" "$tmp/string_core.o" \
        "$tmp/pointer_core.o" "$tmp/utility_core.o" -o "$tmp/t"
    if command -v "$qemu" >/dev/null; then
        if ! timeout 120 "$qemu" -cpu m68040 "$tmp/t" "$tmp/script.txt" | head -c 8000000 > "$tmp/out.txt"; then
            echo "FAIL $build: the run failed"; tail -5 "$tmp/out.txt"; fail=1; continue
        fi
        if cmp -s "$tmp/out.txt" "$tmp/expected.txt"; then
            echo "ok   $build: $(wc -l < "$tmp/script.txt") script lines, $(wc -l < "$tmp/out.txt") log lines as the model"
        else
            echo "FAIL $build: differs from the model:"
            diff "$tmp/expected.txt" "$tmp/out.txt" | cut -c1-200 | head -30
            fail=1
        fi
    else
        echo "ok   $build: built (no $qemu to run it)"
    fi
done
exit $fail
