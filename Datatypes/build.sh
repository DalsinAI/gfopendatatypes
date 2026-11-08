#!/bin/sh
# gfopenimage datatypes: gfopengif, webp, webm, gfopenpicture, gfopensound, gfopenmodule, gfopendoc, cdxl and gfopenvideo.datatype,
# and stream, gzip and brotli.datatype (stream/build.sh)
# for AmigaOS 3.x, built with the os32-gcc16 compiler (bebbo's amiga-gcc, GCC 16.2,
# libnix). The datatypes are built for the 68040, soft float (no FPU code).
# MIT, Copyright (c) 2026 AmiCradle Team. libwebp and libvpx keep their
# BSD licences, libxmp its MIT licence.
#
#   OS32_GCC16   compiler root holding prefix/ (default ~/AmiCradle/stoves/os32-gcc16)
#   PREFIX       where Classes/ and Devs/ go (default ./out)
#   TARBALLS     folder holding the upstream tarballs listed in ../SOURCES
#                (default ./tarballs, else ../tarballs); the script checks
#                their SHA-256
#   AC_HELPERS   1: big copies and fills in common/dtlib.c, libwebp and
#                libvpx go through AC090's native helpers (../achelpers/,
#                see ../README.md; libwebp and libvpx get ac_string.h);
#                0: as before; auto (default): 1 when the pinned
#                amicradle-guest commit is to hand, else 0, saying so
#   AMIGACHROME_GUEST  checkout of amicradle-guest holding the commit in
#                ../AMIGACHROME_GUEST_PINNED_COMMIT, for AC_HELPERS=1
#                (default amicradle-guest, else guest, beside this
#                repository)
#
# usage: ./build.sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
S=${OS32_GCC16:-"$HOME/AmiCradle/stoves/os32-gcc16"}
P=$S/prefix
OUT=${PREFIX:-"$HERE/out"}
# The tarballs sit in the repository's tarballs/ folder, or in one here.
if [ -z "${TARBALLS:-}" ]; then
    if [ -d "$HERE/tarballs" ]; then TARBALLS="$HERE/tarballs"; else TARBALLS="$HERE/../tarballs"; fi
fi
WORK="$HERE/work"
CC="$P/bin/m68k-amigaos-gcc"
AR="$P/bin/m68k-amigaos-ar"
# Address 0 is memory on an Amiga (exec's pointer is at 4): without
# -fno-delete-null-pointer-checks GCC drops null checks after a pointer is
# used and puts a trap (TRAP #7, Software Failure 80000027) on any path it
# proves reads or writes through a null pointer. Every 68k compile here
# (libvpx's too) takes it, so the code behaves as on older compilers.
NONULL=-fno-delete-null-pointer-checks
# No -m68881: soft float, no FPU code in the datatypes.
# Soft float: -m68040 links libnix's soft libm020, not libm881 (see
# ../build/m68k-soft-m68040.specs).
SOFT="-specs=$HERE/../build/m68k-soft-m68040.specs"
CFLAGS="-O2 -m68040 $SOFT -fomit-frame-pointer $NONULL -DNDEBUG -DWORDS_BIGENDIAN -Wall -Wno-pointer-sign"
mkdir -p "$OUT/Classes/DataTypes" "$OUT/Devs/DataTypes" "$WORK"

# AC090's native helpers for common/dtlib.c (realloc's copy, calloc's
# clearing), built from amicradle-guest at the pinned commit; ACLIB goes
# on each datatype's link.
AC_HELPERS=${AC_HELPERS:-auto}
case "$AC_HELPERS" in 0|1|auto) ;; *) echo "AC_HELPERS must be 0, 1 or auto, not $AC_HELPERS"; exit 2 ;; esac
if [ "$AC_HELPERS" = auto ]; then          # on when the pinned amicradle-guest commit is to hand
    if sh "$HERE/../achelpers/achelpers.sh" --have; then AC_HELPERS=1
    else AC_HELPERS=0; echo "AC090 native helpers: off (no amicradle-guest checkout with $(cat "$HERE/../AMIGACHROME_GUEST_PINNED_COMMIT" | cut -c1-12); set AMIGACHROME_GUEST)"; fi
