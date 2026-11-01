#!/bin/sh
# OpenDatatypes' text.datatype, ascii.datatype, animation.datatype and
# anim.datatype (libraries/opendatatypes/classes/datatypes/): each core
# is plain C with no Amiga headers, built with an m68k cross GCC at -O0
# and -O2 for the 68040; checked with nm to call nothing
# but libgcc's arithmetic (everything else comes through the env), to
# keep no data of its own and to hold no C++ runtime; then run under
# qemu-m68k on a script python3 writes at test time
# (opendatatypes_textanim_ref.py: generated IFF ANIMs, no copyrighted
# media), against the log python's own models expect: ANIM frames pixel
# by pixel (ops 0, 5, 7, 8; short and long; XOR; interleave; backward
# seeks from the decoder's checkpoints), the text
# layout, word wrap, selection, FTXT, drawing, frame timing, sound
# offsets, remapping, the control panel, and each class through its life
# on a mock datatypesclass.
#   sh tests/m68k/test_opendatatypes_textanim.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
cls=$here/../../libraries/opendatatypes/classes/datatypes
cc=${M68K_CC:-m68k-linux-gnu-gcc}
prefix=${M68K_PREFIX:-m68k-linux-gnu-}
qemu=${QEMU_M68K:-qemu-m68k}
command -v "$cc" >/dev/null || { echo "skipped: no $cc"; exit 0; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
fail=0
mkdir "$tmp/files"

python3 -I "$here/opendatatypes_textanim_ref.py" "$tmp/files" "$tmp/script.txt" "$tmp/expected.txt" "${SEED:-2026}"

CORES="text/text_core ascii/ascii_core animation/animation_core anim/anim_core"
for build in "-O0 -m68040" "-O2 -m68040"; do
    set -- $build
    opt=$1 cpu=$2
    objs=
    for c in $CORES; do
        o=$tmp/$(basename "$c").o
        # shellcheck disable=SC2086
        "$cc" -std=gnu99 $opt $cpu -fno-lto -fno-builtin -fno-tree-loop-distribute-patterns \
            -fno-delete-null-pointer-checks -Wall -Wextra -Werror -c "$cls/$c.c" -o "$o"
        und=$("${prefix}nm" -u "$o" | awk '{print $NF}' |
              grep -v '^__\(mul\|div\|udiv\|mod\|umod\)[sd]i3$' | tr '\n' ' ' || true)
        [ -z "$und" ] || { echo "FAIL $build $c: the core calls $und"; fail=1; }
        bad=$("${prefix}nm" "$o" | awk '{print $NF}' |
              grep -E '^(__cxa_|__gxx_personality|_ZTI|_ZTS|_ZTV|_GLOBAL__sub_I_|_ZGV|_Znw|_Zna|_ZdlPv|_ZdaPv)' |
              tr '\n' ' ' || true)
        [ -z "$bad" ] || { echo "FAIL $build $c: C++ runtime in the core: $bad"; fail=1; }
        data=$("${prefix}size" -A "$o" | awk '$1 ~ /^\.(data|bss|init_array|ctors)/ && $2 > 0 {print $1}' |
               tr '\n' ' ' || true)
        [ -z "$data" ] || { echo "FAIL $build $c: the core keeps state in $data"; fail=1; }
        objs="$objs $o"
    done
    # shellcheck disable=SC2086
    "$cc" -std=gnu99 $opt $cpu -static -Wall -Wextra -Werror -Wno-format -I"$cls" \
        "$here/opendatatypes_textanim_test.c" $objs -o "$tmp/t"
    if command -v "$qemu" >/dev/null; then
        # at most two minutes and 8 MB of log a run
        { rc_=0; timeout 120 "$qemu" -cpu m68040 "$tmp/t" "$tmp/script.txt" || rc_=$?; echo $rc_ > "$tmp/rc"; } |
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
