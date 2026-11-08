#!/bin/sh
# libwebp and libvpx with achelpers/ac_string.h forced in, as build.sh
# compiles them with AC_HELPERS=1, against the same sources without it, as
# 68040 code under qemu-m68k (an m68k without AC090, where the helpers'
# own 68k code runs): webp.datatype's encoder (tests/webpenctest.c) must
# write the same files, libwebp must decode the same pictures
# (tests/webpraw.c, on the encoder's files and, with Pillow, lossy ones),
# and libvpx must decode the same frames (tests/webmprobe.c, on
# tests/t.webm and, with ffmpeg, a VP8 and a VP9 clip). Both libraries'
# objects must call the helpers, or the check proves nothing.
# MIT, Copyright (c) 2026 AmiCradle Team.
#
#   AMIGACHROME_GUEST  as for build.sh (the helpers' sources, at the pinned commit)
#   M68K_CC, M68K_PREFIX, QEMU_M68K  the cross tools (default m68k-linux-gnu-gcc,
#                m68k-linux-gnu-, qemu-m68k)
#   TARBALLS     as for build.sh
#
# usage: sh tests/test-acstring.sh [OUTDIR]   (default work/acstring-test)
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/work/acstring-test"}
cc=${M68K_CC:-m68k-linux-gnu-gcc}
prefix=${M68K_PREFIX:-m68k-linux-gnu-}
qemu=${QEMU_M68K:-qemu-m68k}
command -v "$cc" >/dev/null || { echo "skipped: no $cc"; exit 0; }
command -v "$qemu" >/dev/null || { echo "skipped: no $qemu"; exit 0; }
sh "$HERE/../achelpers/achelpers.sh" --have || { echo "skipped: no amicradle-guest checkout with the pinned commit (AMIGACHROME_GUEST)"; exit 0; }
if [ -z "${TARBALLS:-}" ]; then
    if [ -d "$HERE/tarballs" ]; then TARBALLS="$HERE/tarballs"; else TARBALLS="$HERE/../tarballs"; fi
fi
get() {          # FILE SHA256: the tarball, fetched from SOURCES's URL when missing
    mkdir -p "$TARBALLS"
    [ -f "$TARBALLS/$1" ] || sh "$HERE/../tools/fetch-tarball.sh" "$1" "$TARBALLS"
    echo "$2  $TARBALLS/$1" | sha256sum -c - >/dev/null
}
get libwebp-1.6.0.tar.gz e4ab7009bf0629fd11982d4c2aa83964cf244cffba7347ecd39019a9e38c4564
get libvpx-1.17.0.tar.gz 1020f184046187baa2985dbde38e0691f49c44088bca7a1842b0236c6081dc0a
rm -rf "$OUT"
mkdir -p "$OUT/media"
CF="-O2 -m68040 -fomit-frame-pointer -fno-delete-null-pointer-checks -DNDEBUG"
CC=$cc AR=${prefix}ar CFLAGS="$CF" sh "$HERE/../achelpers/achelpers.sh" "$OUT/ach" > "$OUT/ach.log"
ACSTRING="-include $HERE/../achelpers/ac_string.h -I$OUT/ach/src"
fail=0
say() { echo "$1"; case "$1" in ok*) ;; *) fail=1 ;; esac; }

# libwebp, patched as build.sh patches it
tar -xzf "$TARBALLS/libwebp-1.6.0.tar.gz" -C "$OUT"
W="$OUT/libwebp-1.6.0"
(cd "$W" && for p in no-float demux-null-chunk encoder-no-float m68060; do
    patch -p1 -s < "$HERE/patches/libwebp-1.6.0-$p.patch"; done)
# libvpx, patched as build.sh patches it
tar -xzf "$TARBALLS/libvpx-1.17.0.tar.gz" -C "$OUT"
V="$OUT/libvpx-1.17.0"
(cd "$V" && patch -p1 -s < "$HERE/patches/libvpx-1.17.0-amiga-align.patch" \
    && patch -p1 -s < "$HERE/patches/libvpx-1.17.0-tx-size-null.patch")

