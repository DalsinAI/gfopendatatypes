#!/bin/sh
# common/encodemsg.c, what gfopenpicture, gfopensound and gfopenvideo send
# media.encode/1 for a raw DTM_WRITE (HEIC, M4A/AAC, WMA, HEVC, WMV are
# written by the service's FFmpeg, never on the 68k): tests/encodemsgtest.c
# checks the settings and info byte for byte, the file each datatype picks
# and a sound.datatype object's samples in every layout, under
# AddressSanitizer and UBSan; with QEMU_M68K set, also as 68040 code under
# qemu-m68k, whose requests must match the host's byte for byte. Then, with
# a gfopenservice checkout and FFmpeg to hand, the requests go through the
# service's own library (host/media_encode.c) and the files come back:
# the HEIC read by libheif (ImageMagick), the M4A and WMA by ffprobe and
# ffmpeg, the two tones where they were sent.
# MIT, Copyright (c) 2026 AmiCradle Team.
#
#   CC            the host compiler (default cc)
#   SAN           sanitizer flags (default -fsanitize=address,undefined; SAN= for none)
#   QEMU_M68K     qemu-m68k, to run it as 68k code too
#   GFOPENSERVICE the gfopenservice checkout (default: beside this repository)
#
# usage: sh tests/test-encodemsg.sh [OUTDIR]    (default work/encodemsg-test)
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/work/encodemsg-test"}
CC=${CC:-cc}
SAN=${SAN--fsanitize=address,undefined -fno-sanitize-recover=all}
SERVICE=${GFOPENSERVICE:-"$HERE/../../gfopenservice"}
rm -rf "$OUT"
mkdir -p "$OUT/host"
# shellcheck disable=SC2086
$CC -O1 -g -Wall -Wextra -Werror $SAN -I"$HERE/common" -o "$OUT/encodemsgtest" \
    "$HERE/tests/encodemsgtest.c" "$HERE/common/encodemsg.c"
