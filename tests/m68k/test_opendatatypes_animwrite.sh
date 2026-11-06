#!/bin/sh
# GFOpenDatatypes animation.datatype's IFF ANIM writer (adt_anim_write in
# libraries/opendatatypes/classes/datatypes/animation/animation_core.c),
# built with an m68k cross GCC at -O0 and -O2 for the 68040 and run under
# qemu-m68k: films made by opendatatypes_animwrite_test.c are written and
# read back by anim.datatype's own reader (anim/anim_core.c), every frame
# forward, backward and at random. With ffmpeg there, it reads them too
# (its IFF ANIM decoder; an outside reader) and must see every frame.
#   sh tests/m68k/test_opendatatypes_animwrite.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
cls=$here/../../libraries/opendatatypes/classes/datatypes
cc=${M68K_CC:-m68k-linux-gnu-gcc}
qemu=${QEMU_M68K:-qemu-m68k}
command -v "$cc" >/dev/null || { echo "skipped: no $cc"; exit 0; }
command -v "$qemu" >/dev/null || { echo "skipped: no $qemu"; exit 0; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
fail=0
for opt in -O0 -O2; do
    for c in animation/animation_core anim/anim_core; do
        "$cc" -std=gnu99 $opt -m68040 -fno-lto -fno-builtin -fno-tree-loop-distribute-patterns \
            -fno-delete-null-pointer-checks -Wall -Wextra -Werror -c "$cls/$c.c" -o "$tmp/$(basename "$c").o"
    done
    # no 64-bit MULU.L/MULS.L/DIVU.L/DIVS.L in the writer: the 68060 traps them
    wide=$("${M68K_PREFIX:-m68k-linux-gnu-}objdump" -d "$tmp/animation_core.o" | python3 -I -c '
import re, sys
n = 0
for l in sys.stdin:
    m = re.match(r"\s*[0-9a-f]+:\s+((?:[0-9a-f]{4} )+)\s*(mul|div)", l)
    if m:
        w = m.group(1).split()
        if len(w) > 1 and (int(w[0], 16) & 0xff80) == 0x4c00 and int(w[1], 16) & 0x0400:
            n += 1
print(n)')
    [ "$wide" = 0 ] || { echo "FAIL $opt: $wide 64-bit multiplies or divides in animation_core.o (the 68060 traps them)"; fail=1; }
    "$cc" -std=gnu99 $opt -m68040 -static -Wall -Wextra -Werror -Wno-format -I"$cls" \
        "$here/opendatatypes_animwrite_test.c" "$tmp/animation_core.o" "$tmp/anim_core.o" -o "$tmp/t"
    rm -rf "$tmp/files"; mkdir "$tmp/files"
    if timeout 300 "$qemu" -cpu m68040 "$tmp/t" "$tmp/files" > "$tmp/out.txt"; then
        echo "$opt: $(tail -1 "$tmp/out.txt")"
    else
        echo "FAIL $opt:"; grep -m 20 FAIL "$tmp/out.txt" || tail -5 "$tmp/out.txt"; fail=1
    fi
done
if command -v ffmpeg >/dev/null; then
    n=0 bad=0
    for f in "$tmp"/files/*.anim; do
        want=$(echo "$f" | sed 's/.*-\([0-9]*\)\.anim$/\1/')
        got=$(ffmpeg -v error -i "$f" -f null - -progress pipe:1 2>/dev/null | sed -n 's/^frame=//p' | tail -1)
        n=$((n + 1))
        if [ "$got" != "$want" ]; then
            echo "ffmpeg reads $(basename "$f"): $got frames, not $want"; bad=$((bad + 1))
        fi
    done
    echo "ffmpeg reads $((n - bad)) of $n films with every frame"
    [ "$bad" = 0 ] || fail=1
else
    echo "ffmpeg: not installed, its check skipped"
fi
exit $fail
