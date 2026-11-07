#!/bin/sh
# gfopengif.datatype and its descriptor, DEVS:DataTypes/GFOpenGIF, for AmigaOS 3.x
# (gifclass.c). Our own code only: no upstream tarballs. Built with the
# os32-gcc16 compiler, as the other datatypes (../build.sh runs this too).
# MIT, Copyright (c) 2026 AmiCradle Team.
#
#   OS32_GCC16   compiler root holding prefix/ (default ~/AmiCradle/stoves/os32-gcc16)
#   PREFIX       where Classes/ and Devs/ go (default $OUT, else ../out)
#
# usage: sh gif/build.sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
S=${OS32_GCC16:-"$HOME/AmiCradle/stoves/os32-gcc16"}
CC="$S/prefix/bin/m68k-amigaos-gcc"
OUT=${PREFIX:-${OUT:-"$HERE/out"}}
WORK="$HERE/work/obj-opengif"
# Address 0 is memory on an Amiga: keep GCC from dropping null checks (../build.sh).
# Soft float: -m68040 links libnix's soft libm020, not libm881 (see
# ../build/m68k-soft-m68040.specs).
SOFT="-specs=$HERE/../build/m68k-soft-m68040.specs"
CFLAGS="-O2 -m68040 $SOFT -fomit-frame-pointer -fno-delete-null-pointer-checks -DNDEBUG -Wall -Wno-pointer-sign"
rm -rf "$WORK"
mkdir -p "$WORK" "$OUT/Classes/DataTypes" "$OUT/Devs/DataTypes"
for f in common/dtstart.c common/dtlib.c common/dtservice.c common/dtnative.c gif/gifdecode.c gif/gifencode.c gif/gifclass.c; do
    (cd "$HERE" && $CC $CFLAGS -I"$HERE/common" -I"$HERE/include" -I"$HERE/gif" -c "$f" -o "$WORK/$(basename "$f" .c).o")
done
# dtstart.o first, so the library's first code is a safe return.
$CC -nostartfiles -m68040 $SOFT -o "$OUT/Classes/DataTypes/gfopengif.datatype" \
    "$WORK/dtstart.o" "$WORK/dtlib.o" "$WORK/dtservice.o" "$WORK/dtnative.o" "$WORK/gifdecode.o" "$WORK/gifencode.o" "$WORK/gifclass.o" -lamiga \
    -Wl,-Map="$WORK/gfopengif.datatype.map"
echo "gfopengif.datatype: $(wc -c < "$OUT/Classes/DataTypes/gfopengif.datatype") bytes"
# "GIF87a" or "GIF89a" and the four bytes of the screen size: ten bytes, a
# longer mask than the system's GIF descriptor, so it is tried first.
python3 "$HERE/common/mkdtdesc.py" "$OUT/Devs/DataTypes/GFOpenGIF" GFOpenGIF gfopengif pict gif "#?" \
    '$VER: GFOpenGIF 47.1 (3.11.2026)' "'G'" "'I'" "'F'" "'8'" ANY "'a'" ANY ANY ANY ANY
echo "Devs/DataTypes/GFOpenGIF: $(wc -c < "$OUT/Devs/DataTypes/GFOpenGIF") bytes"
