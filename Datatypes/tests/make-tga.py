#!/usr/bin/env python3
"""Make TGA test pictures of every kind gfopentga.datatype reads, with no
downloads: every byte is written here, from Truevision's TGA File Format
Specification 2.0, and the picture each should decode to is worked out
here too.

    make-tga.py OUTDIR [--pillow]

writes OUTDIR/*.tga and OUTDIR/expected.txt, a line for each file in the
form tests/dtpic.c prints on the Amiga:

    NAME WIDTH HEIGHT alpha=0|1 CHECKSUM      (or NAME invalid)

and OUTDIR/expected-host.txt, the same with fnv=HASH, a hash of every byte
of the picture, as tests/tgatest.c prints on x86 or ARM64 cores.

The kinds: true-colour 15, 16 (with and without its alpha bit), 24 and 32
bits (alpha, no alpha bits, an empty alpha channel, a TGA 2.0 extension
area saying "no alpha", "useful alpha" or "premultiplied"); colour-mapped
with 8 and 16-bit indexes and 15, 16, 24 and 32-bit colour maps (and a map
that starts at index 16); greyscale 8 bits, and 16 with alpha; each also
run-length encoded, with packets that cross the end of a row and packets
that don't, longer than 128 pixels; rows bottom-up, top-down and right to
left; an image ID, a colour map in a true-colour file, a short file, and
headers that are not TGAs.

With --pillow, each picture Pillow (the Python imaging library, when it is
installed) also reads is checked against it: Pillow decodes these files as
this script says they should decode.

MIT, Copyright (c) 2026 AmigaChrome Team.
"""
import os
import struct
import sys

W, H = 37, 23                      # odd sizes: rows of odd length, no power of two
WIDE_W, WIDE_H = 300, 9            # runs longer than 128 pixels, and across rows


def grad(w=W, h=H):
    """Rows top first of (a, r, g, b), with some flat stretches for runs."""
    rows = []
    for y in range(h):
        row = []
        for x in range(w):
            r = (x * 255) // (w - 1)
            g = (y * 255) // (h - 1)
            b = ((x + y) * 7) & 255
            a = (x * 13 + y * 29) & 255
            if 10 <= x < 22 and y % 3 == 0:          # a run inside a row
                r, g, b, a = 40, 200, 90, 255
            if x >= w - 4 or (x < 3 and y % 2):      # runs that reach the next row
                r, g, b, a = 250, 250, 10, 128
            row.append((a, r, g, b))
        rows.append(row)
    return rows


