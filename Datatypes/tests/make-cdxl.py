#!/usr/bin/env python3
"""Writes CDXL test films for cdxl.datatype's core (tests/test-cdxl.sh), and
for each what it shows: frames in every picture layout the core plays
(bitplanes one after another, the planes of each row in turn, chunky RGB),
palettes of 2 to 256 colours, HAM6 and HAM8, mono and stereo sound.

usage: make-cdxl.py OUTDIR
Writes OUTDIR/NAME.cdxl, OUTDIR/NAME.rgb (each frame shown, R, G, B bytes,
frame after frame), OUTDIR/NAME.pcm (the sound as signed 8-bit mono, stereo
mixed as (left + right) >> 1) and OUTDIR/films.txt (NAME width height
frames fps rate, one a line).

MIT, Copyright (c) 2026 AmiCradle Team.
"""
import os
import random
import struct
import sys

FILMS = [
    # name, width, height, planes, ham, layout, frames, audio a channel, stereo, rate, fps
    ("planar5", 64, 40, 5, False, 0x00, 4, 920, False, 11040, 12),
    ("line4", 50, 30, 4, False, 0x80, 3, 0, False, 0, 0),
    ("planar8", 96, 20, 8, False, 0x00, 3, 1470, False, 22050, 15),
    ("ham6", 80, 16, 6, True, 0x00, 3, 1470, True, 22050, 15),
    ("ham8", 72, 18, 8, True, 0x80, 3, 882, False, 22050, 25),
    ("rgb", 40, 24, 24, False, 0x20, 2, 882, True, 22050, 25),
    ("mono1", 17, 9, 1, False, 0x00, 2, 1000, False, 0, 0),   # fps from the sound: 11025 / 1000
    # frames of different sizes (the sound's length varies): found frame by frame
    ("vary", 48, 20, 4, False, 0x00, 4, [700, 900, 500, 1100], False, 11025, 12),
    # sizes that vary yet add up to four of the first's, the last the first's
    # size: found in constant time, then frame by frame when frame 1 isn't there
    ("trap", 32, 10, 3, False, 0x00, 4, [800, 700, 900, 800], False, 11025, 12),
    # a long film of one size: opened in constant time
    ("long", 16, 8, 1, False, 0x00, 400, 0, False, 0, 25),
]


def shown(pal, planes, ham, idx, w, h):
    """What an Amiga shows for the pixel values idx (row by row)."""
    out = bytearray()
    for y in range(h):
        r, g, b = pal[0]
        for x in range(w):
            v = idx[y * w + x]
            if not ham:
                r, g, b = pal[v]
                out += bytes((r, g, b))
                continue
            bits = planes - 2
            c, d = v >> bits, v & ((1 << bits) - 1)
            m = d * 17 if bits == 4 else d << 2
            if c == 0:
                r, g, b = pal[d]
            elif c == 1:
                b = m if bits == 4 else m | (b & 3)
            elif c == 2:
                r = m if bits == 4 else m | (r & 3)
            else:
                g = m if bits == 4 else m | (g & 3)
            out += bytes((r, g, b))
    return bytes(out)


def planes_of(idx, w, h, planes, layout):
    row = (w + 15) // 16 * 2
    data = bytearray(row * h * planes)
    for p in range(planes):
        for y in range(h):
            at = ((y * planes + p) if layout == 0x80 else (p * h + y)) * row
            for x in range(w):
                if idx[y * w + x] >> p & 1:
                    data[at + x // 8] |= 0x80 >> (x & 7)
    return bytes(data)


def film(rng, name, w, h, planes, ham, layout, frames, audio, stereo, rate, fps):
    data, rgb, pcm, prev = bytearray(), bytearray(), bytearray(), 0
    for n in range(frames):
        if layout == 0x20:
            colours = 0
            image = bytes(rng.randrange(256) for _ in range(w * h * 3))
            show = image
        else:
            colours = 1 << (planes - 2 if ham else planes)
            pal12 = [rng.randrange(4096) for _ in range(colours)]
            pal = [((c >> 8) * 17, (c >> 4 & 15) * 17, (c & 15) * 17) for c in pal12]
            idx = [rng.randrange(1 << planes) for _ in range(w * h)]
            image = planes_of(idx, w, h, planes, layout)
            show = shown(pal, planes, ham, idx, w, h)
        a = audio[n] if isinstance(audio, list) else audio
        left = bytes(rng.randrange(256) for _ in range(a))
        right = bytes(rng.randrange(256) for _ in range(a)) if stereo else b""
        size = 32 + 2 * colours + len(image) + len(left) + len(right)
        head = bytearray(32)
        head[0] = 1
        head[1] = (1 if ham else 0) | layout | (0x10 if stereo else 0)
        struct.pack_into(">III", head, 2, size, prev, n + 1)
        struct.pack_into(">HH", head, 14, w, h)
        head[19] = planes
        struct.pack_into(">HHH", head, 20, 2 * colours, a, rate)
        head[26] = fps
        data += head
        if colours:
            data += b"".join(struct.pack(">H", c) for c in pal12)
        data += image + left + right
        rgb += show
        for i in range(a):
            sl = left[i] - 256 if left[i] > 127 else left[i]
            if stereo:
                sr = right[i] - 256 if right[i] > 127 else right[i]
                sl = (sl + sr) >> 1
            pcm.append(sl & 255)
        prev = size
    real_rate = rate or 11025
    audio = audio[0] if isinstance(audio, list) else audio
    real_fps = fps or (((real_rate + audio // 2) // audio) if audio else 12)
    if real_fps > 60:
        real_fps = 12
    return bytes(data), bytes(rgb), bytes(pcm), real_fps, real_rate


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    rng = random.Random(1985)
    lines = []
    for f in FILMS:
        data, rgb, pcm, fps, rate = film(rng, *f)
        for ext, b in ((".cdxl", data), (".rgb", rgb), (".pcm", pcm)):
            with open(os.path.join(out, f[0] + ext), "wb") as fh:
                fh.write(b)
        lines.append(f"{f[0]} {f[1]} {f[2]} {f[6]} {fps} {rate}")
    with open(os.path.join(out, "films.txt"), "w") as fh:
        fh.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
