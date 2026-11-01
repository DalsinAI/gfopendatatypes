#!/bin/sh
# Copyright (c) 2026 AmigaChrome Team. MIT licence (see LICENSE at the repository root).
# GFOpenDatatypes' picture and sound classes (libraries/opendatatypes/classes/
# datatypes: picture.datatype, ilbm.datatype, sound.datatype, 8svx.datatype):
# each core is lean C++ (no exceptions, no RTTI, no libstdc++), built with
# an m68k cross G++ at -O0 and -O2 for the 68040; checked
# with nm to call nothing but libgcc's arithmetic (the system comes in
# through struct odt_cenv and struct dtc_ext) and to hold no C++ runtime
# (__cxa_*, __gxx_personality*, typeinfo, vtables, static constructors,
# guard variables, operator new and delete) and no data of its own; then
# hosted on a mock BOOPSI with a stand-in datatypesclass, a mock iffparse,
# mock graphics and a mock audio player, and run under qemu-m68k on files
# and a script python3 writes at test time (opendatatypes_picsound_ref.py),
# against the log python's own models expect: every pixel read back in
# every format, every pen of a remap, every destination and mask pixel,
# every sample, every audio.device request and every drawing call.
#   sh tests/m68k/test_opendatatypes_picsound.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
cls=$here/../../libraries/opendatatypes/classes/datatypes
cxx=${M68K_CXX:-m68k-linux-gnu-g++}
cc=${M68K_CC:-m68k-linux-gnu-gcc}
prefix=${M68K_PREFIX:-m68k-linux-gnu-}
qemu=${QEMU_M68K:-qemu-m68k}
command -v "$cxx" >/dev/null || { echo "skipped: no $cxx"; exit 0; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
fail=0

python3 -I "$here/opendatatypes_picsound_ref.py" "$tmp" "${SEED:-4310}"

CORES="picture/picture ilbm/ilbm sound/sound 8svx/svx"
LEAN="-std=gnu++23 -fno-exceptions -fno-rtti -fno-threadsafe-statics
  -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdlib++"
for build in "-O0 -m68040" "-O2 -m68040"; do
    set -- $build
    opt=$1 cpu=$2
    objs=
    for c in $CORES; do
        n=$(basename "$c")
        # -fno-pic: the Amiga build is not position independent, so const
        # tables of pointers stay in .rodata as they will there
        # shellcheck disable=SC2086
        "$cxx" $LEAN $opt $cpu -fno-pic -fno-lto -fno-builtin -fno-tree-loop-distribute-patterns \
            -fno-delete-null-pointer-checks -Wall -Wextra -Werror \
            -c "$cls/${c}_core.cpp" -o "$tmp/$n.o"
        und=$("${prefix}nm" -u "$tmp/$n.o" | awk '{print $NF}' |
              grep -v '^__\(mul\|div\|udiv\|mod\|umod\)[sd]i3$' | tr '\n' ' ' || true)
        [ -z "$und" ] || { echo "FAIL $build $n: the core calls $und"; fail=1; }
        bad=$("${prefix}nm" "$tmp/$n.o" | awk '{print $NF}' |
              grep -E '^(__cxa_|__gxx_personality|_ZTI|_ZTS|_ZTV|_GLOBAL__sub_I_|_ZGV|_Znw|_Zna|_ZdlPv|_ZdaPv)' |
              tr '\n' ' ' || true)
        [ -z "$bad" ] || { echo "FAIL $build $n: C++ runtime in the core: $bad"; fail=1; }
        data=$("${prefix}size" -A "$tmp/$n.o" | awk '$1 ~ /^\.(data|bss|init_array|ctors)/ && $2 > 0 {print $1}' |
               tr '\n' ' ' || true)
        [ -z "$data" ] || { echo "FAIL $build $n: the core keeps state in $data"; fail=1; }
        objs="$objs $tmp/$n.o"
    done
    # shellcheck disable=SC2086
    "$cc" -std=gnu99 $opt $cpu -static -Wall -Wextra -Werror \
        "$here/opendatatypes_picsound_test.c" $objs -o "$tmp/t"
    if command -v "$qemu" >/dev/null; then
        # at most two minutes and 8 MB of log a run
        { rc_=0; (cd "$tmp" && timeout 120 "$qemu" -cpu m68040 "$tmp/t" script.txt) || rc_=$?; echo $rc_ > "$tmp/rc"; } |
            head -c 8000000 > "$tmp/out.txt"
        if [ "$(cat "$tmp/rc")" != 0 ]; then
            echo "FAIL $build: the run failed (exit $(cat "$tmp/rc"))"; tail -5 "$tmp/out.txt"; fail=1; continue
        fi
        if cmp -s "$tmp/out.txt" "$tmp/expected.txt"; then
            echo "ok   $build: $(wc -l < "$tmp/script.txt") script lines, $(wc -l < "$tmp/out.txt") log lines as the reference"
        else
            echo "FAIL $build: differs from the reference:"
            diff "$tmp/expected.txt" "$tmp/out.txt" | cut -c1-200 | head -30
            fail=1
        fi
    else
        echo "ok   $build: built (no $qemu to run it)"
    fi
done
exit $fail
