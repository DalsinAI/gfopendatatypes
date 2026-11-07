#!/bin/sh
# The descriptors' recognition code (DTCD chunks): one AmigaOS load file per
# hook, hooks/<name>.c, for common/mkdtdesc.py's DT_CODE. Built with the
# os32-gcc16 compiler, no startup code and no libraries: a hook calls only
# what its DTHookContext hands it. 68020 code (the pack wants a 68040), so
# no 64-bit multiplies or divides (68060 traps): the one divide is 32-bit.
# ../build.sh runs this before the descriptors (descriptors.sh).
# MIT, Copyright (c) 2026 AmiCradle Team.
#
#   OS32_GCC16   compiler root holding prefix/ (default ~/AmiCradle/stoves/os32-gcc16)
#   DTHOOKS      where the load files go (default ../work/hooks)
#
# usage: sh hooks/build.sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
S=${OS32_GCC16:-"$HOME/AmiCradle/stoves/os32-gcc16"}
CC="$S/prefix/bin/m68k-amigaos-gcc"
OBJDUMP="$S/prefix/bin/m68k-amigaos-objdump"
OUT=${DTHOOKS:-"$HERE/work/hooks"}
mkdir -p "$OUT"
for h in matroska mpegaudio oggtheora docservice; do
    # -fno-toplevel-reorder keeps hookentry.h's entry first; address 0 is
    # memory (../build.sh, NONULL).
    $CC -O2 -m68020 -mtune=68040 -fomit-frame-pointer -fno-delete-null-pointer-checks -fno-toplevel-reorder \
        -ffreestanding -fno-builtin -nostartfiles -nostdlib -s -Wall -Wextra -Werror \
        -I"$HERE/hooks" -I"$HERE/include" -o "$OUT/$h" "$HERE/hooks/$h.c" -lgcc
    # A load file whose first code is the entry's move.l a0,-(sp) (2f08).
    head=$(od -An -v -tx1 -N4 "$OUT/$h" | tr -d ' \n')
    [ "$head" = 000003f3 ] || { echo "hooks/$h: not a load file ($head)" >&2; exit 1; }
    "$OBJDUMP" -m m68k:68040 -d "$OUT/$h" > "$OUT/$h.dis"
    first=$(awk '/^ *0:/ {print $2; exit}' "$OUT/$h.dis")
    [ "$first" = 2f08 ] || { echo "hooks/$h: the first code is $first, not the entry" >&2; exit 1; }
    # No 64-bit MULU.L/MULS.L/DIVU.L/DIVS.L (the 68060 traps them), as
    # tests/m68k/test_opendatatypes_animwrite.sh checks.
    wide=$(python3 -I -c '
import re, sys
n = 0
for l in open(sys.argv[1]):
    m = re.match(r"\s*[0-9a-f]+:\s+((?:[0-9a-f]{4} )+)\s*(mul|div)", l)
    if m:
        w = m.group(1).split()
        if len(w) > 1 and (int(w[0], 16) & 0xff80) == 0x4c00 and int(w[1], 16) & 0x0400:
            n += 1
print(n)' "$OUT/$h.dis")
    rm -f "$OUT/$h.dis"
    [ "$wide" = 0 ] || { echo "hooks/$h: $wide 64-bit multiplies or divides" >&2; exit 1; }
    echo "hooks/$h: $(wc -c < "$OUT/$h") bytes"
done
