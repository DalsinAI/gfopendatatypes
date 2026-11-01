#!/bin/sh
# Makes the test pictures and clips the datatype tests use (on a PC with
# Python's Pillow and GStreamer's vp8enc, vp9enc and webmmux), then prints
# the PC's reference results:
#   tests/webpraw  (libwebp on the PC)   -> what tests/dtpic prints on the Amiga
#   tests/webmprobe -chunky              -> what tests/dtanim prints on the Amiga
# MIT, Copyright (c) 2026 AmiCradle Team.
#
# usage: tests/make-media.sh OUTDIR
set -eu
OUT=${1:?usage: make-media.sh OUTDIR}
mkdir -p "$OUT"
python3 - "$OUT" <<'PY'
import sys
from PIL import Image
out = sys.argv[1]
def grad(w, h, alpha=False):
    im = Image.new('RGBA' if alpha else 'RGB', (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            r = (x * 255) // (w - 1); g = (y * 255) // (h - 1); b = ((x + y) * 7) & 255
            px[x, y] = (r, g, b, (x * 13 + y * 29) & 255) if alpha else (r, g, b)
    return im
grad(64, 48).save(out + '/lossy.webp', 'WEBP', quality=80)
grad(40, 30, True).save(out + '/lossless-alpha.webp', 'WEBP', lossless=True)
grad(50, 20, True).save(out + '/lossy-alpha.webp', 'WEBP', quality=75)
f1 = grad(32, 32); f2 = Image.new('RGB', (32, 32), (200, 30, 30))
f1.save(out + '/anim.webp', 'WEBP', save_all=True, append_images=[f2], duration=100, loop=0, lossless=True)
PY
gst-launch-1.0 -q videotestsrc num-buffers=30 pattern=smpte ! video/x-raw,width=160,height=120,framerate=10/1 \
    ! vp8enc keyframe-max-dist=10 ! webmmux ! filesink location="$OUT/vp8.webm"
gst-launch-1.0 -q videotestsrc num-buffers=30 pattern=ball ! video/x-raw,format=I420,width=160,height=120,framerate=10/1 \
    ! vp9enc keyframe-max-dist=10 ! webmmux ! filesink location="$OUT/vp9.webm"
ls -l "$OUT"
