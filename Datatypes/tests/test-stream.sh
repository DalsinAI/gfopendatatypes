#!/bin/sh
# stream.datatype's core with gzip.datatype's and brotli.datatype's decoders
# on x86 or ARM64 cores (stream/sdtcore.c, gzip/gzcore.c, brotli/brdecode.c):
# makes the test files (tests/make-stream.py) and decodes every one through
# tests/streamtest.c four ways: whole (NewDTObject's way), fed and taken in
# big pieces, fed and taken a byte at a time, and pulled through a hook three
# bytes at a time. Each must give the expected bytes, or fail where the file
# is damaged. Then files cut short and with bytes changed, under
# AddressSanitizer and UBSan: nothing read or written outside the buffers.
# Last, the cores are compiled as C99 with -Wall -Wextra -Werror at -Os,
# and must hold no writable data (they run inside a library).
# MIT, Copyright (c) 2026 AmiCradle Team.
# SPDX-License-Identifier: MIT
#
#   CC                 the host compiler (default cc); CXX for GFOpenZlib's core (default c++)
#   SAN                sanitizer flags (default -fsanitize=address,undefined; SAN= for none)
#   AMIGACHROME_GUEST  checkout of amigachrome-guest, for GFOpenZlib's core (z.library's
#                      inflate); default amigachrome-guest or guest beside this
#                      repository. Without it the host's zlib stands in, and the
#                      test says so.
#   BROTLI_RFC         RFC 7932's text (rfc7932.txt) or its dictionary (122,784 bytes);
#                      default tarballs/rfc7932.txt, fetched from rfc-editor.org when
#                      missing. Without it the Brotli part is skipped.
#
# usage: sh tests/test-stream.sh [OUTDIR]       (default work/stream-test)
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/work/stream-test"}
CC=${CC:-cc}
CXX=${CXX:-c++}
SAN=${SAN--fsanitize=address,undefined -fno-sanitize-recover=all}
mkdir -p "$OUT"

# z.library's inflate: GFOpenZlib's core, or the host's zlib in its place
G=${AMIGACHROME_GUEST:-}
for g in "$HERE/../../amigachrome-guest" "$HERE/../../guest"; do
    [ -n "$G" ] || { [ -f "$g/libraries/openzlib/zlib_core.cpp" ] && G=$g; } || true
done
if [ -n "$G" ] && [ -f "$G/libraries/openzlib/zlib_core.cpp" ]; then
    OZ="$G/libraries/openzlib"
    # shellcheck disable=SC2086
    $CXX -std=gnu++23 -fno-exceptions -fno-rtti -O1 -g $SAN -c "$OZ/zlib_core.cpp" -o "$OUT/zlib_core.o"
    ZL="-I$OZ $OUT/zlib_core.o -lstdc++"
    echo "inflate: GFOpenZlib's core (z.library), $OZ"
else
    ZL="-DZLHOST_SYSTEM -lz"
    echo "inflate: the host's zlib, standing in for z.library (no amigachrome-guest checkout)"
fi

# Brotli's dictionary, from the RFC
SRC=${BROTLI_RFC:-}
if [ -z "$SRC" ]; then
    for t in "$HERE/tarballs" "$HERE/../tarballs"; do
        [ -f "$t/rfc7932.txt" ] && SRC="$t/rfc7932.txt" && break
    done
fi
if [ -z "$SRC" ] && curl -fsSL --retry 2 -o "$OUT/rfc7932.txt" https://www.rfc-editor.org/rfc/rfc7932.txt 2>/dev/null; then
    SRC="$OUT/rfc7932.txt"
fi
BR=
if [ -n "$SRC" ]; then
    python3 "$HERE/brotli/mkbrdict.py" "$SRC" "$OUT/brdict.c" "$OUT/dictionary.bin"
    BR="$HERE/brotli/brdecode.c $OUT/brdict.c"
    export BROTLI_DICT="$OUT/dictionary.bin"
else
    echo "brotli: skipped (no rfc7932.txt; set BROTLI_RFC)"
fi

# shellcheck disable=SC2086
$CC -std=c99 -O1 -g -Wall -Wextra -Werror $SAN -I"$HERE/tests/host" -I"$HERE/common" -I"$HERE/stream" \
    -I"$HERE/gzip" -I"$HERE/brotli" ${BR:+-DWITH_BROTLI} -o "$OUT/streamtest" \
    "$HERE/tests/streamtest.c" "$HERE/tests/zlhost.c" "$HERE/stream/sdtcore.c" "$HERE/gzip/gzcore.c" $BR $ZL

python3 "$HERE/tests/make-stream.py" "$OUT" "$HERE/tests/brotli-vectors.txt"
cd "$OUT"
pass=0 fail=0 skip=0
while read -r name codec wrapper expect; do
    if [ "$codec" = brotli ] && [ -z "$BR" ]; then skip=$((skip + 1)); continue; fi
    size=$(wc -c < "$name")
    for how in "-a" "-f 65536 -t 65536" "-f 1 -t 1" "-p -f 3 -t 777"; do
        # a byte at a time over a megabyte only for the smaller files
        if [ "$how" = "-f 1 -t 1" ] && [ "$size" -gt 300000 ]; then how="-f 1 -t 4096"; fi
        # shellcheck disable=SC2086
        got=$(./streamtest "$codec" -w "$wrapper" $how "$name" "$name.out" || true)
        if [ "$expect" = error ]; then
            case "$got" in error*) pass=$((pass + 1)) ;; *) echo "FAIL $name ($how): $got, expected an error"; fail=$((fail + 1)) ;; esac
        elif [ "${got%% *}" = ok ] && cmp -s "$name.out" "$expect"; then
            pass=$((pass + 1))
        else
            echo "FAIL $name ($how): $got"
            fail=$((fail + 1))
        fi
    done
done < cases.txt
echo "streamtest: $pass decodes as expected, $fail not, $skip skipped (no dictionary)"
[ "$fail" = 0 ] || exit 1

for f in text-6.gz text-w15.zz text-w15.deflate fields.gz members.gz; do
    printf '%s: ' "$f"; ./streamtest gzip -cuts "$f"
done
if [ -n "$BR" ]; then
    for f in dict-9.br literals-w16.br html-q11.br utf8-q5.br binary-q2.br readme-q11.br; do
        [ -f "$f" ] || continue
        printf '%s: ' "$f"; ./streamtest brotli -cuts "$f"
    done
fi

# The cores as the library builds them: C99, warnings as errors, no writable data.
for f in "$HERE/stream/sdtcore.c" "$HERE/gzip/gzcore.c" ${BR:+"$HERE/brotli/brdecode.c" "$OUT/brdict.c"}; do
    o="$OUT/$(basename "$f" .c).o"
    $CC -std=c99 -Os -Wall -Wextra -Werror -pedantic -I"$HERE/tests/host" -I"$HERE/common" -I"$HERE/stream" \
        -I"$HERE/gzip" -I"$HERE/brotli" -c "$f" -o "$o"
    data=$(size -A "$o" | awk '$1 ~ /^\.(data|bss)/ && $2 > 0 {print $1}' | tr '\n' ' ')
    [ -z "$data" ] || { echo "FAIL $(basename "$f"): writable data ($data)"; exit 1; }
done
echo "cores: C99 -Wall -Wextra -Werror -pedantic, no writable data"
