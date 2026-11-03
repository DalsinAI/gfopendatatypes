#!/usr/bin/env python3
"""Test files for gfopengif.datatype's decoder (gif/gifdecode.c).

Writes GIF files into OUTDIR with its own LZW encoder, and expected.txt:
what tests/giftest.c must print for them. Each frame's screen is worked out
here from the index arrays the file was made from, as web browsers composite
GIF frames: a transparent screen, disposal 2 clearing the frame's area to
transparent, disposal 3 putting back what was under the frame.

With --pillow, every one-frame file is also opened with Pillow, and its
RGBA pixels must be the ones expected (Pillow's own reader against this
encoder); and Pillow writes a few files of its own, whose frames are checked
with Pillow's decode for those that are one frame.

usage: make-gif.py OUTDIR [--pillow]

MIT, Copyright (c) 2026 AmiCradle Team.
"""
import os
import random
import struct
import sys


def fnv(words):
    h = 2166136261
    for w in words:
        for b in (24, 16, 8, 0):
            h ^= (w >> b) & 0xff
            h = (h * 16777619) & 0xffffffff
    return h


def lzw(indices, min_size, clear_every=None):
    """GIF LZW: codes packed LSB first. clear_every: send a clear code after
    that many codes (else when the table is full)."""
    clear, end = 1 << min_size, (1 << min_size) + 1
    out, acc, nbits = bytearray(), 0, 0

    def emit(code, size):
        nonlocal acc, nbits
        acc |= code << nbits
        nbits += size
        while nbits >= 8:
            out.append(acc & 0xff)
            acc >>= 8
            nbits -= 8

    def reset():
        return {(i,): i for i in range(clear)}, end + 1, min_size + 1

    table, nxt, size = reset()
    emit(clear, size)
    sent = 0
    w = ()
    for k in indices:
        wk = w + (k,)
        if wk in table:
            w = wk
            continue
        emit(table[w], size)
        sent += 1
        if nxt < 4096:
            table[wk] = nxt
            nxt += 1
            if nxt > (1 << size) and size < 12:
                size += 1
        if (clear_every and sent % clear_every == 0) or nxt >= 4096:
            emit(clear, size)
            table, nxt, size = reset()
        w = (k,)
    if w:
        emit(table[w], size)
        # the decoder adds an entry for this code too: its size may grow
        if nxt < 4096:
            nxt += 1
            if nxt > (1 << size) and size < 12:
                size += 1
    emit(end, size)
    if nbits:
        out.append(acc & 0xff)
    data = bytearray([min_size])
    for i in range(0, len(out), 255):
        part = out[i:i + 255]
        data.append(len(part))
        data += part
    data.append(0)
    return bytes(data)


def table_bits(n):
    b = 0
    while (2 << b) < n:
        b += 1
    return b


def pal_bytes(pal):
    n = 2 << table_bits(len(pal))
    pal = list(pal) + [(0, 0, 0)] * (n - len(pal))
    return b"".join(bytes(c) for c in pal)


def interlace_order(h):
    rows = []
    for start, step in ((0, 8), (4, 8), (2, 4), (1, 2)):
        rows += list(range(start, h, step))
    return rows


class Frame:
    def __init__(self, x, y, w, h, idx, pal=None, delay=0, disposal=0, trans=None, interlaced=False,
                 min_size=None, clear_every=None):
        self.x, self.y, self.w, self.h = x, y, w, h
        self.idx, self.pal, self.delay, self.disposal, self.trans = idx, pal, delay, disposal, trans
        self.interlaced, self.min_size, self.clear_every = interlaced, min_size, clear_every


def make(sw, sh, frames, gpal=None, loops=None, version=b"89a", trailer=True):
    f = bytearray(b"GIF" + version)
    flags = 0
    if gpal:
        flags = 0x80 | table_bits(len(gpal)) | (7 << 4)
    f += struct.pack("<HHBBB", sw, sh, flags, 0, 0)
    if gpal:
        f += pal_bytes(gpal)
    if loops is not None:
        f += b"\x21\xff\x0bNETSCAPE2.0\x03\x01" + struct.pack("<H", loops) + b"\x00"
    for fr in frames:
        if fr.delay or fr.disposal or fr.trans is not None:
            packed = (fr.disposal << 2) | (1 if fr.trans is not None else 0)
            f += b"\x21\xf9\x04" + struct.pack("<BHB", packed, fr.delay, fr.trans or 0) + b"\x00"
        iflags = 0x40 if fr.interlaced else 0
        if fr.pal:
            iflags |= 0x80 | table_bits(len(fr.pal))
        f += b"\x2c" + struct.pack("<HHHHB", fr.x, fr.y, fr.w, fr.h, iflags)
        if fr.pal:
            f += pal_bytes(fr.pal)
        rows = interlace_order(fr.h) if fr.interlaced else range(fr.h)
        stream = []
        for r in rows:
            stream += fr.idx[r * fr.w:(r + 1) * fr.w]
        ncol = len(fr.pal or gpal)
        min_size = fr.min_size or max(2, table_bits(ncol) + 1)
        f += lzw(stream, min_size, fr.clear_every)
    if trailer:
        f += b"\x3b"
    return bytes(f)