"$OUT/encodemsgtest" "$OUT/host"
if [ -n "${QEMU_M68K:-}" ]; then
    mkdir -p "$OUT/m68k"
    m68k-linux-gnu-gcc -m68040 -O2 -static -Wall -Wextra -Werror -I"$HERE/common" -o "$OUT/encodemsgtest-m68k" \
        "$HERE/tests/encodemsgtest.c" "$HERE/common/encodemsg.c"
    printf '68040 under qemu-m68k: '
    "$QEMU_M68K" -cpu m68040 "$OUT/encodemsgtest-m68k" "$OUT/m68k" | tail -1
    for f in "$OUT"/host/*; do
        cmp -s "$f" "$OUT/m68k/$(basename "$f")" || { echo "FAIL $(basename "$f"): 68k and host requests differ"; exit 1; }
    done
    echo "68040 under qemu-m68k: requests byte for byte the host's"
fi
# The datatypes' code: plain C99, no writable data
$CC -std=c99 -Os -Wall -Wextra -Werror -pedantic -c "$HERE/common/encodemsg.c" -o "$OUT/encodemsg.o"
data=$(size -A "$OUT/encodemsg.o" | awk '$1 ~ /^\.(data|bss)/ && $2 > 0 {print $1}' | tr '\n' ' ')
[ -z "$data" ] || { echo "FAIL encodemsg.c: writable data ($data)"; exit 1; }
echo "encodemsg.c: C99 -Wall -Wextra -Werror -pedantic, no writable data"

# The round trip through the service's library and FFmpeg.
if [ ! -f "$SERVICE/host/media_encode.c" ]; then
    echo "round trip: no gfopenservice checkout with media.encode/1 at $SERVICE (GFOPENSERVICE=), skipped"
    exit 0
fi
if ! command -v ffmpeg >/dev/null 2>&1 || ! command -v ffprobe >/dev/null 2>&1; then
    echo "round trip: no ffmpeg and ffprobe, skipped"
    exit 0
fi
gcc -O2 -Wall -fPIC -shared -o "$OUT/libmediaencode.so" "$SERVICE/host/media_encode.c" "$SERVICE/host/heif_mux.c" \
    "$SERVICE/host/hostrun.c" -lpthread
python3 -I - "$OUT/libmediaencode.so" "$OUT/host" <<'EOF'
import ctypes, json, math, shutil, struct, subprocess, sys
lib, d = ctypes.CDLL(sys.argv[1]), sys.argv[2]
FORMATS, BEGIN, PUT, FINISH, READ, CLOSE = 16, 17, 18, 19, 20, 21


class Buf(ctypes.Structure):
    _fields_ = [("inp", ctypes.c_void_p), ("out", ctypes.c_void_p), ("length", ctypes.c_uint32),
                ("written", ctypes.c_uint32)]


def call(op, arg, extra=(0, 0, 0, 0), inp=None, room=0):
    """buf0 the bytes sent, buf1 room for the answer; (status, result, aux, answer)"""
    b, keep = (Buf * 4)(), []
    if inp is not None:
        keep.append(ctypes.create_string_buffer(inp, len(inp) or 1))
        b[0].inp, b[0].length = ctypes.cast(keep[-1], ctypes.c_void_p), len(inp)
    if room:
        keep.append(ctypes.create_string_buffer(room))
        b[1].out, b[1].length = ctypes.cast(keep[-1], ctypes.c_void_p), room
    r, a = ctypes.c_uint32(), ctypes.c_uint32()
    st = lib.me_call(op, arg, (ctypes.c_uint32 * 4)(*extra), b, ctypes.byref(r), ctypes.byref(a))
    return st, r.value, a.value, ctypes.string_at(b[1].out, b[1].written) if room else b""


def write(name):
    """BEGIN, PUT and FINISH as dtencode.c sends them; the file as READ gives it."""
    settings = open(f"{d}/{name}.settings", "rb").read()
    st, job, _, _ = call(BEGIN, 0, inp=settings)
    assert st == 0, (name, st)
    for stream in (0, 1):
        try:
            data = open(f"{d}/{name}.stream{stream}", "rb").read()
        except FileNotFoundError:
            continue
        for at in range(0, len(data), 65536):
            assert call(PUT, job, (stream, 0, 0, 0), data[at:at + 65536])[0] == 0
    while True:
        st, size, working, info = call(FINISH, job, room=24)
        assert st == 0, (name, st)
        if not working:
            break
    out = b""
    while len(out) < size:
        st, n, _, piece = call(READ, job, (len(out), 0, 0, 0), room=131072)
        assert st == 0 and n, (name, st)
        out += piece
    assert call(CLOSE, job)[0] == 0
    return settings, info, out


st, n, _, offered = call(FORMATS, 0, room=64)
fmts = [offered[i:i + 4].decode() for i in range(0, len(offered), 4)]
if not {"HEIC", "M4A ", "WMA "} <= set(fmts):
    print("round trip: this host's FFmpeg writes only", fmts, "- skipped")
    sys.exit(0)

settings, info, heic = write("picture")
w, h = struct.unpack(">II", settings[8:16])
assert heic[4:12] == b"ftypheic" and struct.unpack(">II", info[8:16]) == (w, h)
open(f"{d}/out.heic", "wb").write(heic)
line = f"HEIC {w}x{h} with alpha: {len(heic)} bytes"
if shutil.which("identify"):
    # -ping: the boxes only, which needs no HEVC decoder
    got = subprocess.run(["identify", "-ping", "-format", "%w %h", f"{d}/out.heic"], capture_output=True,
                         text=True).stdout
    assert got.split()[:2] == [str(w), str(h)], got
    line += ", libheif (ImageMagick) reads it"
    rgba = subprocess.run(["convert", f"{d}/out.heic", "-depth", "8", "rgba:-"], capture_output=True).stdout
    if len(rgba) != w * h * 4:
        line += " (no HEVC decoder plugin here to decode it)"
    else:
        argb = open(f"{d}/picture.stream0", "rb").read()
        src = bytes(argb[i + k] for i in range(0, len(argb), 4) for k in (1, 2, 3, 0))
        se = [sum((src[i] - rgba[i]) ** 2 for i in range(c, len(src), 4)) / (w * h) for c in range(4)]
        q = [99 if e == 0 else 10 * math.log10(65025 / e) for e in se]
        assert min(q[:3]) > 32 and q[3] > 40, q
        line += f" and decodes it: colour {min(q[:3]):.1f} dB, alpha {q[3]:.1f} dB"
print(line)

for name, codec in (("sound", "aac"), ("wma", "wmav2")):
    settings, info, out = write(name)
    path = f"{d}/out.{name}"
    open(path, "wb").write(out)
    s = json.loads(subprocess.run(["ffprobe", "-v", "error", "-show_streams", "-show_format", "-of", "json", path],
                                  capture_output=True, check=True).stdout)
    a = s["streams"][0]
    assert (a["codec_name"], int(a["sample_rate"]), a["channels"]) == (codec, 32000, 2), a
    pcm = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-f", "s16le", "-"], capture_output=True, check=True).stdout
    pcm = struct.unpack("<%dh" % (len(pcm) // 2), pcm)
    tones = []
    for ch in (0, 1):
        mid = pcm[ch::2][8000:24000]                       # half a second from the middle
        tones.append(sum(1 for x, y in zip(mid, mid[1:]) if x < 0 <= y) * 2)
    assert 430 <= tones[0] <= 450 and 860 <= tones[1] <= 900, tones
    print(f"{s['format']['format_name'].split(',')[0].upper()} {codec}: {len(out)} bytes, 32000 Hz stereo, "
          f"{tones[0]} Hz left and {tones[1]} Hz right")
print("round trip through media.encode/1 and FFmpeg: all passed")
EOF
