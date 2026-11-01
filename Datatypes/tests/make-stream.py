#!/usr/bin/env python3
"""Test files for stream.datatype's decoders (tests/test-stream.sh).

usage: make-stream.py OUTDIR [VECTORS]

Writes OUTDIR/cases.txt, a line a case:

    NAME CODEC WRAPPER EXPECT

NAME is the coded file in OUTDIR, CODEC gzip or brotli, WRAPPER gzip's
(0 auto, 1 gzip, 2 zlib, 3 raw; 0 for brotli), EXPECT the file of plain
bytes it must decode to, or "error".

gzip, zlib and raw deflate come from Python's own gzip and zlib modules,
at every level and window, with gzip's optional header fields, several
members, trailing bytes, and damaged and cut-short streams.

Brotli comes from three places:
- Python's brotli module, when installed (Google's encoder): every quality,
  windows of 2^10 to 2^24, its three modes, and flushed streams;
- streams written here, bit by bit, from RFC 7932 alone: every dictionary
  word length with every one of the 121 transforms, uncompressed and
  metadata meta-blocks, simple prefix codes of one to four symbols (both
  shapes of four), every window size. With the brotli module installed,
  its decoder must agree with what is expected here;
- VECTORS (tests/brotli-vectors.txt), small streams kept in the repository
  with what they decode to, so the test runs without the module.

MIT, Copyright (c) 2026 AmiCradle Team.
SPDX-License-Identifier: MIT
"""
import gzip
import io
import os
import random
import struct
import sys
import zlib

try:
    import brotli
except ImportError:
    brotli = None

# ---- the plain data --------------------------------------------------------

WORDS = ("the of and to in is that for it as was with be by on not he this are or his from at "
         "which but have an they you were her she there been one all we their has would when if "
         "so no more what up out about who into than them can only other new some could these "
         "time two may then do first any my now such like our over man me even most made after "
         "also did many before must through back years where much your way well down should "
         "because each just those people how too little state good very make world still own "
         "see men work long get here between both life being under never day same another know "
         "while last might us great old year off come since against go came right used take "
         "Amiga datatype window picture brotli gzip deflate stream HTTP body").split()


def text(rnd, n):
    out, line = [], 0
    while sum(map(len, out)) < n:
        w = rnd.choice(WORDS)
        if rnd.random() < 0.08:
            w = w.capitalize()
        out.append(w + (".\n" if rnd.random() < 0.05 else " "))
    return "".join(out).encode()[:n]


def html(rnd, n):
    parts = []
    while sum(map(len, parts)) < n:
        tag = rnd.choice(["div", "span", "p", "a", "li", "td"])
        cls = rnd.choice(["nav", "item", "main", "footer", "content", "title"])
        parts.append(f'<{tag} class="{cls}" id="x{rnd.randrange(1000)}">{text(rnd, rnd.randrange(10, 80)).decode()}</{tag}>\n')
    return "".join(parts).encode()[:n]


def utf8(rnd, n):
    s = "Grüße aus Köln, ½ € 東京 — naïve façade, Ελληνικά, русский текст, 日本語のテキスト "
    out = []
    while sum(len(x.encode()) for x in out) < n:
        out.append(rnd.choice(s.split(" ")) + " ")
    return "".join(out).encode()[:n]


def binary(rnd, n):
    b = bytearray()
    v = 0
    while len(b) < n:
        v = (v + rnd.randrange(-300, 300)) & 0xffff
        b += struct.pack(">hH", v - 32768, rnd.randrange(16))
    return bytes(b[:n])


def picture(n):
    return bytes(((x * 7) ^ (x >> 9) * 3) & 0xff for x in range(n))


def datasets():
    rnd = random.Random(7932)
    here = os.path.dirname(os.path.abspath(__file__))
    readme = open(os.path.join(here, "..", "README.md"), "rb").read()
    d = {
        "empty": b"",
        "one": b"x",
        "short": b"Hello, Amiga!\n",
        "text": text(rnd, 60000),
        "html": html(rnd, 80000),
        "utf8": utf8(rnd, 30000),
        "binary": binary(rnd, 70000),
        "picture": picture(90000),
        "random": bytes(rnd.randrange(256) for _ in range(40000)),
        "aaaa": b"a" * 100000,
        "readme": readme,
    }
    d["big"] = (d["text"] + d["binary"] + d["html"]) * 6 + bytes(rnd.randrange(256) for _ in range(300000))
    return d