def composite(sw, sh, frames, gpal):
    """Every frame's screen as 0xAARRGGBB words, browser style."""
    screen = [0] * (sw * sh)
    out = []
    prev = None
    saved = None
    for fr in frames:
        if prev is not None:
            if prev.disposal == 2:
                for y in range(prev.y, min(prev.y + prev.h, sh)):
                    for x in range(prev.x, min(prev.x + prev.w, sw)):
                        screen[y * sw + x] = 0
            elif prev.disposal == 3:
                screen = saved
        if fr.disposal == 3:
            saved = list(screen)
        pal = fr.pal or gpal
        pal = list(pal) + [(0, 0, 0)] * ((2 << table_bits(len(pal))) - len(pal))   # as the file has it
        for y in range(fr.h):
            for x in range(fr.w):
                i = fr.idx[y * fr.w + x]
                sx, sy = fr.x + x, fr.y + y
                if sx >= sw or sy >= sh or i >= len(pal) or i == fr.trans:
                    continue
                r, g, b = pal[i]
                screen[sy * sw + sx] = 0xff000000 | r << 16 | g << 8 | b
        out.append(list(screen))
        prev = fr
    return out


def pixels(im):
    b = im.tobytes()
    return [tuple(b[i:i + 4]) for i in range(0, len(b), 4)]


def rnd_pal(rng, n):
    return [(rng.randrange(256), rng.randrange(256), rng.randrange(256)) for _ in range(n)]


def rnd_idx(rng, w, h, n, runs=True):
    if not runs:
        return [rng.randrange(n) for _ in range(w * h)]
    out, v = [], 0
    while len(out) < w * h:
        v = rng.randrange(n)
        out += [v] * rng.randrange(1, 9)
    return out[:w * h]


def cases(rng):
    c = []
    p4 = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255)]
    # stills
    c.append(("still-2col", 13, 7, [Frame(0, 0, 13, 7, rnd_idx(rng, 13, 7, 2))], [(0, 0, 0), (255, 255, 255)], None))
    for n in (2, 4, 16, 64, 256):
        p = rnd_pal(rng, n)
        c.append(("still-%d" % n, 61, 37, [Frame(0, 0, 61, 37, rnd_idx(rng, 61, 37, n))], p, None))
    p = rnd_pal(rng, 256)
    c.append(("still-noise-256", 120, 90, [Frame(0, 0, 120, 90, rnd_idx(rng, 120, 90, 256, False))], p, None))
    c.append(("still-clears", 64, 64, [Frame(0, 0, 64, 64, rnd_idx(rng, 64, 64, 16, False), clear_every=37)],
              rnd_pal(rng, 16), None))
    c.append(("still-min8-for-4", 20, 20, [Frame(0, 0, 20, 20, rnd_idx(rng, 20, 20, 4), min_size=8)], p4, None))
    c.append(("still-interlaced", 33, 29, [Frame(0, 0, 33, 29, rnd_idx(rng, 33, 29, 16), interlaced=True)],
              rnd_pal(rng, 16), None))
    c.append(("still-interlaced-1row", 9, 1, [Frame(0, 0, 9, 1, rnd_idx(rng, 9, 1, 4), interlaced=True)], p4, None))
    c.append(("still-interlaced-3rows", 9, 3, [Frame(0, 0, 9, 3, rnd_idx(rng, 9, 3, 4), interlaced=True)], p4, None))
    c.append(("still-trans", 30, 20, [Frame(0, 0, 30, 20, rnd_idx(rng, 30, 20, 4), trans=2)], p4, None))
    c.append(("still-local-pal", 30, 20, [Frame(0, 0, 30, 20, rnd_idx(rng, 30, 20, 16), pal=rnd_pal(rng, 16))], None, None))
    c.append(("still-offset", 40, 30, [Frame(5, 7, 20, 10, rnd_idx(rng, 20, 10, 4))], p4, None))
    c.append(("still-past-edge", 20, 20, [Frame(12, 15, 16, 9, rnd_idx(rng, 16, 9, 4))], p4, None))
    c.append(("still-index-past-table", 10, 10, [Frame(0, 0, 10, 10, rnd_idx(rng, 10, 10, 8), min_size=3)],
              p4, None))
    c.append(("still-87a", 16, 16, [Frame(0, 0, 16, 16, rnd_idx(rng, 16, 16, 4))], p4, None, b"87a"))
    c.append(("still-no-screen", 0, 0, [Frame(3, 4, 10, 6, rnd_idx(rng, 10, 6, 4))], p4, None))
    c.append(("still-no-trailer", 16, 16, [Frame(0, 0, 16, 16, rnd_idx(rng, 16, 16, 4))], p4, None, b"89a", False))
    # animations
    w, h = 48, 32
    gp = rnd_pal(rng, 32)
    full = [Frame(0, 0, w, h, rnd_idx(rng, w, h, 32), delay=10)]
    for i in range(5):
        x, y = rng.randrange(w - 10), rng.randrange(h - 8)
        fw, fh = rng.randrange(4, w - x + 1), rng.randrange(3, h - y + 1)
        full.append(Frame(x, y, fw, fh, rnd_idx(rng, fw, fh, 32), delay=5 + i, trans=rng.randrange(32)))
    c.append(("anim-keep", w, h, full, gp, 0))
    for d in (1, 2, 3):
        fr = []
        for i in range(6):
            x, y = rng.randrange(w - 10), rng.randrange(h - 8)
            fw, fh = rng.randrange(4, w - x + 1), rng.randrange(3, h - y + 1)
            fr.append(Frame(x, y, fw, fh, rnd_idx(rng, fw, fh, 32), delay=i * 3, disposal=d,
                            trans=rng.choice([None, rng.randrange(32)])))
        c.append(("anim-dispose%d" % d, w, h, fr, gp, 3))
    fr = []
    for i in range(12):
        x, y = rng.randrange(w), rng.randrange(h)
        fw, fh = rng.randrange(1, 30), rng.randrange(1, 20)
        fr.append(Frame(x, y, fw, fh, rnd_idx(rng, fw, fh, 16), pal=rng.choice([None, rnd_pal(rng, 16)]),
                        delay=rng.randrange(0, 50), disposal=rng.randrange(4),
                        trans=rng.choice([None, rng.randrange(16)]), interlaced=rng.random() < 0.3))
    c.append(("anim-mixed", w, h, fr, rnd_pal(rng, 16), 7))
    # an advertisement's shape: a small first frame away from the corner
    fr = [Frame(20, 10, 30, 20, rnd_idx(rng, 30, 20, 8), delay=200)]
    fr += [Frame(0, 0, 80, 40, rnd_idx(rng, 80, 40, 8), delay=100, disposal=1)]
    c.append(("anim-ad", 80, 40, fr, rnd_pal(rng, 8), None))
    return c


