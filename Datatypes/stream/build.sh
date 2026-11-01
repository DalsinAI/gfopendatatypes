#!/bin/sh
# stream.datatype (the "data" class), gzip.datatype and brotli.datatype for
# AmigaOS 3.x, and the descriptors DEVS:DataTypes/GZip and Brotli. Our own
# code only: gzip.datatype inflates through z.library (GFOpenZlib) at run
# time, brotli.datatype is our decoder from RFC 7932. Built with the
# os32-gcc16 compiler, as the other datatypes (../build.sh runs this too).
# MIT, Copyright (c) 2026 AmiCradle Team.
# SPDX-License-Identifier: MIT
#
#   OS32_GCC16   compiler root holding prefix/ (default ~/AmiCradle/stoves/os32-gcc16)
#   PREFIX       where Classes/ and Devs/ go (default ../out)
#   BROTLI_RFC   RFC 7932's text (rfc7932.txt), for Brotli's dictionary; default
#                ../tarballs/rfc7932.txt or ../../tarballs/rfc7932.txt, fetched from
#                rfc-editor.org when missing (brotli/mkbrdict.py checks the
#                dictionary's SHA-256 either way)
#
# usage: sh stream/build.sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
S=${OS32_GCC16:-"$HOME/AmiCradle/stoves/os32-gcc16"}
CC="$S/prefix/bin/m68k-amigaos-gcc"
OUT=${PREFIX:-"$HERE/out"}
WORK="$HERE/work/obj-stream"
# Address 0 is memory on an Amiga: keep GCC from dropping null checks (../build.sh).
# Soft float through ../build/m68k-soft-m68040.specs. -Os: these are small.
SOFT="-specs=$HERE/../build/m68k-soft-m68040.specs"
CFLAGS="-Os -m68040 $SOFT -fomit-frame-pointer -fno-delete-null-pointer-checks -DNDEBUG -Wall -Wno-pointer-sign"
INC="-I$HERE/common -I$HERE/include -I$HERE/stream -I$HERE/gzip -I$HERE/brotli"
rm -rf "$WORK"
mkdir -p "$WORK" "$OUT/Classes/DataTypes" "$OUT/Devs/DataTypes"

SRC=${BROTLI_RFC:-}
if [ -z "$SRC" ]; then
    for t in "$HERE/tarballs" "$HERE/../tarballs"; do
        [ -f "$t/rfc7932.txt" ] && SRC="$t/rfc7932.txt" && break
    done
fi
if [ -z "$SRC" ]; then
    mkdir -p "$HERE/../tarballs"
    curl -fsSL --retry 3 -o "$HERE/../tarballs/rfc7932.txt" https://www.rfc-editor.org/rfc/rfc7932.txt ||
        { echo "stream/build.sh: no rfc7932.txt (set BROTLI_RFC)" >&2; exit 2; }
    SRC="$HERE/../tarballs/rfc7932.txt"
fi
python3 "$HERE/brotli/mkbrdict.py" "$SRC" "$WORK/brdict.c"

obj() {    # NAME FILE...: compiles each into $WORK/NAME/
    d="$WORK/$1"; shift
    mkdir -p "$d"
    for f in "$@"; do
        # shellcheck disable=SC2086
        (cd "$HERE" && $CC $CFLAGS $INC -c "$f" -o "$d/$(basename "$f" .c).o")
    done
}
link() {    # NAME: links $WORK/NAME/*.o, dtstart.o first, into NAME.datatype
    d="$WORK/$1"
    # shellcheck disable=SC2046
    $CC -nostartfiles -m68040 $SOFT -o "$OUT/Classes/DataTypes/$1.datatype" \
        "$d/dtstart.o" $(ls "$d"/*.o | grep -v '/dtstart\.o$') -lamiga -Wl,-Map="$WORK/$1.datatype.map"
    echo "$1.datatype: $(wc -c < "$OUT/Classes/DataTypes/$1.datatype") bytes"
}
obj stream common/dtstart.c common/dtlib.c stream/sdtcore.c stream/streamclass.c
link stream
obj gzip common/dtstart.c common/dtlib.c common/zlibrary.c gzip/gzcore.c gzip/gzipclass.c
link gzip
obj brotli common/dtstart.c common/dtlib.c brotli/brdecode.c "$WORK/brdict.c" brotli/brotliclass.c
link brotli

# gzip: 1f 8b and method 8 (RFC 1952). Brotli has no signature: by name.
python3 "$HERE/common/mkdtdesc.py" "$OUT/Devs/DataTypes/GZip" GZip gzip data gzip "#?" \
    '$VER: GZip 47.1 (23.10.2026)' 0x1f 0x8b 0x08
python3 "$HERE/common/mkdtdesc.py" "$OUT/Devs/DataTypes/Brotli" Brotli brotli data brot "#?.br" \
    '$VER: Brotli 47.1 (23.10.2026)'
echo "Devs/DataTypes/GZip, Brotli: $(wc -c < "$OUT/Devs/DataTypes/GZip"), $(wc -c < "$OUT/Devs/DataTypes/Brotli") bytes"
