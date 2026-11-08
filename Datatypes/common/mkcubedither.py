#!/usr/bin/env python3
"""Writes the 6x6x6 colour cube's ordered-dither tables, const at build time:

  webm/webm_dither_tables.h  webm.datatype's BT.601 tables (8.6 fixed point,
                             as webm_dither.h always had them) and, for each
                             of the 16 dither values, the cube's red, green
                             and blue parts for every sum the YUV formula can
                             make before it is clamped (so the clamp is in
                             the table)
  cdxl/cdxl_cube_tables.h    the same cube parts for 0 to 255 only (CDXL's
                             chunky RGB films)

A part is ((c * 5 + d) >> 8) times 36, 6 or 1, c the channel clamped to 0 to
255 and d the dither value times 16: the formula webm_dither.h and
cdxl_core.c used pixel by pixel. tests/test-webm-dither.sh checks every input
against that formula, and that these files are what this script writes.

usage: mkcubedither.py DATATYPES_DIR
MIT, Copyright (c) 2026 AmiCradle Team.
"""
import os
import sys

Y = [(298 * (i - 16)) >> 2 for i in range(256)]
RV = [(409 * (i - 128)) >> 2 for i in range(256)]
GU = [(-100 * (i - 128)) >> 2 for i in range(256)]
GV = [(-208 * (i - 128)) >> 2 for i in range(256)]
BU = [(516 * (i - 128)) >> 2 for i in range(256)]


def span(sums):
    return min(sums) >> 6, max(sums) >> 6


# the sums before >> 6 (32 for the rounding included), at their extremes
R = span([y + v + 32 for y in (min(Y), max(Y)) for v in (min(RV), max(RV))])
G = span([y + a + b + 32 for y in (min(Y), max(Y)) for a in (min(GU), max(GU)) for b in (min(GV), max(GV))])
B = span([y + u + 32 for y in (min(Y), max(Y)) for u in (min(BU), max(BU))])


def part(s, d, mul):
    c = 0 if s < 0 else 255 if s > 255 else s
    return ((c * 5 + d * 16) >> 8) * mul


def rows(spans):
    out = []
    for d in range(16):
        row = []
        for (lo, hi), mul in zip(spans, (36, 6, 1)):
            row += [part(s, d, mul) for s in range(lo, hi + 1)]
        out.append(row)
    return out


def body(vals, per, indent):
    return "\n".join(indent + ", ".join(str(v) for v in vals[i:i + per]) + "," for i in range(0, len(vals), per))


def carray(decl, vals, per=24):
    return decl + " = {\n" + body(vals, per, "    ") + "\n};\n"


def carray2(decl, rows_, per=32):
    return decl + " = {\n" + "\n".join("    {\n" + body(r, per, "        ") + "\n    }," for r in rows_) + "\n};\n"


HEAD = """/* Written by common/mkcubedither.py: do not edit (see it for what these are).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
"""


def write(path, text):
    with open(path, "w") as f:
        f.write(text)


def main():
    top = sys.argv[1]
    spans = (R, G, B)
    n = [hi - lo + 1 for lo, hi in spans]
    t = HEAD + "#ifndef WEBM_DITHER_TABLES_H\n#define WEBM_DITHER_TABLES_H\n\n"
    t += "/* BT.601, limited range, 8.6 fixed point */\n"
    for name, vals in (("y", Y), ("rv", RV), ("gu", GU), ("gv", GV), ("bu", BU)):
        t += carray("static const short webm_%s[256]" % name, vals)
    t += "\n/* A row a dither value: the red parts for sums %d to %d, the green %d to %d,\n" % (R + G)
    t += " * the blue %d to %d (each sum the YUV formula's, >> 6) */\n" % B
    t += "#define WEBM_CUBE_R (%d)\n#define WEBM_CUBE_G (%d)\n#define WEBM_CUBE_B (%d)\n" % (
        -R[0], n[0] - G[0], n[0] + n[1] - B[0])
    t += "#define WEBM_CUBE_ROW %d\n" % sum(n)
    t += carray2("static const unsigned char webm_cube[16][WEBM_CUBE_ROW]", rows(spans))
    t += "\n#endif\n"
    write(os.path.join(top, "webm", "webm_dither_tables.h"), t)
    c = HEAD + "#ifndef CDXL_CUBE_TABLES_H\n#define CDXL_CUBE_TABLES_H\n\n"
    c += "/* A row a dither value: the red parts for 0 to 255, then the green, the blue */\n"
    c += carray2("static const unsigned char cdxl_cube[16][768]", rows(((0, 255),) * 3))
    c += "\n#endif\n"
    write(os.path.join(top, "cdxl", "cdxl_cube_tables.h"), c)


if __name__ == "__main__":
    main()