# ---- a Brotli writer, from RFC 7932 ---------------------------------------------

class Bits:
    def __init__(self):
        self.out = bytearray()
        self.acc = 0
        self.n = 0

    def put(self, v, n):
        for i in range(n):
            self.acc |= ((v >> i) & 1) << self.n
            self.n += 1
            if self.n == 8:
                self.out.append(self.acc)
                self.acc = self.n = 0

    def align(self):
        if self.n:
            self.put(0, 8 - self.n)

    def code(self, c, length):          # a prefix code: its first bit first
        for i in range(length - 1, -1, -1):
            self.put((c >> i) & 1, 1)

    def done(self):
        self.align()
        return bytes(self.out)


def canonical(lengths):
    """{symbol: (code, length)} from {symbol: length} (RFC 7932, 3.2)."""
    out, code = {}, 0
    for l in range(1, 16):
        for s in sorted(k for k, v in lengths.items() if v == l):
            out[s] = (code, l)
            code += 1
        code <<= 1
    return out


def simple_code(bw, alpha, syms, select=0):
    """A simple prefix code (3.4); the {symbol: (code, length)} it gives."""
    abits = max(1, (alpha - 1).bit_length())
    bw.put(1, 2)
    bw.put(len(syms) - 1, 2)
    for s in syms:
        bw.put(s, abits)
    if len(syms) == 1:
        return {syms[0]: (0, 0)}
    if len(syms) == 2:
        lens = [1, 1]
    elif len(syms) == 3:
        lens = [1, 2, 2]
    else:
        bw.put(select, 1)
        lens = [1, 2, 3, 3] if select else [2, 2, 2, 2]
    return canonical(dict(zip(syms, lens)))


def wbits_header(bw, w):
    if w == 16:
        bw.put(0, 1)
    elif w == 17:
        bw.put(1, 1)
        bw.put(0, 3)
        bw.put(0, 3)
    elif w > 17:
        bw.put(1, 1)
        bw.put(w - 17, 3)
    else:
        bw.put(1, 1)
        bw.put(0, 3)
        bw.put(w - 8, 3)


def mb_header(bw, mlen, uncompressed=False):
    bw.put(0, 1)                        # ISLAST
    nib = 4 if mlen - 1 < 1 << 16 else 5 if mlen - 1 < 1 << 20 else 6
    bw.put(nib - 4, 2)
    bw.put(mlen - 1, 4 * nib)
    bw.put(1 if uncompressed else 0, 1)


def last_empty(bw):
    bw.put(1, 1)
    bw.put(1, 1)


INS = [(0, 0), (0, 1), (0, 2), (0, 3), (0, 4), (0, 5), (1, 6), (1, 8), (2, 10), (2, 14), (3, 18), (3, 26),
       (4, 34), (4, 50), (5, 66), (5, 98), (6, 130), (7, 194), (8, 322), (9, 578), (10, 1090), (12, 2114),
       (14, 6210), (24, 22594)]
CPY = [(0, 2), (0, 3), (0, 4), (0, 5), (0, 6), (0, 7), (0, 8), (0, 9), (1, 10), (1, 12), (2, 14), (2, 18),
       (3, 22), (3, 30), (4, 38), (4, 54), (5, 70), (5, 102), (6, 134), (7, 198), (8, 326), (9, 582),
       (10, 1094), (24, 2118)]
NDBITS = [0, 0, 0, 0, 10, 10, 11, 11, 10, 10, 10, 10, 10, 9, 9, 8, 7, 7, 8, 7, 7, 6, 6, 5, 5]


def lencode(table, v):
    for i in range(len(table) - 1, -1, -1):
        if v >= table[i][1]:
            return i, v - table[i][1], table[i][0]


def command_symbol(ic, cc, implicit):
    cells = [(0, 0), (0, 8), (0, 0), (0, 8), (8, 0), (8, 8), (0, 16), (16, 0), (8, 16), (16, 8), (16, 16)]
    for cell, (ib, cb) in enumerate(cells):
        if (cell < 2) != implicit:
            continue
        if ib <= ic < ib + 8 and cb <= cc < cb + 8:
            return 64 * cell + ((ic - ib) << 3) + (cc - cb)
    raise ValueError("no cell")