def main():
    out = sys.argv[1]
    pillow = "--pillow" in sys.argv[2:]
    os.makedirs(out, exist_ok=True)
    rng = random.Random(20261103)
    lines = []
    files = []
    for case in cases(rng):
        name, sw, sh, frames, gpal, loops = case[:6]
        version = case[6] if len(case) > 6 else b"89a"
        trailer = case[7] if len(case) > 7 else True
        data = make(sw, sh, frames, gpal, loops, version, trailer)
        path = name + ".gif"
        with open(os.path.join(out, path), "wb") as fh:
            fh.write(data)
        if not sw or not sh:
            sw = max(f.x + f.w for f in frames)
            sh = max(f.y + f.h for f in frames)
        screens = composite(sw, sh, frames, gpal)
        lines.append("%s %d %d frames=%d loops=%d" % (path, sw, sh, len(frames), -1 if loops is None else loops))
        for i, (fr, s) in enumerate(zip(frames, screens)):
            alpha = 1 if any(not (p >> 24) for p in s) else 0
            lines.append("%s#%d delay=%d alpha=%d fnv=%08x" % (path, i, fr.delay, alpha, fnv(s)))
        files.append((path, sw, sh, frames, screens))
    with open(os.path.join(out, "expected.txt"), "w") as fh:
        fh.write("\n".join(lines) + "\n")
    if pillow:
        from PIL import Image
        checked = 0
        for path, sw, sh, frames, screens in files:
            f = frames[0]
            if len(frames) != 1 or (f.x, f.y, f.w, f.h) != (0, 0, sw, sh) or path.startswith("still-index-past"):
                continue                  # Pillow fills what the frame leaves with the background colour
            im = Image.open(os.path.join(out, path)).convert("RGBA")
            got = [(a << 24 | r << 16 | g << 8 | b) if a else 0 for r, g, b, a in pixels(im)]
            if got != screens[0]:
                sys.exit("Pillow reads %s otherwise" % path)
            checked += 1
        print("make-gif.py: Pillow reads %d one-frame files as expected" % checked)
        # Pillow's own encoder: one-frame files, checked against Pillow's decode.
        for i, (w, h, n) in enumerate(((50, 40, 2), (77, 23, 16), (64, 64, 256))):
            im = Image.new("P", (w, h))
            im.putpalette(sum(rnd_pal(rng, 256), ()))
            im.putdata(rnd_idx(rng, w, h, n))
            path = "pillow-%d.gif" % i
            im.save(os.path.join(out, path), interlace=(i == 1))
            rgba = Image.open(os.path.join(out, path)).convert("RGBA")
            s = [(a << 24 | r << 16 | g << 8 | b) if a else 0 for r, g, b, a in pixels(rgba)]
            fh = open(os.path.join(out, "expected.txt"), "a")
            fh.write("%s %d %d frames=1 loops=-1\n%s#0 delay=0 alpha=%d fnv=%08x\n"
                     % (path, w, h, path, 1 if any(not (p >> 24) for p in s) else 0, fnv(s)))
            fh.close()
        print("make-gif.py: 3 files from Pillow's encoder")


if __name__ == "__main__":
    main()
