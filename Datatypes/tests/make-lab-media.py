#!/usr/bin/env python3
"""The files a lab checks the writers with (tests/dtsave, tests/dtanim),
made here with Python alone so a build can carry them into a lab: PNGs
(true colour with alpha, opaque, and 16 colours), a gzip file, and, with
Pillow there, a lossless WebP. tests/make-gif.py, make-tga.py and
make-cdxl.py make the GIFs, TGAs and CDXL films beside them.

usage: make-lab-media.py OUTDIR

MIT, Copyright (c) 2026 AmiCradle Team.
"""
import gzip
import os
import struct
import sys
import zlib


def png(path, w, h, pixel, alpha):
    rows = bytearray()
    for y in range(h):
        rows.append(0)
        for x in range(w):
            r, g, b, a = pixel(x, y)
            rows += bytes((r, g, b, a) if alpha else (r, g, b))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    head = struct.pack(">IIBBBBB", w, h, 8, 6 if alpha else 2, 0, 0, 0)
    with open(path, "wb") as fh:
        fh.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", head) + chunk(b"IDAT", zlib.compress(bytes(rows), 9))
                 + chunk(b"IEND", b""))


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    out = argv[1]
    os.makedirs(out, exist_ok=True)
    png(os.path.join(out, "alpha.png"), 96, 64,
        lambda x, y: (x * 255 // 95, y * 255 // 63, (x + y) * 7 & 255, (x * 13 + y * 29) & 255), True)
    png(os.path.join(out, "opaque.png"), 160, 100,
        lambda x, y: (x * 255 // 159, y * 255 // 99, (x ^ y) & 255, 255), False)
    pal = [(0, 0, 0), (255, 255, 255), (200, 30, 30), (30, 200, 30), (30, 30, 200), (240, 200, 40),
           (90, 60, 30), (128, 128, 128), (255, 128, 0), (0, 160, 160), (160, 0, 160), (60, 60, 60),
           (180, 220, 255), (255, 180, 200), (20, 90, 20), (250, 250, 170)]
    png(os.path.join(out, "sixteen.png"), 120, 80, lambda x, y: pal[(x // 8 + y // 8) % 16] + (255,), False)
    text = "".join("Line %d of the gzip test: the Amiga deflates it again.\n" % i for i in range(400))
    with open(os.path.join(out, "text.txt"), "w") as fh:
        fh.write(text)
    with open(os.path.join(out, "text.gz"), "wb") as fh:
        fh.write(gzip.compress(text.encode("ascii"), 9, mtime=0))
    try:
        from PIL import Image
        im = Image.new("RGBA", (64, 48))
        im.putdata([(x * 4, y * 5, (x + y) * 3 & 255, (x * 7 + y * 11) & 255) for y in range(48) for x in range(64)])
        im.save(os.path.join(out, "lossless.webp"), "WEBP", lossless=True)
    except ImportError:
        print("Pillow not installed: no WebP")
    print("lab media in", out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