for v in plain ac; do
    X=; [ $v = ac ] && X=$ACSTRING
    o="$OUT/$v"; mkdir -p "$o/webp" "$o/vpx"
    for f in src/dec/alpha_dec.c src/dec/buffer_dec.c src/dec/frame_dec.c src/dec/idec_dec.c src/dec/io_dec.c \
        src/dec/quant_dec.c src/dec/tree_dec.c src/dec/vp8_dec.c src/dec/vp8l_dec.c src/dec/webp_dec.c \
        src/demux/demux.c src/dsp/alpha_processing.c src/dsp/cpu.c src/dsp/dec.c src/dsp/dec_clip_tables.c \
        src/dsp/filters.c src/dsp/lossless.c src/dsp/rescaler.c src/dsp/upsampling.c src/dsp/yuv.c \
        src/utils/bit_reader_utils.c src/utils/color_cache_utils.c src/utils/filters_utils.c \
        src/utils/huffman_utils.c src/utils/palette.c src/utils/quant_levels_dec_utils.c \
        src/utils/random_utils.c src/utils/rescaler_utils.c src/utils/thread_utils.c src/utils/utils.c \
        src/enc/vp8l_enc.c src/enc/histogram_enc.c src/enc/backward_references_enc.c \
        src/enc/backward_references_cost_enc.c src/enc/predictor_enc.c src/enc/near_lossless_enc.c \
        src/dsp/lossless_enc.c src/utils/bit_writer_utils.c src/utils/huffman_encode_utils.c; do
        # shellcheck disable=SC2086
        (cd "$W" && $cc $CF -DWORDS_BIGENDIAN $X -I. -Isrc -c $f -o "$o/webp/$(echo $f | tr / _ | sed 's/\.c$/.o/')")
    done
    "${prefix}ar" rcs "$o/libwebp.a" "$o"/webp/*.o
    # shellcheck disable=SC2086
    $cc $CF -static -I"$HERE/tests/host" -I"$HERE/webp" -I"$W" -o "$o/webpenctest" \
        "$HERE/tests/webpenctest.c" "$HERE/webp/webpencode.c" "$o/libwebp.a" -L"$OUT/ach" -lachelpers
    # shellcheck disable=SC2086
    $cc $CF -static -I"$W/src" -o "$o/webpraw" "$HERE/tests/webpraw.c" "$o/libwebp.a" -L"$OUT/ach" -lachelpers
    (cd "$o/vpx" && CROSS=$prefix CFLAGS="$CF $X" "$V/configure" --target=generic-gnu \
        --disable-vp8-encoder --disable-vp9-encoder --enable-vp8-decoder --enable-vp9-decoder \
        --disable-examples --disable-tools --disable-docs --disable-unit-tests --disable-multithread \
        --disable-runtime-cpu-detect --disable-install-docs --disable-install-bins --disable-webm-io \
        --disable-libyuv --enable-static --disable-shared --disable-postproc --disable-vp9-postproc \
        --disable-internal-stats --disable-pic --size-limit=8192x8192 > configure.log 2>&1 \
        && make -j"${JOBS:-4}" libvpx.a > build.log 2>&1) || { echo "FAIL libvpx ($v): see $o/vpx"; exit 1; }
    # shellcheck disable=SC2086
    $cc $CF -static -I"$V" -I"$o/vpx" -I"$HERE/webm" -o "$o/webmprobe" "$HERE/tests/webmprobe.c" \
        "$HERE/webm/webm_demux.c" "$o/vpx/libvpx.a" -L"$OUT/ach" -lachelpers
done

n=$("${prefix}nm" "$OUT"/ac/webp/*.o | grep -cE ' U ac_mem(cpy|move|set)$' || true)
m=$("${prefix}nm" "$OUT"/ac/vpx/libvpx.a | grep -cE ' U ac_mem(cpy|move|set)$' || true)
if [ "$n" -gt 0 ] && [ "$m" -gt 0 ]; then say "ok   ac_string.h sends libwebp's ($n) and libvpx's ($m) copies and fills to the helpers"
else say "FAIL ac_string.h: libwebp $n, libvpx $m calls of the helpers"; fi

# the encoder's files, and libwebp reading them (and Pillow's) back
for v in plain ac; do
    mkdir -p "$OUT/$v/files"
    "$qemu" -cpu m68040 "$OUT/$v/webpenctest" "$OUT/$v/files" > "$OUT/$v/webpenctest.txt" || true
done
if diff -r "$OUT/plain/files" "$OUT/ac/files" >/dev/null && grep -q ', 0 failed' "$OUT/ac/webpenctest.txt"; then
    say "ok   webp.datatype's encoder: $(ls "$OUT/ac/files" | grep -c '\.webp$') files the same with the helpers ($(cat "$OUT/ac/webpenctest.txt"))"
else say "FAIL webp.datatype's encoder differs with the helpers"; fi
python3 -I - "$OUT/media" <<'EOF' || true
import sys
try:
    from PIL import Image
except ImportError:
    print("Pillow: not installed, no lossy WebP files"); sys.exit(0)
out = sys.argv[1]
im = Image.new('RGB', (200, 131)); px = im.load()
for y in range(131):
    for x in range(200):
        px[x, y] = ((x * 255) // 199, (y * 255) // 130, ((x + y) * 7) & 255)
im.save(out + '/lossy.webp', 'WEBP', quality=80)
im.convert('RGBA').save(out + '/lossy-alpha.webp', 'WEBP', quality=70)
EOF
for v in plain ac; do
    (cd "$OUT/plain/files" && "$qemu" -cpu m68040 "$OUT/$v/webpraw" ./*.webp "$OUT"/media/*.webp) > "$OUT/$v/webpraw.txt" 2>&1 || true
done
if cmp -s "$OUT/plain/webpraw.txt" "$OUT/ac/webpraw.txt" && ! grep -q 'FAIL\|BAD' "$OUT/ac/webpraw.txt"; then
    say "ok   libwebp's decoder: $(wc -l < "$OUT/ac/webpraw.txt") pictures the same with the helpers"
else say "FAIL libwebp's decoder differs with the helpers"; fi

# libvpx's frames
set -- "$HERE/tests/t.webm"
if command -v ffmpeg >/dev/null 2>&1; then
    ffmpeg -v error -y -f lavfi -i testsrc=size=320x240:rate=25:duration=2 -c:v libvpx -b:v 300k "$OUT/media/vp8.webm"
    ffmpeg -v error -y -f lavfi -i testsrc=size=160x120:rate=25:duration=2 -c:v libvpx-vp9 -b:v 100k "$OUT/media/vp9.webm"
    set -- "$@" "$OUT/media/vp8.webm" "$OUT/media/vp9.webm"
else
    echo "no ffmpeg: tests/t.webm only"
fi
for f in "$@"; do
    a=$("$qemu" -cpu m68040 "$OUT/plain/webmprobe" "$f" 2>&1 || true)
    b=$("$qemu" -cpu m68040 "$OUT/ac/webmprobe" "$f" 2>&1 || true)
    if [ "$a" = "$b" ] && echo "$b" | grep -q WEBM_DONE && ! echo "$b" | grep -q FAIL; then
        say "ok   libvpx: $(basename "$f"), $(echo "$b" | grep -c '^frame') frames the same with the helpers"
    else say "FAIL libvpx: $(basename "$f") differs with the helpers"; fi
done
exit $fail
