#!/bin/sh
# The service rung, pixel for pixel (tests/servicerungtest.c): what
# gfopenservice's media.decode/1 gives must be what the datatypes make on
# the 68k, byte for byte.
#  - webm.datatype 47.2: VOPEN, VINDEX and VFRAME in MD_PIX_WEBM (with the
#    next frame read ahead, as the datatype asks) against the datatype's own
#    reader, libvpx 1.17.0 with its patches and its dither, every frame in
#    turn and jumping about: tests/t.webm and, with ffmpeg, VP8 with hidden
#    frames, VP9, VP9 4:4:4 and VP9 from RGB.
#  - gfopenpng, gfopentga, gfopengif and webp.datatype on the services card:
#    NDECODE against pngdecode.c, tgadecode.c, gifdecode.c and libwebp 1.6.0
#    with its patches, on the pictures make-tga.py and make-gif.py make and,
#    with Pillow, PNGs of every colour type and lossy, lossless, alpha and
#    animated WebPs.
# With QEMU_M68K set, the datatypes' code is also built for the 68040
# (m68k-linux-gnu-gcc) and run under qemu-m68k: its answers must be the
# same as this computer's (libvpx for the 68040 takes some minutes to build).
# MIT, Copyright (c) 2026 AmiCradle Team.
#
#   SERVICE      the gfopenservice checkout (default: beside this repository),
#                whose host/libmediadecode.so is the service (its build.sh,
#                with this checkout as ODT for NDECODE); or
#   MEDIADECODE  that library itself. Without one, only the 68k check runs.
#   TARBALLS     the upstream tarballs (as Datatypes/build.sh)
#   QEMU_M68K    qemu-m68k, to run the datatypes' code as 68k code too
#   CC           the host compiler (default cc)
#
# usage: sh tests/test-service-rung.sh [OUTDIR]       (default work/service-rung-test)
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/work/service-rung-test"}
CC=${CC:-cc}
if [ -z "${TARBALLS:-}" ]; then
    if [ -d "$HERE/tarballs" ]; then TARBALLS="$HERE/tarballs"; else TARBALLS="$HERE/../tarballs"; fi
fi
SERVICE=${SERVICE:-"$HERE/../../gfopenservice"}
LIB=${MEDIADECODE:-"$SERVICE/host/libmediadecode.so"}
mkdir -p "$OUT"
OUT=$(CDPATH= cd -- "$OUT" && pwd)

unpack() {   # NAME TARBALL SHA256: the tarball, checked, with this repository's patches for it
    if [ ! -d "$OUT/$1" ]; then
        t="$TARBALLS/$2"
        [ -f "$t" ] || sh "$HERE/../tools/fetch-tarball.sh" "$2" "$TARBALLS" || exit 2
        echo "$3  $t" | sha256sum -c - >/dev/null || { echo "SHA-256 mismatch: $t"; exit 2; }
        mkdir -p "$OUT/$1.tmp"
        tar xf "$t" -C "$OUT/$1.tmp"
        for p in "$HERE"/patches/"$1"-*.patch; do
            (cd "$OUT/$1.tmp/$1" && patch -p1 -s < "$p")
        done
        mv "$OUT/$1.tmp/$1" "$OUT/$1"
        rm -rf "$OUT/$1.tmp"
    fi
}
unpack libwebp-1.6.0 libwebp-1.6.0.tar.gz e4ab7009bf0629fd11982d4c2aa83964cf244cffba7347ecd39019a9e38c4564
unpack libvpx-1.17.0 libvpx-1.17.0.tar.gz 1020f184046187baa2985dbde38e0691f49c44088bca7a1842b0236c6081dc0a
W="$OUT/libwebp-1.6.0"
V="$OUT/libvpx-1.17.0"

