#!/usr/bin/env python3
"""Make PNG test pictures of every kind gfopenpng.datatype reads, with no
downloads: every byte is written here (Python's zlib for the deflate), from
the PNG specification, and the picture each should decode to is worked out
here too, from the samples, not by decoding the file.

    make-png.py OUTDIR [--pillow] [--big]

writes OUTDIR/NAME.png and OUTDIR/expected.txt, a line for each file:

    NAME WIDTH HEIGHT alpha=0|1 fnv=HASH      (or NAME invalid)

HASH is FNV-1a over the picture as tests/pngdectest.c prints it: every
pixel's A, R, G, B bytes, rows top to bottom. OUTDIR/damage.txt names the
files tests/pngdectest.c damages (cut short, bytes changed).

The kinds: greyscale 1, 2, 4, 8 and 16 bits, RGB 8 and 16, palette 1, 2,
4 and 8, grey with alpha 8 and 16, RGBA 8 and 16, each plain and Adam7
interlaced, at sizes from 1x1 up (rows of odd lengths, pictures smaller
than an Adam7 block); every row filter (None, Sub, Up, Average, Paeth) in
turn; tRNS for each kind that has one (a grey or RGB key, palette alphas
shorter than the palette, indexes past the palette); deflate stored
(level 0), fixed Huffman codes, levels 1 and 9, IDAT split in one-byte
chunks and with an empty one, ancillary chunks before and after; noise;
and pictures that must be refused (bad signature, IHDR, depth, no IDAT).
With --big, 1024x768 pictures too (RGB and RGBA with grain, RGB smooth,
1-bit grey, 4-bit palette, interlaced RGB): the ones the speed numbers
are measured on.

With --pillow, each picture Pillow (the Python imaging library, when it is
installed) also reads is checked against it: Pillow decodes these files as
this script says they should decode. (Not 16-bit grey, which Pillow keeps
at 16 bits, nor a grey or RGB tRNS key below or above 8 bits, which Pillow
compares after scaling.)

MIT, Copyright (c) 2026 AmiCradle Team.
"""
import os
import struct
import sys
import zlib

ADAM7 = ((0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2))
CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}


class Rand:
    """A small LCG: the same pictures on every computer and Python."""
    def __init__(self, seed):
        self.s = seed & 0xFFFFFFFF

    def next(self, n):
        self.s = (self.s * 1103515245 + 12345) & 0xFFFFFFFF
        return (self.s >> 8) % n


def chunk(t, data):
    return struct.pack(">I", len(data)) + t + data + struct.pack(">I", zlib.crc32(t + data))


def pack_row(px, ctype, depth):
    """A row of pixels (tuples of samples) as the PNG stores it."""
    out = bytearray()
    if depth < 8:
        acc = nbits = 0
        for p in px:
            acc = (acc << depth) | p[0]
            nbits += depth
            if nbits == 8:
                out.append(acc)
                acc = nbits = 0
        if nbits:
            out.append(acc << (8 - nbits))
    elif depth == 8:
        for p in px:
            out += bytes(p)
    else:
        for p in px:
            for s in p:
                out += struct.pack(">H", s)
    return bytes(out)


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def filter_row(f, cur, prev, bpp):
    out = bytearray([f])
    for i, x in enumerate(cur):
        a = cur[i - bpp] if i >= bpp else 0
        b = prev[i] if prev is not None else 0
        c = prev[i - bpp] if prev is not None and i >= bpp else 0
        if f == 0:
            v = x
        elif f == 1:
            v = x - a
        elif f == 2:
            v = x - b
        elif f == 3:
            v = x - ((a + b) >> 1)
        else:
            v = x - paeth(a, b, c)
        out.append(v & 255)
    return bytes(out)