fi
# ACSTRING goes on libwebp's and libvpx's compiles: ac_string.h, forced in,
# sends their memcpy, memmove and memset calls of 64 bytes and more to the
# helpers (on a 68k without AC090 the helpers' own 68k code runs).
ACFLAGS= ACLIB= ACSTRING=
if [ "$AC_HELPERS" = 1 ]; then
    CC="$CC" AR="$AR" CFLAGS="$CFLAGS" sh "$HERE/../achelpers/achelpers.sh" "$WORK/achelpers"
    ACFLAGS="-DOAI_AC_HELPERS -I$WORK/achelpers/src"
    ACLIB="$WORK/achelpers/libachelpers.a"
    ACSTRING="-include $HERE/../achelpers/ac_string.h -I$WORK/achelpers/src"
fi
echo "AC090 native helpers: AC_HELPERS=$AC_HELPERS"

# The descriptors (descriptors.sh, one desc_<datatype> for each) and the
# recognition code some of them carry (hooks/, DTCD chunks).
DTHOOKS="$WORK/hooks"
OS32_GCC16="$S" DTHOOKS="$DTHOOKS" sh "$HERE/hooks/build.sh"
. "$HERE/descriptors.sh"

unpack() {
    t="$TARBALLS/$2"
    [ -f "$t" ] || sh "$HERE/../tools/fetch-tarball.sh" "$2" "$TARBALLS" || exit 2   # fetched from SOURCES's URL, SHA-256 checked
    echo "$3  $t" | sha256sum -c - >/dev/null || { echo "SHA-256 mismatch: $t"; exit 2; }
    rm -rf "$WORK/$1"; mkdir -p "$WORK/$1"
    tar xf "$t" -C "$WORK/$1"
}

# compile DIR OBJDIR FILE...: compile C files into OBJDIR ($XFLAGS added)
compile() {
    dir=$1; obj=$2; shift 2
    rm -rf "$obj"; mkdir -p "$obj"
    for f in "$@"; do
        o="$obj/$(echo "$f" | tr '/' '_' | sed 's/\.c$//').o"
        (cd "$dir" && $CC $CFLAGS ${XFLAGS:-} -c "$f" -o "$o")
    done
}

# --- webp.datatype ------------------------------------------------------------
unpack libwebp libwebp-1.6.0.tar.gz e4ab7009bf0629fd11982d4c2aa83964cf244cffba7347ecd39019a9e38c4564
W="$WORK/libwebp/libwebp-1.6.0"
# No floating point in the decoder or the lossless encoder (see the
# patches), no null read in the demuxer's chunk iterator (see the patch), and
# no 64-bit multiplies or divides in the lossless encoder's busiest sums,
# which a 68060 traps (libwebp-1.6.0-m68060.patch, tests/test-webp060.sh).
(cd "$W" && patch -p1 -s < "$HERE/patches/libwebp-1.6.0-no-float.patch" \
    && patch -p1 -s < "$HERE/patches/libwebp-1.6.0-demux-null-chunk.patch" \
    && patch -p1 -s < "$HERE/patches/libwebp-1.6.0-encoder-no-float.patch" \
    && patch -p1 -s < "$HERE/patches/libwebp-1.6.0-m68060.patch")
# The decoder, the demuxer (animated and extended files) and the shared code.
# The SSE, NEON and MIPS files compile to nothing on 68k and are left out.
XFLAGS="-I$W -I$W/src $ACSTRING" compile "$W" "$WORK/obj-libwebp" \
    src/dec/alpha_dec.c src/dec/buffer_dec.c src/dec/frame_dec.c src/dec/idec_dec.c src/dec/io_dec.c \
    src/dec/quant_dec.c src/dec/tree_dec.c src/dec/vp8_dec.c src/dec/vp8l_dec.c src/dec/webp_dec.c \
    src/demux/demux.c \
    src/dsp/alpha_processing.c src/dsp/cpu.c src/dsp/dec.c src/dsp/dec_clip_tables.c src/dsp/filters.c \
    src/dsp/lossless.c src/dsp/rescaler.c src/dsp/upsampling.c src/dsp/yuv.c \
    src/utils/bit_reader_utils.c src/utils/color_cache_utils.c src/utils/filters_utils.c \
    src/utils/huffman_utils.c src/utils/palette.c src/utils/quant_levels_dec_utils.c \
    src/utils/random_utils.c src/utils/rescaler_utils.c src/utils/thread_utils.c src/utils/utils.c \
    src/enc/vp8l_enc.c src/enc/histogram_enc.c src/enc/backward_references_enc.c \
    src/enc/backward_references_cost_enc.c src/enc/predictor_enc.c src/enc/near_lossless_enc.c \
    src/dsp/lossless_enc.c src/utils/bit_writer_utils.c src/utils/huffman_encode_utils.c
