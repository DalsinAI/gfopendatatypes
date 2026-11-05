#!/bin/sh
# Lab tool, not for merging: builds giflab (giflab.c) into $OUT/C/.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
S=${OS32_GCC16:-"$HOME/AmiCradle/stoves/os32-gcc16"}
CC="$S/prefix/bin/m68k-amigaos-gcc"
OUT=${OUT:-"$HERE/out"}
mkdir -p "$OUT/C"
$CC -O2 -m68040 -specs="$HERE/../build/m68k-soft-m68040.specs" -fno-delete-null-pointer-checks -Wall -Wno-pointer-sign \
    -I"$HERE/include" -o "$OUT/C/giflab" "$HERE/giflab/giflab.c" -lamiga
echo "giflab: $(wc -c < "$OUT/C/giflab") bytes"