def raw_image(pixels, w, h, ctype, depth, interlace, filters, rnd):
    """The rows of every pass, filtered, ready to deflate."""
    bpp = max(1, CHANNELS[ctype] * depth // 8)
    passes = ADAM7 if interlace else ((0, 0, 1, 1),)
    out = bytearray()
    k = 0
    for x0, y0, dx, dy in passes:
        xs = list(range(x0, w, dx))
        ys = list(range(y0, h, dy))
        if not xs or not ys:
            continue
        prev = None
        for y in ys:
            cur = pack_row([pixels[y][x] for x in xs], ctype, depth)
            if filters == "cycle":
                f = k % 5
            elif filters == "random":
                f = rnd.next(5)
            else:
                f = filters
            k += 1
            out += filter_row(f, cur, prev, bpp)
            prev = cur
    return bytes(out)


def argb(pixels, w, h, ctype, depth, plte, trns):
    """What the decoder should give: 0xAARRGGBB words, 16-bit samples taken
    to their high byte, sub-byte grey scaled to 0..255."""
    pal = [0xFF000000] * 256
    if plte is not None:
        for i, (r, g, b) in enumerate(plte[:256]):
            pal[i] = 0xFF000000 | (r << 16) | (g << 8) | b
    key = None
    alpha = ctype in (4, 6)
    if trns is not None:
        if ctype == 3:
            for i, a in enumerate(trns[:256]):
                pal[i] = (pal[i] & 0xFFFFFF) | (a << 24)
            alpha = True
        elif ctype in (0, 2):
            key = trns
            alpha = True
    hi = (lambda v: v >> 8) if depth == 16 else (lambda v: v)
    mul = 255 // ((1 << depth) - 1) if depth < 8 else 1
    rows = []
    for y in range(h):
        row = []
        for x in range(w):
            p = pixels[y][x]
            if ctype == 0:
                g = p[0] * mul if depth < 8 else hi(p[0])
                a = 0 if key is not None and p[0] == key[0] else 255
                v = (a << 24) | (g << 16) | (g << 8) | g
            elif ctype == 2:
                a = 0 if key is not None and tuple(p) == tuple(key) else 255
                v = (a << 24) | (hi(p[0]) << 16) | (hi(p[1]) << 8) | hi(p[2])
            elif ctype == 3:
                v = pal[p[0]]
            elif ctype == 4:
                g = hi(p[0])
                v = (hi(p[1]) << 24) | (g << 16) | (g << 8) | g
            else:
                v = (hi(p[3]) << 24) | (hi(p[0]) << 16) | (hi(p[1]) << 8) | hi(p[2])
            row.append(v)
        rows.append(row)
    return rows, alpha


def fnv(rows):
    h = 0x811C9DC5
    for row in rows:
        for b in b"".join(struct.pack(">I", v) for v in row):
            h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def picture(w, h, ctype, depth, kind, rnd, npal=None):
    """Pixels as tuples of samples: a gradient with a pattern, or noise."""
    top = (1 << depth) - 1
    if ctype == 3:
        top = min(top, (npal or 256) - 1)
    ch = CHANNELS[ctype]
    rows = []
    for y in range(h):
        row = []
        for x in range(w):
            if kind == "noise":
                row.append(tuple(rnd.next(top + 1) for _ in range(ch)))
            elif kind == "photo":               # smooth, with grain: mostly literals and short matches
                row.append(tuple(min(top, max(0, (x * (c + 2) + y * (3 - c) + (x * y >> 7)) % (top + 1)
                                                 + rnd.next(5) - 2)) for c in range(ch)))
            else:
                base = (x * 7 + y * 13 + ((x ^ y) & 3)) * (top + 1) // max(1, (w * 7 + h * 13))
                row.append(tuple(min(top, max(0, (base + c * (top // 3 + 1) + ((x * y) >> c & 1)) % (top + 1)))
                                 for c in range(ch)))
        rows.append(row)
    return rows


def make(name, w, h, ctype, depth, interlace=0, kind="grad", filters="cycle", plte=None, trns=None,
         level=6, strategy=zlib.Z_DEFAULT_STRATEGY, split=None, empty_idat=False, extra=False, seed=1,
         pixels=None, npal=None, plte_after=False):
    rnd = Rand(seed)
    if ctype == 3 and plte is None:
        n = npal or (1 << depth)
        plte = [((i * 37) & 255, (i * 91 + 7) & 255, (255 - i * 5) & 255) for i in range(n)]
    if pixels is None:
        pixels = picture(w, h, ctype, depth, kind, rnd, npal=len(plte) if plte else None)
    raw = raw_image(pixels, w, h, ctype, depth, interlace, filters, rnd)
    co = zlib.compressobj(level, zlib.DEFLATED, 15, 9, strategy)
    z = co.compress(raw) + co.flush()
    out = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, depth, ctype, 0, 0, interlace))
    if extra:
        out += chunk(b"gAMA", struct.pack(">I", 45455)) + chunk(b"tEXt", b"Comment\0made by make-png.py")
    if plte is not None and not plte_after:
        out += chunk(b"PLTE", bytes(c for rgb in plte for c in rgb))
    if trns is not None:
        if ctype == 3:
            out += chunk(b"tRNS", bytes(trns))
        else:
            out += chunk(b"tRNS", b"".join(struct.pack(">H", v) for v in trns))
    if empty_idat:
        out += chunk(b"IDAT", b"")
    step = split or len(z) or 1
    for i in range(0, len(z), step):
        out += chunk(b"IDAT", z[i:i + step])
    if plte is not None and plte_after:
        out += chunk(b"PLTE", bytes(c for rgb in plte for c in rgb))
    if extra:
        out += chunk(b"tEXt", b"After\0the pixels")
    out += chunk(b"IEND", b"")
    rows, alpha = argb(pixels, w, h, ctype, depth, None if plte_after else plte, trns)
    return name, out, rows, alpha


SIZES = ((37, 23), (1, 1), (5, 3), (33, 17), (8, 8), (13, 1), (1, 13), (3, 7), (64, 9), (17, 31))
KINDS = ((0, (1, 2, 4, 8, 16)), (2, (8, 16)), (3, (1, 2, 4, 8)), (4, (8, 16)), (6, (8, 16)))


def cases(big):
    out = []
    n = 0
    for ctype, depths in KINDS:
        for depth in depths:
            for inter in (0, 1):
                w, h = SIZES[n % len(SIZES)]
                n += 1
                tag = f"c{ctype}d{depth}" + ("i" if inter else "")
                out.append(make(tag, w, h, ctype, depth, inter, seed=n))
                out.append(make(tag + "-noise", 37, 23, ctype, depth, inter, kind="noise", filters="random", seed=n))
    # transparency
    for depth in (1, 2, 4, 8, 16):
        out.append(make(f"c0d{depth}-key", 37, 23, 0, depth, trns=[1 % (1 << depth)], seed=depth))
    out.append(make("c2d8-key", 33, 17, 2, 8, trns=[0, 63, 126], kind="noise", seed=4,
                    pixels=[[(0, 63, 126) if (x + y) % 3 == 0 else (x * 7 & 255, y * 9 & 255, 5) for x in range(33)]
                            for y in range(17)]))
    out.append(make("c2d16-key", 9, 7, 2, 16, trns=[300, 600, 900],
                    pixels=[[(300, 600, 900) if (x ^ y) & 1 else (300, 600, 901 + x) for x in range(9)] for y in range(7)]))
    out.append(make("c3d8-trns", 37, 23, 3, 8, trns=list(range(0, 250, 3))[:70], npal=200))
    out.append(make("c3d4-trns-short", 37, 23, 3, 4, trns=[0, 128], interlace=1))
    out.append(make("c3d8-pastpal", 16, 16, 3, 8, plte=[(1, 2, 3), (4, 5, 6)],
                    pixels=[[((x * 16 + y) & 255,) for x in range(16)] for y in range(16)]))
    out.append(make("c3d8-pastpal-trns", 16, 16, 3, 8, plte=[(1, 2, 3), (4, 5, 6)], trns=[10, 20, 30, 40],
                    pixels=[[((x * 16 + y) & 255,) for x in range(16)] for y in range(16)]))
    # deflate and chunks
    out.append(make("stored", 37, 23, 6, 8, level=0, kind="noise"))
    out.append(make("stored-big", 300, 200, 2, 8, level=0, kind="noise", filters=0))
    out.append(make("fixed", 64, 9, 2, 8, strategy=zlib.Z_FIXED))
    out.append(make("fixed-noise", 64, 9, 6, 8, strategy=zlib.Z_FIXED, kind="noise"))
    out.append(make("level1", 100, 50, 2, 8, level=1))
    out.append(make("level9", 100, 50, 6, 8, level=9, kind="noise", filters="random"))
    out.append(make("rle", 100, 50, 0, 8, strategy=zlib.Z_RLE))
    out.append(make("huffonly", 100, 50, 0, 8, strategy=zlib.Z_HUFFMAN_ONLY, kind="noise"))
    out.append(make("split1", 37, 23, 2, 8, split=1, extra=True, empty_idat=True))
    out.append(make("split7i", 37, 23, 6, 16, interlace=1, split=7))
    out.append(make("extra", 37, 23, 3, 2, extra=True))
    out.append(make("plte-after", 8, 8, 3, 8, plte_after=True))   # a PLTE after IDAT counts for nothing
    out.append(make("wide", 1000, 3, 2, 8, kind="noise", filters="random"))
    out.append(make("tall", 2, 700, 6, 8, filters="random"))
    for f in range(5):
        out.append(make(f"filter{f}", 31, 9, 6, 8, filters=f, kind="noise"))
        out.append(make(f"filter{f}-g1", 31, 9, 0, 1, filters=f, kind="noise"))
    if big:
        out.append(make("big-rgb", 1024, 768, 2, 8, kind="photo", filters="random"))
        out.append(make("big-rgba", 1024, 768, 6, 8, kind="photo", filters="random"))
        out.append(make("big-rgb-flat", 1024, 768, 2, 8, filters="random"))
        out.append(make("big-g1", 1024, 768, 0, 1, filters="random", seed=9))
        out.append(make("big-p4", 1024, 768, 3, 4, filters="random", seed=10))
        out.append(make("big-rgb-i", 1024, 768, 2, 8, kind="photo", interlace=1, filters="random", seed=11))
    return out


def refused():
    """Files png_read_head() must refuse."""
    good = make("x", 4, 4, 2, 8)[1]
    sig_bad = b"\x89PNH" + good[4:]
    def ihdr(w, h, depth, ctype, comp=0, filt=0, inter=0):
        body = struct.pack(">IIBBBBB", w, h, depth, ctype, comp, filt, inter)
        return good[:8] + chunk(b"IHDR", body) + good[33:]
    no_idat = good[:33] + chunk(b"IEND", b"")
    return [("bad-sig", sig_bad), ("zero-width", ihdr(0, 4, 8, 2)), ("depth3", ihdr(4, 4, 3, 0)),
            ("rgb-depth4", ihdr(4, 4, 4, 2)), ("ctype5", ihdr(4, 4, 8, 5)), ("interlace2", ihdr(4, 4, 8, 2, inter=2)),
            ("compression1", ihdr(4, 4, 8, 2, comp=1)), ("huge", ihdr(40000, 4, 8, 2)), ("no-idat", no_idat),
            ("short", good[:30])]


def pillow_check(path, ctype, depth, rows, trns):
    from PIL import Image
    if (ctype == 0 and depth == 16) or (trns is not None and ctype in (0, 2) and depth != 8):
        return False
    im = Image.open(path)
    im.load()
    got = im.convert("RGBA").tobytes()
    want = b"".join(struct.pack(">I", v) for row in rows for v in row)
    want = b"".join(want[i + 1:i + 4] + want[i:i + 1] for i in range(0, len(want), 4))   # ARGB to RGBA
    if got != want:
        sys.exit(f"{path}: Pillow reads other pixels")
    return True


def main():
    out = sys.argv[1]
    pillow = "--pillow" in sys.argv
    big = "--big" in sys.argv
    if pillow:
        try:
            import PIL  # noqa: F401
        except ImportError:
            print("make-png.py: Pillow not installed, no check against it")
            pillow = False
    os.makedirs(out, exist_ok=True)
    lines, damage, checked = [], [], 0
    for name, data, rows, alpha in cases(big):
        path = os.path.join(out, name + ".png")
        open(path, "wb").write(data)
        lines.append(f"{name} {len(rows[0])} {len(rows)} alpha={int(alpha)} fnv={fnv(rows):08x}")
        if not name.startswith("big") and len(data) < 20000:
            damage.append(name)
        if pillow:
            ihdr = struct.unpack(">IIBBBBB", data[16:29])
            trns = None if b"tRNS" not in data else True
            checked += pillow_check(path, ihdr[3], ihdr[2], rows, trns)
    for name, data in refused():
        open(os.path.join(out, name + ".png"), "wb").write(data)
        lines.append(f"{name} invalid")
    open(os.path.join(out, "expected.txt"), "w").write("\n".join(lines) + "\n")
    open(os.path.join(out, "damage.txt"), "w").write("\n".join(damage) + "\n")
    print(f"make-png.py: {len(lines)} files" + (f", {checked} read the same by Pillow" if pillow else ""))


if __name__ == "__main__":
    main()