# libwebp's decoder and demuxer, plain C only (as on the 68k: an empty
# config.h with HAVE_CONFIG_H leaves out the SSE2 and SSE4.1 code).
webp() {   # CC CFLAGS DIR
    if [ ! -f "$3/libwebp.a" ]; then
        mkdir -p "$3/src/webp"
        : > "$3/src/webp/config.h"
        for f in dec/alpha_dec dec/buffer_dec dec/frame_dec dec/idec_dec dec/io_dec dec/quant_dec dec/tree_dec \
                 dec/vp8_dec dec/vp8l_dec dec/webp_dec demux/demux dsp/alpha_processing dsp/cpu dsp/dec \
                 dsp/dec_clip_tables dsp/filters dsp/lossless dsp/rescaler dsp/upsampling dsp/yuv \
                 utils/bit_reader_utils utils/color_cache_utils utils/filters_utils utils/huffman_utils utils/palette \
                 utils/quant_levels_dec_utils utils/random_utils utils/rescaler_utils utils/thread_utils utils/utils; do
            # shellcheck disable=SC2086
            $1 $2 -DHAVE_CONFIG_H -I"$3" -I"$W" -I"$W/src" -c "$W/src/$f.c" -o "$3/$(echo "$f" | tr / _).o"
        done
        "${1%gcc}ar" rcs "$3/libwebp.a" "$3"/*.o 2>/dev/null || ar rcs "$3/libwebp.a" "$3"/*.o
    fi
}
# libvpx's VP8 and VP9 decoders in plain C, configured as Datatypes/build.sh does.
vpx() {   # CROSS CFLAGS DIR
    if [ ! -f "$3/libvpx.a" ]; then
        mkdir -p "$3"
        (cd "$3" && CROSS="$1" CFLAGS="$2" "$V/configure" --target=generic-gnu --disable-vp8-encoder \
            --disable-vp9-encoder --enable-vp8-decoder --enable-vp9-decoder --disable-examples --disable-tools \
            --disable-docs --disable-unit-tests --disable-multithread --disable-runtime-cpu-detect \
            --disable-install-docs --disable-install-bins --disable-webm-io --disable-libyuv --enable-static \
            --disable-shared --disable-postproc --disable-vp9-postproc --disable-internal-stats \
            --size-limit=8192x8192 > configure.log 2>&1 && make -j"${JOBS:-2}" libvpx.a > build.log 2>&1) \
            || { echo "libvpx build failed (see $3)"; exit 1; }
    fi
}
webp "$CC" "-O2" "$OUT/webp-host"
vpx "" "-O2" "$OUT/vpx-host"
SRC="$HERE/tests/servicerungtest.c $HERE/webm/webm_demux.c $HERE/png/pngdecode.c $HERE/tga/tgadecode.c $HERE/gif/gifdecode.c"
INC="-I$V -I$HERE/tests/host -I$HERE/webm -I$HERE/png -I$HERE/tga -I$HERE/gif -I$W/src -I$W"
# shellcheck disable=SC2086
$CC -O2 -Wall -Wextra -DREF -I"$OUT/vpx-host" $INC -o "$OUT/ref" $SRC "$OUT/webp-host/libwebp.a" "$OUT/vpx-host/libvpx.a" -lm
# shellcheck disable=SC2086
$CC -O2 -Wall -Wextra -DSVC -I"$HERE/webm" -o "$OUT/svc" "$HERE/tests/servicerungtest.c" "$HERE/webm/webm_demux.c" -ldl
if [ -n "${QEMU_M68K:-}" ]; then
    webp m68k-linux-gnu-gcc "-m68040 -O2 -DWORDS_BIGENDIAN" "$OUT/webp-m68k"
    vpx m68k-linux-gnu- "-m68040 -O2" "$OUT/vpx-m68k"
    # shellcheck disable=SC2086
    m68k-linux-gnu-gcc -m68040 -O2 -static -Wall -DREF -DWORDS_BIGENDIAN -I"$OUT/vpx-m68k" $INC -o "$OUT/ref-m68k" $SRC \
        "$OUT/webp-m68k/libwebp.a" "$OUT/vpx-m68k/libvpx.a" -lm
fi

# The pictures and the clips.
M="$OUT/media"
mkdir -p "$M/png" "$M/webp" "$M/webm"
python3 "$HERE/tests/make-tga.py" "$M/tga" > /dev/null
python3 "$HERE/tests/make-gif.py" "$M/gif" > /dev/null
if python3 -c "import PIL" 2>/dev/null; then
    python3 - "$M" <<'PY'
import os, sys
from PIL import Image
out = sys.argv[1]
def grad(w, h):
    im = Image.new('RGBA', (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = ((x * 255) // max(1, w - 1), (y * 255) // max(1, h - 1), ((x ^ y) * 7) & 255, (x * 3 + y) & 255)
    return im
g = grad(173, 91)
for mode in ('RGB', 'RGBA', 'L', 'LA', 'P', '1'):
    g.convert(mode).save(os.path.join(out, 'png', f'grad-{mode}.png'))
g.convert('L').convert('I').point(lambda v: v * 257).convert('I;16').save(os.path.join(out, 'png', 'grad-I16.png'))
grad(64, 48).convert('P').save(os.path.join(out, 'png', 'trns-P.png'), transparency=3)
big = grad(1024, 768)
big.convert('RGB').save(os.path.join(out, 'png', 'big-RGB.png'))
big.save(os.path.join(out, 'png', 'big-RGBA.png'), compress_level=1)
s = grad(320, 240)
for q in (30, 90):
    s.convert('RGB').save(os.path.join(out, 'webp', f'lossy-q{q}.webp'), quality=q)
s.save(os.path.join(out, 'webp', 'lossy-alpha.webp'), quality=80)
s.convert('RGB').save(os.path.join(out, 'webp', 'lossless.webp'), lossless=True)
s.save(os.path.join(out, 'webp', 'lossless-alpha.webp'), lossless=True)
big.convert('RGB').save(os.path.join(out, 'webp', 'big-lossy.webp'), quality=80)
frames = [grad(120, 90).rotate(a) for a in (0, 30, 60)]
frames[0].save(os.path.join(out, 'webp', 'anim.webp'), save_all=True, append_images=frames[1:], duration=100, loop=0)
PY
else
    echo "no Pillow: no PNG or WebP pictures"
fi
set -- "$HERE/tests/t.webm"
if command -v ffmpeg >/dev/null 2>&1; then
    src="testsrc2=s=176x144:r=25:d=2"
    ffmpeg -v error -y -f lavfi -i "$src" -c:v libvpx -b:v 300k -auto-alt-ref 1 -lag-in-frames 16 -pass 1 \
        -passlogfile "$M/webm/p" -f null /dev/null
    ffmpeg -v error -y -f lavfi -i "$src" -c:v libvpx -b:v 300k -auto-alt-ref 1 -lag-in-frames 16 -pass 2 \
        -passlogfile "$M/webm/p" "$M/webm/vp8-hidden.webm"
    ffmpeg -v error -y -f lavfi -i "testsrc2=s=160x120:r=25:d=2" -c:v libvpx-vp9 -b:v 200k -g 20 "$M/webm/vp9.webm"
    ffmpeg -v error -y -f lavfi -i "testsrc2=s=200x150:r=25:d=2" -pix_fmt yuv444p -c:v libvpx-vp9 -profile:v 1 \
        -b:v 300k "$M/webm/vp9-444.webm"
    ffmpeg -v error -y -f lavfi -i "testsrc2=s=200x150:r=25:d=2" -pix_fmt gbrp -c:v libvpx-vp9 -b:v 300k \
        "$M/webm/vp9-rgb.webm"
    set -- "$@" "$M/webm/vp8-hidden.webm" "$M/webm/vp9.webm" "$M/webm/vp9-444.webm" "$M/webm/vp9-rgb.webm"
else
    echo "no ffmpeg: tests/t.webm only"
fi

# Every answer, one line each: run PROGRAM (with qemu-m68k in front, perhaps).
run() {
    for clip in $CLIPS; do
        for how in play jump; do $1 webm "$clip" "$how" 2>/dev/null | sed "s|$M/||; s|$HERE/||"; done
    done
    (cd "$M"
     $1 pic png 0 0 png/*.png 2>/dev/null || true
     $1 pic tga 0 0 tga/*.tga
     $1 pic gif 1 0 gif/*.gif
     for f in 0 1 2 5; do $1 pic gif 0 "$f" gif/*.gif; done
     $1 pic webp 0 0 webp/*.webp 2>/dev/null || true)
}
CLIPS="$*"
run "$OUT/ref" > "$OUT/ref.txt"
echo "the datatypes' code: $(grep -c '^frame' "$OUT/ref.txt") frames, $(grep -vc '^frame\|frames,' "$OUT/ref.txt") pictures"
fail=0
if [ -f "$LIB" ]; then
    MEDIADECODE=$LIB run "$OUT/svc" > "$OUT/svc.txt"
    if cmp -s "$OUT/ref.txt" "$OUT/svc.txt"; then
        echo "media.decode/1 ($LIB): every frame and picture the same, byte for byte"
    else
        echo "media.decode/1 differs from the datatypes' code:"
        diff "$OUT/ref.txt" "$OUT/svc.txt" | head -20
        fail=1
    fi
else
    echo "no media.decode/1 library ($LIB; build gfopenservice's host side or set MEDIADECODE): service side skipped"
fi
if [ -n "${QEMU_M68K:-}" ]; then
    run "$QEMU_M68K $OUT/ref-m68k" > "$OUT/m68k.txt"
    if cmp -s "$OUT/ref.txt" "$OUT/m68k.txt"; then
        echo "68040 under qemu-m68k: the same frames and pictures"
    else
        echo "68040 under qemu-m68k differs:"
        diff "$OUT/ref.txt" "$OUT/m68k.txt" | head -20
        fail=1
    fi
fi
exit $fail
