#!/bin/sh
# gfopenpng.datatype and its descriptor, DEVS:DataTypes/GFOpenPNG, for AmigaOS 3.x
# (pngclass.c). Our own code only: no upstream tarballs. PNG out
# (pngencode.c) deflates through z.library (GFOpenZlib) at run time. Built with the
# os32-gcc16 compiler, as the other datatypes (../build.sh runs this too).
# MIT, Copyright (c) 2026 AmiCradle Team.
#
#   OS32_GCC16   compiler root holding prefix/ (default ~/AmiCradle/stoves/os32-gcc16)
#   ACFLAGS, ACLIB  AC090's native helpers for common/dtlib.c, as ../build.sh
#                passes them (AC_HELPERS=1 there); empty: none
#   PREFIX       where Classes/ and Devs/ go (default ../out)
#
# usage: sh png/build.sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
S=${OS32_GCC16:-"$HOME/AmiCradle/stoves/os32-gcc16"}
CC="$S/prefix/bin/m68k-amigaos-gcc"
OUT=${PREFIX:-"$HERE/out"}
WORK="$HERE/work/obj-openpng"
# Address 0 is memory on an Amiga: keep GCC from dropping null checks (../build.sh).
# Soft float: -m68040 links libnix's soft libm020, not libm881 (see
# ../build/m68k-soft-m68040.specs).
SOFT="-specs=$HERE/../build/m68k-soft-m68040.specs"
CFLAGS="-O2 -m68040 $SOFT -fomit-frame-pointer -fno-delete-null-pointer-checks -DNDEBUG -Wall -Wno-pointer-sign"
rm -rf "$WORK"
mkdir -p "$WORK" "$OUT/Classes/DataTypes" "$OUT/Devs/DataTypes"
for f in common/dtstart.c common/dtlib.c common/dtservice.c common/dtnative.c common/zlibrary.c png/pngdecode.c png/pngencode.c png/pngclass.c; do
    (cd "$HERE" && $CC $CFLAGS -I"$HERE/common" -I"$HERE/include" -I"$HERE/png" ${ACFLAGS:-} -c "$f" -o "$WORK/$(basename "$f" .c).o")
done
# dtstart.o first, so the library's first code is a safe return.
$CC -nostartfiles -m68040 $SOFT -o "$OUT/Classes/DataTypes/gfopenpng.datatype" \
    "$WORK/dtstart.o" "$WORK/dtlib.o" "$WORK/dtservice.o" "$WORK/dtnative.o" "$WORK/zlibrary.o" "$WORK/pngdecode.o" "$WORK/pngencode.o" \
    "$WORK/pngclass.o" ${ACLIB:-} -lamiga \
    -Wl,-Map="$WORK/gfopenpng.datatype.map"
echo "gfopenpng.datatype: $(wc -c < "$OUT/Classes/DataTypes/gfopenpng.datatype") bytes"
# Its descriptor: descriptors.sh.
. "$HERE/descriptors.sh"
desc_gfopenpng "$OUT"
