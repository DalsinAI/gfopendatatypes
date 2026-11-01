#!/usr/bin/env python3
"""Write brotli.datatype's constant data, brdict.c: RFC 7932's static
dictionary (Appendix A) and its 121 word transforms (Appendix B).

usage: mkbrdict.py SOURCE OUT.c [OUT.bin]
  SOURCE is the RFC's text (rfc7932.txt, from https://www.rfc-editor.org/),
  whose Appendix A gives the dictionary in hex, or the dictionary itself as
  122,784 bytes. Either way the bytes must have the dictionary's SHA-256,
  or nothing is written. OUT.bin, if named, gets the dictionary's bytes
  (for tests/make-stream.py).

The transforms are typed in below from Appendix B: a prefix, a transform
(Identity, OmitLastN, UppercaseFirst, UppercaseAll, OmitFirstN) and a
suffix. The strings go into one pool, each with its length before it; the
tables hold offsets into it, so the data needs no pointers (and so no
relocations or writable data in the library).

MIT, Copyright (c) 2026 AmiCradle Team.
SPDX-License-Identifier: MIT
"""
import hashlib
import re
import sys

DICT_SIZE = 122784
DICT_SHA256 = "20e42eb1b511c21806d4d227d07e5dd06877d8ce7b3a817f378f313653f35c70"

# Transform numbers in brdecode.c: 0 Identity, 1-9 OmitLast1-9,
# 10 UppercaseFirst, 11 UppercaseAll, 12-20 OmitFirst1-9.
ID, UF, UA = 0, 10, 11


def OL(n):
    return n


def OF(n):
    return 11 + n


# RFC 7932, Appendix B: ID, prefix, transform, suffix
TRANSFORMS = [
    ("", ID, ""), ("", ID, " "), (" ", ID, " "), ("", OF(1), ""),
    ("", UF, " "), ("", ID, " the "), (" ", ID, ""), ("s ", ID, " "),
    ("", ID, " of "), ("", UF, ""), ("", ID, " and "), ("", OF(2), ""),
    ("", OL(1), ""), (", ", ID, " "), ("", ID, ", "), (" ", UF, " "),
    ("", ID, " in "), ("", ID, " to "), ("e ", ID, " "), ("", ID, "\""),
    ("", ID, "."), ("", ID, "\">"), ("", ID, "\n"), ("", OL(3), ""),
    ("", ID, "]"), ("", ID, " for "), ("", OF(3), ""), ("", OL(2), ""),
    ("", ID, " a "), ("", ID, " that "), (" ", UF, ""), ("", ID, ". "),
    (".", ID, ""), (" ", ID, ", "), ("", OF(4), ""), ("", ID, " with "),
    ("", ID, "'"), ("", ID, " from "), ("", ID, " by "), ("", OF(5), ""),
    ("", OF(6), ""), (" the ", ID, ""), ("", OL(4), ""), ("", ID, ". The "),
    ("", UA, ""), ("", ID, " on "), ("", ID, " as "), ("", ID, " is "),
    ("", OL(7), ""), ("", OL(1), "ing "), ("", ID, "\n\t"), ("", ID, ":"),
    (" ", ID, ". "), ("", ID, "ed "), ("", OF(9), ""), ("", OF(7), ""),
    ("", OL(6), ""), ("", ID, "("), ("", UF, ", "), ("", OL(8), ""),
    ("", ID, " at "), ("", ID, "ly "), (" the ", ID, " of "), ("", OL(5), ""),
    ("", OL(9), ""), (" ", UF, ", "), ("", UF, "\""), (".", ID, "("),
    ("", UA, " "), ("", UF, "\">"), ("", ID, "=\""), (" ", ID, "."),
    (".com/", ID, ""), (" the ", ID, " of the "), ("", UF, "'"), ("", ID, ". This "),
    ("", ID, ","), (".", ID, " "), ("", UF, "("), ("", UF, "."),
    ("", ID, " not "), (" ", ID, "=\""), ("", ID, "er "), (" ", UA, " "),
    ("", ID, "al "), (" ", UA, ""), ("", ID, "='"), ("", UA, "\""),
    ("", UF, ". "), (" ", ID, "("), ("", ID, "ful "), (" ", UF, ". "),
    ("", ID, "ive "), ("", ID, "less "), ("", UA, "'"), ("", ID, "est "),
    (" ", UF, "."), ("", UA, "\">"), (" ", ID, "='"), ("", UF, ","),
    ("", ID, "ize "), ("", UA, "."), ("\xc2\xa0", ID, ""), (" ", ID, ","),
    ("", UF, "=\""), ("", UA, "=\""), ("", ID, "ous "), ("", UA, ", "),
    ("", UF, "='"), (" ", UF, ","), (" ", UA, "=\""), (" ", UA, ", "),
    ("", UA, ","), ("", UA, "("), ("", UA, ". "), (" ", UA, "."),
    ("", UA, "='"), (" ", UA, ". "), (" ", UF, "=\""), (" ", UA, "='"),
    (" ", UF, "='"),
]