def wide():
    rows = []
    for y in range(WIDE_H):
        row = []
        for x in range(WIDE_W):
            if (x // 50 + y) % 3 == 0:
                row.append((255, (x * 3) & 255, y * 20, 7))
            else:
                row.append((200, 10, 120, 240))      # long flat stretches
        rows.append(row)
    return rows


def grey_rows(w=W, h=H):
    """(alpha, grey) pairs, top row first."""
    return [[((x * 13 + y * 29) & 255, 30 if 10 <= x < 22 and y % 3 == 0 else (x * 7 + y * 11) & 255)
             for x in range(w)] for y in range(h)]


def palette(n, with_alpha=True):
    return [(((i * 37) & 255) if with_alpha else 255, (i * 53) & 255, (i * 97 + 11) & 255, (255 - i * 29) & 255)
            for i in range(n)]


def index_rows(n, w=W, h=H):
    return [[(3 if 10 <= x < 22 and y % 3 == 0 else (x // 3 + (y // 2) * 13 + x * y) % n) for x in range(w)]
            for y in range(h)]


def widen5(c):
    """An 8-bit value through 5 bits and back, as the decoder widens it."""
    c >>= 3
    return (c << 3) | (c >> 2)


def store(c, depth, alpha, junk=False):
    """A colour (a, r, g, b) as stored at 15, 16, 24 or 32 bits."""
    a, r, g, b = c
    if depth == 24:
        return bytes((b, g, r))
    if depth == 32:
        return bytes((b, g, r, a))
    v = ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)
    if (depth == 16 and alpha and a >= 128) or (not (depth == 16 and alpha) and junk and (r ^ b) & 1):
        v |= 0x8000                     # the alpha bit, or a stray top bit the decoder must ignore
    return struct.pack("<H", v)


def seen(c, depth, alpha):
    """What the decoder makes of a stored colour."""
    a, r, g, b = c
    if depth == 24:
        return (255, r, g, b)
    if depth == 32:
        return (a if alpha else 255, r, g, b)
    return ((255 if a >= 128 else 0) if (alpha and depth == 16) else 255, widen5(r), widen5(g), widen5(b))


def premultiply(c):
    a, r, g, b = c
    return (a, r * a // 255, g * a // 255, b * a // 255)


def unpremultiply(c):
    a, r, g, b = c
    if a in (0, 255):
        return c
    return (a, min(255, r * 255 // a), min(255, g * 255 // a), min(255, b * 255 // a))


def in_file_order(rows, top_down, right_left):
    """The pixels in the order the file holds them."""
    out = []
    for row in (rows if top_down else rows[::-1]):
        out.extend(row[::-1] if right_left else row)
    return out


def rle(units, width, cross_rows):
    """Run-length packets: a run of up to 128 equal pixels, or up to 128 raw
    ones; with cross_rows False no packet goes past the end of a row."""
    out = bytearray()
    i, n = 0, len(units)
    while i < n:
        limit = min(n if cross_rows else (i // width + 1) * width, i + 128)
        j = i + 1
        while j < limit and units[j] == units[i]:
            j += 1
        if j - i >= 2:
            out.append(0x80 | (j - i - 1))
            out += units[i]
        else:
            j = i + 1
            while j < limit and not (j + 1 < limit and units[j] == units[j + 1]):
                j += 1
            out.append(j - i - 1)
            out += b"".join(units[i:j])
        i = j
    return bytes(out)


def extension(offset, attr):
    """A TGA 2.0 extension area saying ATTR (byte 494), and the footer."""
    ext = bytearray(495)
    ext[0:2] = struct.pack("<H", 495)
    ext[494] = attr
    return bytes(ext) + struct.pack("<II", offset, 0) + b"TRUEVISION-XFILE.\0"


def checksum(rows, alpha):
    """As tests/dtpic.c: ARGB words top row first; no alpha counts as opaque."""
    s = 0
    for row in rows:
        for a, r, g, b in row:
            s = (s * 31 + (((a if alpha else 255) << 24) | (r << 16) | (g << 8) | b)) & 0xFFFFFFFF
    return s


def fnv(rows, alpha):
    """FNV-1a over the A, R, G, B bytes, as tests/tgatest.c: every byte counts
    alike (dtpic's sum sees alpha in its top byte only)."""
    h = 0x811C9DC5
    for row in rows:
        for a, r, g, b in row:
            for v in (a if alpha else 255, r, g, b):
                h = ((h ^ v) * 0x01000193) & 0xFFFFFFFF
    return h


def make(rows, kind, depth, alpha_bits=0, rle_mode=None, top_down=False, right_left=False, pal=None,
         map_depth=0, map_first=0, attr=None, idtext=b"", stray_map=False, cut=None, premul=False):
    """One TGA file: (its bytes, the rows it decodes to, top first, and
    whether they have alpha). kind 1 colour-mapped (rows of indexes), 2
    true-colour (rows of (a, r, g, b)), 3 greyscale (rows of (a, grey));
    rle_mode None, "rows" (packets stop at each row's end) or "cross"."""
    h, w = len(rows), len(rows[0])
    bits = map_depth if kind == 1 else depth
    alpha = bool(alpha_bits) and bits in (16, 32)
    if attr is not None and attr <= 2:
        alpha = False
    elif attr in (3, 4) and bits in (16, 32):
        alpha = True
    cmtype, map_len, map_bytes = 0, 0, b""
    order = in_file_order(rows, top_down, right_left)
    if kind == 1:
        cmtype, map_len = 1, len(pal)
        map_bytes = b"".join(store(c, map_depth, alpha, junk=True) for c in pal)
        shown = [seen(c, map_depth, alpha) for c in pal]
        units = [bytes((i + map_first,)) if depth == 8 else struct.pack("<H", i + map_first) for i in order]
        expected = [[shown[i] for i in row] for row in rows]
    elif kind == 2:
        stored = [[premultiply(c) if premul else c for c in row] for row in rows]
        units = [store(c, depth, alpha, junk=True) for c in in_file_order(stored, top_down, right_left)]
        expected = [[seen(c, depth, alpha) for c in row] for row in stored]
        if premul and alpha:
            expected = [[unpremultiply(c) for c in row] for row in expected]
        if stray_map:                   # a colour map in a true-colour file: skipped
            cmtype, map_len, map_depth = 1, 4, 24
            map_bytes = bytes(range(12))
    else:
        units = [bytes((g,)) if depth == 8 else bytes((g, a)) for a, g in order]
        expected = [[(a if alpha else 255, g, g, g) for a, g in row] for row in rows]
    data = rle(units, w, rle_mode == "cross") if rle_mode else b"".join(units)
    desc = alpha_bits | (0x10 if right_left else 0) | (0x20 if top_down else 0)
    head = struct.pack("<BBBHHBHHHHBB", len(idtext), cmtype, kind + (8 if rle_mode else 0), map_first, map_len,
                       map_depth, 0, 0, w, h, depth, desc)
    body = head + idtext + map_bytes + data
    if cut is not None:                 # a short file (uncompressed): the rest is opaque black
        body = body[:cut]
        whole = (len(body) - len(head) - len(idtext) - len(map_bytes)) // ((depth + 7) // 8)
        for k in range(max(whole, 0), w * h):
            sr, c = divmod(k, w)
            expected[sr if top_down else h - 1 - sr][w - 1 - c if right_left else c] = (255, 0, 0, 0)
    if attr is not None:
        body += extension(len(body), attr)
    if alpha and not any(c[0] for row in expected for c in row):
        alpha = False                   # an empty alpha channel is no alpha
        expected = [[(255,) + c[1:] for c in row] for row in expected]
    return body, expected, alpha


def cases():
    """(name, bytes, expected rows or None for a file that must be refused,
    alpha, whether Pillow 12 reads it as the specification says). Pillow
    reads no 15-bit pictures or colour maps, no 32-bit colour maps and no
    16-bit indexes; it takes a 16-bit pixel's top bit, and a 32-bit pixel's
    fourth byte, as they are, whatever the alpha bits and the extension
    area say; it does not undo premultiplied alpha; it decodes a row at a
    time, so packets that cross rows (version 1 files may have them) are
    past it; and it refuses a short file and a colour map in a true-colour
    file."""
    g, wd, gr = grad(), wide(), grey_rows()
    zero = [[(0,) + c[1:] for c in row] for row in g]
    out = []

    def add(name, pillow=True, **kw):
        rows = kw.pop("rows", g)
        data, exp, alpha = make(rows, **kw)
        out.append((name, data, exp, alpha, pillow and kw.get("rle_mode") != "cross"))

    for rl, tag in ((None, ""), ("rows", "-rle"), ("cross", "-rlex")):
        add(f"tc24{tag}", kind=2, depth=24, rle_mode=rl)
        add(f"tc32a{tag}", kind=2, depth=32, alpha_bits=8, rle_mode=rl)
        add(f"tc16a{tag}", kind=2, depth=16, alpha_bits=1, rle_mode=rl, pillow=False)
        add(f"tc15{tag}", kind=2, depth=15, rle_mode=rl, pillow=False)
        add(f"cm8-24{tag}", kind=1, depth=8, rows=index_rows(200), pal=palette(200), map_depth=24, rle_mode=rl)
        add(f"cm8-32a{tag}", kind=1, depth=8, rows=index_rows(256), pal=palette(256), map_depth=32, alpha_bits=8,
            rle_mode=rl, pillow=False)
        add(f"grey8{tag}", kind=3, depth=8, rows=gr, rle_mode=rl)
        add(f"grey16a{tag}", kind=3, depth=16, alpha_bits=8, rows=gr, rle_mode=rl)
        add(f"wide32a{tag}", kind=2, depth=32, alpha_bits=8, rows=wd, rle_mode=rl)
    # Rows from the top, right to left, both; RLE too.
    for td, rl_, tag in ((True, False, "td"), (False, True, "rl"), (True, True, "tdrl")):
        add(f"tc32a-{tag}", kind=2, depth=32, alpha_bits=8, top_down=td, right_left=rl_)
        add(f"tc24-{tag}-rlex", kind=2, depth=24, top_down=td, right_left=rl_, rle_mode="cross")
        add(f"cm8-24-{tag}", kind=1, depth=8, rows=index_rows(200), pal=palette(200), map_depth=24,
            top_down=td, right_left=rl_)
        add(f"grey8-{tag}-rle", kind=3, depth=8, rows=gr, top_down=td, right_left=rl_, rle_mode="rows")
    # 16-bit pixels without the alpha bit, 32 without alpha bits, an empty alpha channel.
    add("tc16", kind=2, depth=16, pillow=False)
    add("tc32-noalphabits", kind=2, depth=32, pillow=False)
    add("tc32a-empty", kind=2, depth=32, alpha_bits=8, rows=zero, pillow=False)
    # Colour maps: 15 and 16-bit entries, a map that starts at 16, 16-bit indexes.
    add("cm8-15", kind=1, depth=8, rows=index_rows(64), pal=palette(64), map_depth=15, pillow=False)
    add("cm8-16a", kind=1, depth=8, rows=index_rows(64), pal=palette(64), map_depth=16, alpha_bits=1, pillow=False)
    add("cm8-24-first16", kind=1, depth=8, rows=index_rows(100), pal=palette(100), map_depth=24, map_first=16)
    add("cm16-24", kind=1, depth=16, rows=index_rows(300), pal=palette(300), map_depth=24, pillow=False)
    add("cm16-24-rle", kind=1, depth=16, rows=index_rows(300), pal=palette(300), map_depth=24, rle_mode="cross", pillow=False)
    # TGA 2.0 extension areas: no alpha, useful alpha (without alpha bits), premultiplied.
    add("tc32-ext-noalpha", kind=2, depth=32, alpha_bits=8, attr=0, pillow=False)
    add("tc32-ext-useful", kind=2, depth=32, attr=3)
    add("tc32-ext-premul", kind=2, depth=32, alpha_bits=8, attr=4, premul=True, pillow=False)
    # An image ID, a colour map in a true-colour file, a short file.
    add("tc24-id", kind=2, depth=24, idtext=b"opentga test picture\0")
    add("tc24-straymap", kind=2, depth=24, stray_map=True, pillow=False)
    add("tc24-short", kind=2, depth=24, cut=18 + W * 3 * 10 + 5, pillow=False)
    # Not TGAs it reads: type 0, the Huffman type 32, no width, a colour map
    # type 2, a colour-mapped picture without its map, 7-bit pixels, too short.
    bad = {"bad-type0": (0, 0, 0, 0, W, H, 24, 0), "bad-type32": (0, 0, 32, 0, W, H, 24, 0),
           "bad-nowidth": (0, 0, 2, 0, 0, H, 24, 0), "bad-cmtype2": (0, 2, 2, 0, W, H, 24, 0),
           "bad-cm-nomap": (0, 0, 1, 0, W, H, 8, 0), "bad-depth7": (0, 0, 2, 0, W, H, 7, 0)}
    for name, (idl, cmt, typ, ml, w, h, dep, dsc) in bad.items():
        out.append((name, struct.pack("<BBBHHBHHHHBB", idl, cmt, typ, 0, ml, 0, 0, 0, w, h, dep, dsc) + bytes(64),
                    None, False, False))
    out.append(("bad-short", b"\0\0\2\0\0\0\0", None, False, False))
    return out


def pillow_rows(path):
    from PIL import Image
    with Image.open(path) as im:
        im = im.convert("RGBA")
        w, h = im.size
        px = im.load()
        return [[(px[x, y][3],) + tuple(px[x, y][:3]) for x in range(w)] for y in range(h)]


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    out_dir, use_pillow = argv[1], "--pillow" in argv[2:]
    os.makedirs(out_dir, exist_ok=True)
    if use_pillow:
        try:
            import PIL  # noqa: F401
        except ImportError:
            print("Pillow is not installed: no cross-check")
            use_pillow = False
    lines, host, agreed, failed = [], [], 0, []
    for name, data, expected, alpha, pillow in cases():
        path = os.path.join(out_dir, name + ".tga")
        with open(path, "wb") as f:
            f.write(data)
        if expected is None:
            lines.append(f"{name}.tga invalid")
            host.append(lines[-1])
            continue
        lines.append(f"{name}.tga {len(expected[0])} {len(expected)} alpha={int(alpha)} {checksum(expected, alpha):08x}")
        host.append(f"{lines[-1]} fnv={fnv(expected, alpha):08x}")
        if use_pillow and pillow:
            want = [[(c[0] if alpha else 255,) + c[1:] for c in row] for row in expected]
            try:
                got = pillow_rows(path)
            except Exception as exc:    # a kind Pillow does not read
                got = f"unreadable ({exc})"
            if got == want:
                agreed += 1
            else:
                failed.append(name if isinstance(got, list) else f"{name} {got}")
    with open(os.path.join(out_dir, "expected.txt"), "w") as f:          # tests/dtpic.c on the Amiga
        f.write("\n".join(lines) + "\n")
    with open(os.path.join(out_dir, "expected-host.txt"), "w") as f:     # tests/tgatest.c
        f.write("\n".join(host) + "\n")
    print(f"{len(lines)} files in {out_dir}")
    if use_pillow:
        print(f"Pillow reads {agreed} of them as expected" + (f"; differs on {', '.join(failed)}" if failed else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
