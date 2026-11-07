#!/bin/sh
# webp.datatype's webp/webpcore.c on x86 or ARM64 cores and, with an m68k
# cross GCC and qemu-m68k, as 68040 code (tests/webpcoretest.c), with
# libwebp 1.6.0 patched as for the Amiga:
# - dt_init()'s webp_core_init() sets up all of libwebp's tables: libwebp's
#   .data and .bss, linked as sections of their own (webp_data, webp_bss),
#   are compared before and after decoding every kind of WebP and
#   encoding: nothing may change. The init dt_init() did before 47.4 must
#   be caught writing them. The WebP files are the test's own
#   ("webpcoretest media": lossless ones from webp.datatype's encoder,
#   lossy ones from tests/webpcore-media.h), so a clean checkout has them.
# - an opaque animated WebP whose first frame doesn't cover the canvas
#   comes on the ANIM background colour; with alpha, or a frame covering
#   the canvas, as the frame alone.
# MIT, Copyright (c) 2026 AmiCradle Team.
#
#   CC        the host compiler (default cc)
#   M68K_CC, QEMU_M68K  the cross tools (default m68k-linux-gnu-gcc, qemu-m68k);
#             QEMU_M68K= skips the m68k half
#   TARBALLS  as for test-webpenc.sh
#
# usage: sh tests/test-webpcore.sh [OUTDIR]   (default work/webpcore-test)
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/work/webpcore-test"}
CC=${CC:-cc}
m68kcc=${M68K_CC:-m68k-linux-gnu-gcc}
qemu=${QEMU_M68K-qemu-m68k}
if [ -z "${TARBALLS:-}" ]; then
    if [ -d "$HERE/tarballs" ]; then TARBALLS="$HERE/tarballs"; else TARBALLS="$HERE/../tarballs"; fi
fi
T="$TARBALLS/libwebp-1.6.0.tar.gz"
mkdir -p "$TARBALLS"
[ -f "$T" ] || sh "$HERE/../tools/fetch-tarball.sh" libwebp-1.6.0.tar.gz "$TARBALLS"
echo "e4ab7009bf0629fd11982d4c2aa83964cf244cffba7347ecd39019a9e38c4564  $T" | sha256sum -c - >/dev/null
rm -rf "$OUT"
mkdir -p "$OUT"
tar -xzf "$T" -C "$OUT"
W="$OUT/libwebp-1.6.0"
(cd "$W" && for p in no-float demux-null-chunk encoder-no-float m68060; do
    patch -p1 -s < "$HERE/patches/libwebp-1.6.0-$p.patch"; done)
fail=0
say() { echo "$1"; case "$1" in ok*) ;; *) fail=1 ;; esac; }

# build NAME CC LD OBJCOPY FLAGS...: OUT/NAME/webpcoretest, libwebp linked
# as one object whose .data and .bss are renamed webp_data and webp_bss
build() {
    name=$1 c=$2 ld=$3 oc=$4; shift 4
    o="$OUT/$name"; mkdir -p "$o/obj"
    # shellcheck disable=SC2046
    for f in $(cd "$W" && ls src/dec/*.c src/demux/demux.c src/dsp/*.c src/utils/*.c src/enc/vp8l_enc.c \
            src/enc/histogram_enc.c src/enc/backward_references_enc.c src/enc/backward_references_cost_enc.c \
            src/enc/predictor_enc.c src/enc/near_lossless_enc.c); do
        (cd "$W" && $c "$@" -I. -Isrc -c "$f" -o "$o/obj/$(echo "$f" | tr / _ | sed 's/\.c$/.o/')")
    done
    $c "$@" -I"$HERE/tests/host" -I"$HERE/webp" -I"$W" -c "$HERE/webp/webpcore.c" -o "$o/obj/webpcore.o"
    $c "$@" -I"$HERE/tests/host" -I"$HERE/webp" -I"$W" -c "$HERE/webp/webpencode.c" -o "$o/obj/webpencode.o"
    "$ld" -r -o "$o/libwebp-r.o" "$o"/obj/src_*.o
    "$oc" --rename-section .data=webp_data --rename-section .bss=webp_bss "$o/libwebp-r.o"
    $c "$@" -Wall -Wextra -I"$HERE/tests/host" -I"$HERE/webp" -I"$W" -o "$o/webpcoretest" \
        "$HERE/tests/webpcoretest.c" "$o/obj/webpcore.o" "$o/obj/webpencode.o" "$o/libwebp-r.o"
}

run() {          # NAME RUNNER...
    name=$1; shift
    m="$OUT/$name/media"; mkdir -p "$m"
    n=$("$@" "$OUT/$name/webpcoretest" media "$m" || true)
    set -- "$@" "$OUT/$name/webpcoretest"
    if [ "$n" = 7 ] && [ "$(ls "$m"/*.webp 2>/dev/null | wc -l)" -eq 7 ]; then
        say "ok   ($name) the test's 7 WebP files made (lossy, lossless, alpha, animated)"
    else
        say "FAIL ($name) the test's WebP files weren't made (${n:-none} of 7)"
        return
    fi
    r=0; "$@" init old "$m"/*.webp > "$OUT/$name/old.txt" 2>&1 || r=$?
    sed 's/^/     /' "$OUT/$name/old.txt"
    if [ $r -eq 3 ]; then say "ok   ($name) the test sees decodes write libwebp's tables after the old dt_init()"
    else say "FAIL ($name) the old dt_init() should have been caught (exit $r)"; fi
    r=0; "$@" init "$m"/*.webp > "$OUT/$name/new.txt" 2>&1 || r=$?
    sed 's/^/     /' "$OUT/$name/new.txt"
    if [ $r -eq 0 ]; then say "ok   ($name) after webp_core_init() nothing writes libwebp's data"
    else say "FAIL ($name) webp_core_init() leaves tables to the first decode (exit $r)"; fi
    r=0; "$@" anim > "$OUT/$name/anim.txt" 2>&1 || r=$?
    sed 's/^/     /' "$OUT/$name/anim.txt"
    if [ $r -eq 0 ]; then say "ok   ($name) an animation's first frame on its background colour"
    else say "FAIL ($name) an animation's first frame"; fi
}

build host "$CC" ld objcopy -O1 -g -fno-pie -no-pie
run host
if [ -z "$qemu" ] || ! command -v "$qemu" >/dev/null || ! command -v "$m68kcc" >/dev/null; then
    echo "m68k half skipped (no $m68kcc or qemu-m68k)"
    exit $fail
fi
p=${m68kcc%gcc}
build m68k "$m68kcc" "${p}ld" "${p}objcopy" -O2 -m68040 -fomit-frame-pointer -fno-delete-null-pointer-checks -DWORDS_BIGENDIAN -static
run m68k "$qemu" -cpu m68040
exit $fail