def from_rfc(text):
    """The hex lines of Appendix A, between its heading and Appendix B's."""
    starts = [m.end() for m in re.finditer(r"^Appendix A\.", text, re.M)]
    ends = [m.start() for m in re.finditer(r"^Appendix B\.", text, re.M)]
    if not starts or not ends:
        sys.exit("mkbrdict: no Appendix A in the RFC text")
    body = text[starts[-1]:ends[-1]]
    hexdata = "".join(l.strip() for l in body.splitlines() if re.fullmatch(r"\s+[0-9a-f]{2,}\s*", l))
    return bytes.fromhex(hexdata)


def c_bytes(data, per=20):
    out = []
    for i in range(0, len(data), per):
        out.append("    " + ",".join(str(b) for b in data[i:i + per]) + ",")
    return "\n".join(out)


def main(src, dst, binout=None):
    raw = open(src, "rb").read()
    d = raw if len(raw) == DICT_SIZE else from_rfc(raw.decode("latin-1"))
    if len(d) != DICT_SIZE or hashlib.sha256(d).hexdigest() != DICT_SHA256:
        sys.exit(f"mkbrdict: {src} does not give RFC 7932's dictionary "
                 f"({len(d)} bytes, SHA-256 {hashlib.sha256(d).hexdigest()})")
    if len(TRANSFORMS) != 121:
        sys.exit("mkbrdict: there are 121 transforms")
    pool, where = bytearray(), {}
    for p, _, s in TRANSFORMS:
        for t in (p, s):
            b = t.encode("latin-1")
            if b not in where:
                where[b] = len(pool)
                pool += bytes([len(b)]) + b
    pre = [where[p.encode("latin-1")] for p, _, _ in TRANSFORMS]
    suf = [where[s.encode("latin-1")] for _, _, s in TRANSFORMS]
    kind = [k for _, k, _ in TRANSFORMS]
    if len(pool) > 255:
        sys.exit("mkbrdict: the pool outgrew byte offsets")
    with open(dst, "w") as f:
        f.write("/* Written by mkbrdict.py from RFC 7932 (Appendices A and B): do not edit. */\n")
        f.write('#include "brdecode.h"\n\n')
        f.write(f"const UBYTE br_dict[{DICT_SIZE}] = {{\n{c_bytes(d)}\n}};\n\n")
        f.write(f"const UBYTE br_xf_pool[{len(pool)}] = {{\n{c_bytes(pool)}\n}};\n\n")
        f.write(f"const UBYTE br_xf_prefix[121] = {{\n{c_bytes(pre)}\n}};\n\n")
        f.write(f"const UBYTE br_xf_kind[121] = {{\n{c_bytes(kind)}\n}};\n\n")
        f.write(f"const UBYTE br_xf_suffix[121] = {{\n{c_bytes(suf)}\n}};\n")
    if binout:
        open(binout, "wb").write(d)


if __name__ == "__main__":
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    main(*sys.argv[1:])