def compressed_header(bw, cmode=0):
    bw.put(0, 1)                        # NBLTYPESL = 1
    bw.put(0, 1)                        # NBLTYPESI = 1
    bw.put(0, 1)                        # NBLTYPESD = 1
    bw.put(0, 2)                        # NPOSTFIX
    bw.put(0, 4)                        # NDIRECT
    bw.put(cmode, 2)                    # the one literal context mode
    bw.put(0, 1)                        # NTREESL = 1
    bw.put(0, 1)                        # NTREESD = 1


def dict_distance(d):
    for x in range(48):
        nbits = 1 + (x >> 1)
        offset = ((2 + (x & 1)) << nbits) - 4
        if offset + 1 <= d <= offset + (1 << nbits):
            return 16 + x, nbits, d - offset - 1
    raise ValueError("distance too far")


def transforms():
    """Appendix B, as brotli/mkbrdict.py types it in."""
    import importlib.util
    sys.dont_write_bytecode = True
    here = os.path.dirname(os.path.abspath(__file__))
    spec = importlib.util.spec_from_file_location("mkbrdict", os.path.join(here, "..", "brotli", "mkbrdict.py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m.TRANSFORMS


TRANSFORMS = transforms()


def transform(word, t):
    """What transform t makes of a dictionary word (for the expected output
    when Python's brotli module is not there)."""
    p, k, s = TRANSFORMS[t]
    w = bytearray(word)
    if 1 <= k <= 9:
        w = w[:max(0, len(w) - k)]
    elif 12 <= k <= 20:
        w = w[k - 11:]

    def up(i):
        if w[i] < 0xc0:
            if 97 <= w[i] <= 122:
                w[i] ^= 32
            return 1
        if w[i] < 0xe0:
            if i + 1 < len(w):
                w[i + 1] ^= 32
            return 2
        if i + 2 < len(w):
            w[i + 2] ^= 5
        return 3
    if k == 10 and w:
        up(0)
    elif k == 11:
        i = 0
        while i < len(w):
            i += up(i)
    return p.encode("latin-1") + bytes(w) + s.encode("latin-1")


def dict_word(dictionary, length, idx):
    off = sum(l << NDBITS[l] for l in range(4, length))
    return dictionary[off + idx * length: off + (idx + 1) * length]


def synth_dictionary(dictionary, length, wbits=16):
    """One meta-block a word: every transform of a few words of this length,
    each a copy whose distance points into the dictionary."""
    bw, plain = Bits(), bytearray()
    wbits_header(bw, wbits)
    window = (1 << wbits) - 16
    n = 1 << NDBITS[length]
    for t in range(121):
        for idx in sorted({0, n - 1, (t * 37) % n}):
            word = dict_word(dictionary, length, idx) if dictionary else None
            out = transform(word, t) if word is not None else None
            mlen = len(out) if out is not None else None
            if mlen == 0:
                continue
            if mlen is None:
                return None, None
            cc, cextra, cbits = lencode(CPY, length)
            sym = command_symbol(0, cc, False)
            maxd = min(len(plain), window)
            d = maxd + 1 + (idx | (t << NDBITS[length]))
            dcode, dbits, dextra = dict_distance(d)
            mb_header(bw, mlen)
            compressed_header(bw)
            simple_code(bw, 256, [0])
            simple_code(bw, 704, [sym])
            simple_code(bw, 64, [dcode])
            bw.put(cextra, cbits)
            bw.put(dextra, dbits)
            plain += out
    last_empty(bw)
    return bw.done(), bytes(plain)


def synth_literals(rnd, wbits=16):
    """Literal-only meta-blocks with simple codes of one to four symbols
    (both shapes of four), each mode, then uncompressed and metadata
    meta-blocks between them."""
    bw, plain = Bits(), bytearray()
    wbits_header(bw, wbits)
    shapes = [(1, 0), (2, 0), (3, 0), (4, 0), (4, 1)]
    for k, (nsym, select) in enumerate(shapes * 4):
        syms = rnd.sample(range(256), nsym)
        n = rnd.randrange(1, 60)
        ic, iextra, ibits = lencode(INS, n)
        sym = command_symbol(ic, 0, ic < 8)    # the copy is never reached
        mb_header(bw, n)
        compressed_header(bw, cmode=k % 4)
        codes = simple_code(bw, 256, syms, select)
        simple_code(bw, 704, [sym])
        simple_code(bw, 64, [rnd.randrange(64)])
        bw.put(iextra, ibits)
        for _ in range(n):
            s = rnd.choice(syms)
            c, l = codes[s]
            bw.code(c, l)
            plain.append(s)
        # an uncompressed meta-block
        raw = bytes(rnd.randrange(256) for _ in range(rnd.randrange(1, 300)))
        mb_header(bw, len(raw), uncompressed=True)
        bw.align()
        for b in raw:
            bw.put(b, 8)
        plain += raw
        # a metadata meta-block, skipped
        skip = rnd.choice([0, 1, 5, 256, 300])
        bw.put(0, 1)
        bw.put(3, 2)
        bw.put(0, 1)
        nb = 0 if skip == 0 else 1 if skip <= 256 else 2
        bw.put(nb, 2)
        if nb:
            bw.put((skip - 1) & 0xff, 8)
            if nb == 2:
                bw.put((skip - 1) >> 8, 8)
        bw.align()
        for _ in range(skip):
            bw.put(rnd.randrange(256), 8)
    last_empty(bw)
    return bw.done(), bytes(plain)


# ---- writing the cases ----------------------------------------------------------

class Cases:
    def __init__(self, out):
        self.out = out
        self.lines = []
        self.plains = {}

    def plain(self, key, data):
        name = f"plain-{key}"
        if name not in self.plains:
            open(os.path.join(self.out, name), "wb").write(data)
            self.plains[name] = data
        return name

    def add(self, name, codec, coded, expect, wrapper=0):
        open(os.path.join(self.out, name), "wb").write(coded)
        self.lines.append(f"{name} {codec} {wrapper} {expect}")


def gzip_header_fields(data):
    """A gzip member with FTEXT, FEXTRA, FNAME, FCOMMENT and FHCRC set (RFC 1952, 2.3)."""
    head = bytearray(b"\x1f\x8b\x08\x1f" + struct.pack("<I", 1700000000) + b"\x00\x03")
    extra = b"AC\x04\x00data"
    head += struct.pack("<H", len(extra)) + extra + b"picture.iff\x00" + b"made by make-stream.py\x00"
    head += struct.pack("<H", zlib.crc32(head) & 0xffff)
    c = zlib.compressobj(6, zlib.DEFLATED, -15)
    body = c.compress(data) + c.flush()
    return bytes(head) + body + struct.pack("<II", zlib.crc32(data), len(data) & 0xffffffff)


def gzip_cases(cs, d):
    for key in ("empty", "one", "short", "text", "html", "binary", "random", "aaaa", "big"):
        data = d[key]
        p = cs.plain(key, data)
        for level in (1, 6, 9):
            cs.add(f"{key}-{level}.gz", "gzip", gzip.compress(data, level, mtime=0), p)
        for wb in (9, 12, 15):
            c = zlib.compressobj(9 if wb != 12 else 1, zlib.DEFLATED, wb)
            cs.add(f"{key}-w{wb}.zz", "gzip", c.compress(data) + c.flush(), p)
            c = zlib.compressobj(6, zlib.DEFLATED, -wb)
            cs.add(f"{key}-w{wb}.deflate", "gzip", c.compress(data) + c.flush(), p)
        c = zlib.compressobj(0, zlib.DEFLATED, 15)
        cs.add(f"{key}-stored.zz", "gzip", c.compress(data) + c.flush(), p)
    data = d["text"]
    p = cs.plain("text", data)
    buf = io.BytesIO()
    with gzip.GzipFile("picture.iff", "wb", 9, buf, mtime=1) as f:
        f.write(data)
    cs.add("fname.gz", "gzip", buf.getvalue(), p)
    cs.add("fields.gz", "gzip", gzip_header_fields(data), p)
    two = d["text"] + d["binary"]
    cs.add("members.gz", "gzip", gzip.compress(d["text"], mtime=0) + gzip.compress(d["binary"], mtime=0),
           cs.plain("text+binary", two))
    cs.add("trailing.gz", "gzip", gzip.compress(data, mtime=0) + b"\0\0\0\0garbage", p)
    # the wrapper named rather than told
    cs.add("named-gzip", "gzip", gzip.compress(data, mtime=0), p, 1)
    cs.add("named-zlib", "gzip", zlib.compress(data), p, 2)
    c = zlib.compressobj(6, zlib.DEFLATED, -15)
    cs.add("named-raw", "gzip", c.compress(data) + c.flush(), p, 3)
    cs.add("wrong-wrapper", "gzip", zlib.compress(data), "error", 1)
    # damaged and cut short
    g = bytearray(gzip.compress(data, mtime=0))
    cs.add("cut.gz", "gzip", bytes(g[:len(g) // 2]), "error")
    cs.add("cut-trailer.gz", "gzip", bytes(g[:-3]), "error")
    g[-6] ^= 0x40
    cs.add("badcrc.gz", "gzip", bytes(g), "error")
    z = bytearray(zlib.compress(data))
    z[-1] ^= 1
    cs.add("badadler.zz", "gzip", bytes(z), "error")
    cs.add("cut.zz", "gzip", zlib.compress(data)[:-5], "error")


def brotli_cases(cs, d, dictionary, vectors):
    rnd = random.Random(16)
    if brotli:
        for key in ("empty", "one", "short", "text", "html", "utf8", "binary", "picture", "random", "aaaa", "readme"):
            data = d[key]
            p = cs.plain(key, data)
            for q in range(12):
                cs.add(f"{key}-q{q}.br", "brotli", brotli.compress(data, quality=q), p)
            for mode, mname in ((brotli.MODE_TEXT, "text"), (brotli.MODE_FONT, "font")):
                cs.add(f"{key}-{mname}.br", "brotli", brotli.compress(data, mode=mode, quality=9), p)
        data = d["big"]
        p = cs.plain("big", data)
        for w in (10, 12, 16, 18, 22, 24):
            for q in (2, 6, 11):
                cs.add(f"big-w{w}-q{q}.br", "brotli", brotli.compress(data, quality=q, lgwin=w), p)
        c = brotli.Compressor(quality=5)
        coded = c.process(d["text"]) + c.flush() + c.process(d["html"]) + c.flush() + c.finish()
        cs.add("flushed.br", "brotli", coded, cs.plain("text+html", d["text"] + d["html"]))
        good = brotli.compress(d["html"], quality=11)
        cs.add("cut.br", "brotli", good[:len(good) // 2], "error")
        cs.add("cut-end.br", "brotli", good[:-1], "error")
    # written here, from the RFC
    synth = []
    for length in range(4, 25):
        synth.append((f"dict-{length}.br", *synth_dictionary(dictionary, length)))
    for w in (10, 11, 15, 16, 17, 18, 24):
        synth.append((f"literals-w{w}.br", *synth_literals(rnd, w)))
    synth.append(("empty-last.br", bytes([0x06]), b""))
    synth.append(("large-window.br", bytes([0x11]), None))      # WBITS code 1 after 000: not RFC 7932
    for name, coded, plain in synth:
        if coded is None:
            continue
        if brotli:
            try:
                ref = brotli.decompress(coded)
            except brotli.error:
                ref = None
            if ref != plain:
                sys.exit(f"make-stream: Google's decoder reads {name} otherwise than expected here")
        cs.add(name, "brotli", coded, cs.plain(name, plain) if plain is not None else "error")
    # the kept vectors
    if vectors and os.path.exists(vectors):
        for line in open(vectors):
            f = line.split()
            if not f or f[0].startswith("#"):
                continue
            name, coded, plain = f[0], bytes.fromhex(f[1]), f[2]
            cs.add(name, "brotli", coded, "error" if plain == "error" else cs.plain(name, bytes.fromhex(plain if plain != "-" else "")))


def main(out, vectors=None):
    os.makedirs(out, exist_ok=True)
    d = datasets()
    dictionary = None
    dpath = os.environ.get("BROTLI_DICT")
    if dpath and os.path.exists(dpath) and os.path.getsize(dpath) == 122784:
        dictionary = open(dpath, "rb").read()
    cs = Cases(out)
    gzip_cases(cs, d)
    brotli_cases(cs, d, dictionary, vectors)
    with open(os.path.join(out, "cases.txt"), "w") as f:
        f.write("\n".join(cs.lines) + "\n")
    print(f"make-stream: {len(cs.lines)} cases; Python's brotli module "
          f"{'used' if brotli else 'not installed (Brotli: the streams written here and the kept vectors)'}"
          f"{'' if dictionary else '; no dictionary file (BROTLI_DICT), so no dictionary-word streams'}")


if __name__ == "__main__":
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    main(*sys.argv[1:])