# The lossless encoder above, for DTM_WRITE (webp/webpencode.c, which has the
# three picture calls it needs: libwebp's picture files would bring the lossy
# encoder). No floating point in any of it: the link map is checked below.
# dtstart.c is linked first, so the library's first code is a safe return.
# webpcore.c: libwebp's tables set up in dt_init(), and an animation's first
# frame on its background colour (tests/test-webpcore.sh).
XFLAGS="-I$HERE/common -I$HERE/include -I$HERE/webp -I$W -I$W/src $ACFLAGS" compile "$HERE" "$WORK/obj-webp" \
    common/dtstart.c common/dtlib.c webp/webpclass.c webp/webpencode.c webp/webpcore.c
$CC -nostartfiles -m68040 $SOFT -o "$OUT/Classes/DataTypes/webp.datatype" \
    "$WORK"/obj-webp/common_dtstart.o "$WORK"/obj-webp/common_dtlib.o "$WORK"/obj-webp/webp_webpclass.o \
    "$WORK"/obj-webp/webp_webpencode.o "$WORK"/obj-webp/webp_webpcore.o "$WORK"/obj-libwebp/*.o $ACLIB -lamiga \
    -Wl,-Map="$WORK/webp.datatype.map"
echo "webp.datatype: $(wc -c < "$OUT/Classes/DataTypes/webp.datatype") bytes"
# No soft-float helpers and no maths library linked in: they would open the ROM
# math libraries in the calling program's task (openmodule/DESIGN.md, section 2).
fp=$(grep -oE '\b(___?((add|sub|mul|div|neg|cmp|eq|ne|lt|le|gt|ge|unord)[sdx]f[23]|(fix|fixuns)[sdx]f[sd]i|float(un)?[sd]i[sdx]f|extendsfdf2|truncdfsf2)|__?(log|exp|pow|sqrt))\b' \
    "$WORK/webp.datatype.map" | sort -u | tr '\n' ' ')
[ -z "$fp" ] || { echo "webp.datatype: floating point linked in: $fp" >&2; exit 1; }
desc_webp "$OUT"

# --- webm.datatype ------------------------------------------------------------
unpack libvpx libvpx-1.17.0.tar.gz 1020f184046187baa2985dbde38e0691f49c44088bca7a1842b0236c6081dc0a
V="$WORK/libvpx/libvpx-1.17.0"
# Amiga objects align to at most 8 bytes, and VP9's transform size reader
# never reads through a null table (see the patches).
(cd "$V" && patch -p1 -s < "$HERE/patches/libvpx-1.17.0-amiga-align.patch" \
    && patch -p1 -s < "$HERE/patches/libvpx-1.17.0-tx-size-null.patch")
# The VP8 and VP9 decoders in plain C: no encoders, threads or tools.
# A fresh build folder: libvpx's make doesn't notice CFLAGS changing (AC_HELPERS).
rm -rf "$WORK/libvpx-build"
mkdir -p "$WORK/libvpx-build"
(cd "$WORK/libvpx-build" && CROSS="$P/bin/m68k-amigaos-" CFLAGS="-O2 -m68040 $SOFT -fomit-frame-pointer $NONULL $ACSTRING" \
    "$V/configure" --target=generic-gnu --disable-vp8-encoder --disable-vp9-encoder --enable-vp8-decoder \
    --enable-vp9-decoder --disable-examples --disable-tools --disable-docs --disable-unit-tests \
    --disable-multithread --disable-runtime-cpu-detect --disable-install-docs --disable-install-bins \
    --disable-webm-io --disable-libyuv --enable-static --disable-shared --disable-postproc \
    --disable-vp9-postproc --disable-internal-stats --disable-pic --size-limit=8192x8192 > configure.log 2>&1 \
    && make -j"${JOBS:-2}" libvpx.a > build.log 2>&1) || { echo "libvpx build failed (see $WORK/libvpx-build)"; exit 1; }
XFLAGS="-I$HERE/common -I$HERE/include -I$HERE/webm -I$V -I$WORK/libvpx-build $ACFLAGS" compile "$HERE" "$WORK/obj-webm" \
    common/dtstart.c common/dtlib.c common/dtstack.c common/dtservice.c webm/webm_demux.c webm/webmclass.c
$CC -nostartfiles -m68040 $SOFT -o "$OUT/Classes/DataTypes/webm.datatype" \
    "$WORK"/obj-webm/common_dtstart.o "$WORK"/obj-webm/common_dtlib.o "$WORK"/obj-webm/common_dtstack.o \
    "$WORK"/obj-webm/common_dtservice.o \
    "$WORK"/obj-webm/webm_webm_demux.o "$WORK"/obj-webm/webm_webmclass.o "$WORK/libvpx-build/libvpx.a" $ACLIB -lamiga \
    -Wl,-Map="$WORK/webm.datatype.map"
echo "webm.datatype: $(wc -c < "$OUT/Classes/DataTypes/webm.datatype") bytes"
desc_webm "$OUT"

# --- gfopenpicture.datatype -----------------------------------------------------
# AVIF, HEIC, JPEG XL, camera RAW, PSD, XCF, EXR, HDR, QOI, DDS and JPEG 2000,
# decoded by the media.decode/1 service (gfopenservice) on the services
# card or a paired Cradle; no codec on the 68k.
XFLAGS="-I$HERE/common -I$HERE/include $ACFLAGS" compile "$HERE" "$WORK/obj-openpicture" \
    common/dtstart.c common/dtlib.c common/dtservice.c common/dtencode.c common/encodemsg.c openpicture/pictureclass.c
$CC -nostartfiles -m68040 $SOFT -o "$OUT/Classes/DataTypes/gfopenpicture.datatype" \
    "$WORK"/obj-openpicture/common_dtstart.o "$WORK"/obj-openpicture/common_dtlib.o \
    "$WORK"/obj-openpicture/common_dtservice.o "$WORK"/obj-openpicture/common_dtencode.o \
    "$WORK"/obj-openpicture/common_encodemsg.o "$WORK"/obj-openpicture/openpicture_pictureclass.o $ACLIB -lamiga \
    -Wl,-Map="$WORK/gfopenpicture.datatype.map"
echo "gfopenpicture.datatype: $(wc -c < "$OUT/Classes/DataTypes/gfopenpicture.datatype") bytes"
desc_gfopenpicture "$OUT"

# --- gfopenpng.datatype ---------------------------------------------------------
# PNG on the Amiga itself, our own decoder (png/pngclass.c, png/build.sh).
OS32_GCC16="$S" PREFIX="$OUT" ACFLAGS="$ACFLAGS" ACLIB="$ACLIB" sh "$HERE/png/build.sh"

# --- gfopentga.datatype ---------------------------------------------------------
# Targa (TGA) on the Amiga itself, our own decoder (tga/tgaclass.c,
# tga/build.sh); tests/test-tga.sh checks the decoder on x86 or ARM64 cores.
OS32_GCC16="$S" PREFIX="$OUT" ACFLAGS="$ACFLAGS" ACLIB="$ACLIB" sh "$HERE/tga/build.sh"

# --- gfopengif.datatype ---------------------------------------------------------
# GIF on the Amiga itself, every frame of an animation (gif/gifclass.c,
# gif/build.sh); tests/test-gif.sh checks the decoder on x86 or ARM64 cores.
OS32_GCC16="$S" PREFIX="$OUT" ACFLAGS="$ACFLAGS" ACLIB="$ACLIB" sh "$HERE/gif/build.sh"

# --- stream.datatype, gzip.datatype, brotli.datatype --------------------------
# The "data" class and its first two decoders (stream/build.sh); gzip
# inflates through z.library. tests/test-stream.sh checks the cores on x86
# or ARM64 cores.
OS32_GCC16="$S" PREFIX="$OUT" sh "$HERE/stream/build.sh"

# --- gfopensound.datatype -------------------------------------------------------
# FLAC, Ogg (Vorbis, Opus), AAC/M4A, ALAC, WMA and MP3, decoded by the
# media.decode/1 service on the services card or a paired Cradle.
# With neither, FLAC, Ogg Vorbis and MP3 are decoded on this CPU
# (opensound/snddec.c: dr_flac, stb_vorbis and dr_mp3, single files SOURCES
# pins, fetched and checked here), which is also how OIA_Reader reads them.
# libm: libnix's soft floating point, calling the ROM math libraries.
single() {       # FILE SHA256: a single upstream file into $WORK/single
    t="$TARBALLS/$1"
    [ -f "$t" ] || sh "$HERE/../tools/fetch-tarball.sh" "$1" "$TARBALLS" || exit 2
    echo "$2  $t" | sha256sum -c - >/dev/null || { echo "SHA-256 mismatch: $t"; exit 2; }
    mkdir -p "$WORK/single" && cp "$t" "$WORK/single/$1"
}
single stb_vorbis.c 4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636
single dr_mp3.h f5c8020eeebb79877184432efdd2651f24869f5bb8f379d45450fd0aed1ee915
single dr_flac.h b8078eeb5d523502eb7964508cd7ec8450bc79d383bec90aa17fd9e2fefd9c24
(cd "$WORK/single" && patch -p1 -s < "$HERE/patches/stb_vorbis-1.22-comment-list-null.patch")
XFLAGS="-I$HERE/common -I$HERE/include $ACFLAGS" compile "$HERE" "$WORK/obj-opensound" \
    common/dtstart.c common/dtlib.c common/dtservice.c common/dtstack.c \
    common/dtencode.c common/encodemsg.c opensound/soundclass.c
# -fwrapv: dr_flac's prediction overflows a signed int on a damaged file;
# wrapping is what the 68k does anyway, and now the compiler knows it.
XFLAGS="-I$WORK/single -w -fwrapv" compile "$HERE" "$WORK/obj-snddec" opensound/snddec.c
$CC -nostartfiles -m68040 $SOFT -o "$OUT/Classes/DataTypes/gfopensound.datatype" \
    "$WORK"/obj-opensound/common_dtstart.o "$WORK"/obj-opensound/common_dtlib.o \
    "$WORK"/obj-opensound/common_dtservice.o "$WORK"/obj-opensound/common_dtstack.o \
    "$WORK"/obj-opensound/common_dtencode.o \
    "$WORK"/obj-opensound/common_encodemsg.o "$WORK"/obj-opensound/opensound_soundclass.o \
    "$WORK"/obj-snddec/opensound_snddec.o $ACLIB -lamiga -lm \
    -Wl,-Map="$WORK/gfopensound.datatype.map"
echo "gfopensound.datatype: $(wc -c < "$OUT/Classes/DataTypes/gfopensound.datatype") bytes"
desc_gfopensound "$OUT"

# --- gfopenaiff.datatype and gfopenwave.datatype --------------------------------
# AIFF/AIFC and WAV, read (8 to 32 bits, float too) and written (DTM_WRITE,
# DTWM_RAW: AIFF or WAV, 16 or 8 bits, or headerless PCM): pcm/pcmclass.c,
# built twice over pcm/pcmcore.c, which tests/test-pcm.sh checks on x86 or
# ARM64 cores. Their descriptors' masks are longer than the system's AIFF
# and WAVE ones (the first chunk too), so they are tried first.
for k in aiff wave; do
    def=; [ $k = wave ] && def=-DPCM_CLASS_WAVE
    XFLAGS="-I$HERE/common -I$HERE/include -I$HERE/pcm $def $ACFLAGS" compile "$HERE" "$WORK/obj-$k" \
        common/dtstart.c common/dtlib.c pcm/pcmcore.c pcm/pcmclass.c
    $CC -nostartfiles -m68040 $SOFT -o "$OUT/Classes/DataTypes/gfopen$k.datatype" \
        "$WORK"/obj-$k/common_dtstart.o "$WORK"/obj-$k/common_dtlib.o \
        "$WORK"/obj-$k/pcm_pcmcore.o "$WORK"/obj-$k/pcm_pcmclass.o $ACLIB -lamiga \
        -Wl,-Map="$WORK/gfopen$k.datatype.map"
    echo "gfopen$k.datatype: $(wc -c < "$OUT/Classes/DataTypes/gfopen$k.datatype") bytes"
done
# shellcheck disable=SC2046
DT_IFF=1 python3 "$HERE/common/mkdtdesc.py" "$OUT/Devs/DataTypes/GFOpenAIFF" "AIFF sound" gfopenaiff soun aiff "#?" \
    '$VER: GFOpenAIFF 47.1 (8.11.2026)' $(chars "FORM") ANY ANY ANY ANY $(chars "AIFFCOMM")
# shellcheck disable=SC2046
DT_IFF=1 python3 "$HERE/common/mkdtdesc.py" "$OUT/Devs/DataTypes/GFOpenAIFC" "AIFF-C sound" gfopenaiff soun aifc "#?" \
    '$VER: GFOpenAIFC 47.1 (8.11.2026)' $(chars "FORM") ANY ANY ANY ANY $(chars "AIFC")
# shellcheck disable=SC2046
python3 "$HERE/common/mkdtdesc.py" "$OUT/Devs/DataTypes/GFOpenWAVE" "WAV sound" gfopenwave soun wave "#?" \
    '$VER: GFOpenWAVE 47.1 (8.11.2026)' $(chars "RIFF") ANY ANY ANY ANY $(chars "WAVEfmt ")
for f in GFOpenAIFF GFOpenAIFC GFOpenWAVE; do echo "Devs/DataTypes/$f: $(wc -c < "$OUT/Devs/DataTypes/$f") bytes"; done

# --- gfopenmodule.datatype -----------------------------------------------------
# Music modules: libxmp mixes them a chunk at a time for a player process
# of the object's own, which plays them through AHI (16-bit) or Paula
# (openmodule/DESIGN.md). The mixing goes to a cores board core
# (gfopenmulticore.library, headers copied in include/), else to
# media.decode/1 (dtservice), else stays on this CPU. libxmp's loaders, all of
# them; no depackers or ProWizard (they write temporary files) and no Ogg
# Vorbis samples (openmodule/xmpglue.c).
unpack libxmp libxmp-4.7.3.tar.gz b6a98797e4fb9c9a705f5d53112aa5214561857e929a644928b9e658930d9440
X="$WORK/libxmp/libxmp-4.7.3"
XMPFLAGS="-DWORDS_BIGENDIAN -DLIBXMP_NO_DEPACKERS -DLIBXMP_NO_PROWIZARD -DLIBXMP_STATIC -I$X/include"
# shellcheck disable=SC2046
XFLAGS="$XMPFLAGS -w" compile "$X" "$WORK/obj-libxmp" \
    $(cd "$X" && ls src/*.c | grep -v -e win32.c -e mkstemp.c -e tempfile.c) \
    $(cd "$X" && ls src/loaders/*.c | grep -v -e pw_load.c -e vorbis.c)
rm -f "$WORK/libxmp.a"
"$AR" rcs "$WORK/libxmp.a" "$WORK"/obj-libxmp/*.o
# dtlib without its malloc (DT_OWN_MALLOC): gfopenmodule's own keeps libxmp's
# memory in one arena when a cores board may mix (openmodule/xmpglue.h);
# dtservice for media.decode/1, the third rung.
XFLAGS="-I$HERE/common -I$HERE/include -I$X/include -DLIBXMP_STATIC -DDT_OWN_MALLOC $ACFLAGS" compile "$HERE" "$WORK/obj-openmodule" \
    common/dtstart.c common/dtlib.c common/dtservice.c openmodule/moduleclass.c openmodule/xmpglue.c openmodule/novorbis.c
# libm: libnix's soft floating point, which calls the ROM math libraries
# (opened in the player process; see moduleclass.c).
$CC -nostartfiles -m68040 $SOFT -o "$OUT/Classes/DataTypes/gfopenmodule.datatype" \
    "$WORK"/obj-openmodule/common_dtstart.o "$WORK"/obj-openmodule/common_dtlib.o \
    "$WORK"/obj-openmodule/common_dtservice.o "$WORK"/obj-openmodule/openmodule_moduleclass.o \
    "$WORK"/obj-openmodule/openmodule_xmpglue.o \
    "$WORK"/obj-openmodule/openmodule_novorbis.o \
    "$WORK/libxmp.a" $ACLIB -lamiga -lm \
    -Wl,-Map="$WORK/gfopenmodule.datatype.map"
echo "gfopenmodule.datatype: $(wc -c < "$OUT/Classes/DataTypes/gfopenmodule.datatype") bytes"
# modbench (tests/modbench.c): libxmp's mixing time on this Amiga, a plain
# Shell program, left in work/.
$CC -O2 -m68040 $SOFT -fomit-frame-pointer $NONULL -I"$X/include" -DLIBXMP_STATIC -o "$WORK/modbench" \
    "$HERE/tests/modbench.c" "$WORK"/obj-openmodule/openmodule_novorbis.o "$WORK/libxmp.a" -lm
desc_gfopenmodule "$OUT"

# --- gfopendoc.datatype ---------------------------------------------------------
# Office documents as pictures of their pages, laid out by LibreOffice through
# the doc.render/1 service on the services card or a paired Cradle.
XFLAGS="-I$HERE/common -I$HERE/include $ACFLAGS" compile "$HERE" "$WORK/obj-opendoc" \
    common/dtstart.c common/dtlib.c common/dtservice.c opendoc/docclass.c
$CC -nostartfiles -m68040 $SOFT -o "$OUT/Classes/DataTypes/gfopendoc.datatype" \
    "$WORK"/obj-opendoc/common_dtstart.o "$WORK"/obj-opendoc/common_dtlib.o \
    "$WORK"/obj-opendoc/common_dtservice.o "$WORK"/obj-opendoc/opendoc_docclass.o $ACLIB -lamiga \
    -Wl,-Map="$WORK/gfopendoc.datatype.map"
echo "gfopendoc.datatype: $(wc -c < "$OUT/Classes/DataTypes/gfopendoc.datatype") bytes"
desc_gfopendoc "$OUT"

# --- cdxl.datatype ------------------------------------------------------------
# CDXL films, played on the Amiga itself a frame at a time from the file.
XFLAGS="-I$HERE/common -I$HERE/include -I$HERE/cdxl $ACFLAGS" compile "$HERE" "$WORK/obj-cdxl" \
    common/dtstart.c common/dtlib.c cdxl/cdxl_core.c cdxl/cdxlclass.c
$CC -nostartfiles -m68040 $SOFT -o "$OUT/Classes/DataTypes/cdxl.datatype" \
    "$WORK"/obj-cdxl/common_dtstart.o "$WORK"/obj-cdxl/common_dtlib.o \
    "$WORK"/obj-cdxl/cdxl_cdxl_core.o "$WORK"/obj-cdxl/cdxl_cdxlclass.o $ACLIB -lamiga \
    -Wl,-Map="$WORK/cdxl.datatype.map"
echo "cdxl.datatype: $(wc -c < "$OUT/Classes/DataTypes/cdxl.datatype") bytes"
desc_cdxl "$OUT"

# --- gfopenvideo.datatype -------------------------------------------------------
# MP4, MOV, MKV, AVI, WMV, MPEG and FLV, decoded frame by frame by the
# media.decode/1 service on the services card or a paired Cradle.
XFLAGS="-I$HERE/common -I$HERE/include $ACFLAGS" compile "$HERE" "$WORK/obj-openvideo" \
    common/dtstart.c common/dtlib.c common/dtservice.c common/dtencode.c common/encodemsg.c openvideo/videoclass.c
$CC -nostartfiles -m68040 $SOFT -o "$OUT/Classes/DataTypes/gfopenvideo.datatype" \
    "$WORK"/obj-openvideo/common_dtstart.o "$WORK"/obj-openvideo/common_dtlib.o \
    "$WORK"/obj-openvideo/common_dtservice.o "$WORK"/obj-openvideo/common_dtencode.o \
    "$WORK"/obj-openvideo/common_encodemsg.o "$WORK"/obj-openvideo/openvideo_videoclass.o $ACLIB -lamiga \
    -Wl,-Map="$WORK/gfopenvideo.datatype.map"
echo "gfopenvideo.datatype: $(wc -c < "$OUT/Classes/DataTypes/gfopenvideo.datatype") bytes"
desc_gfopenvideo "$OUT"
