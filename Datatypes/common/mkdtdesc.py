#!/usr/bin/env python3
"""Write an AmigaOS DataTypes descriptor (DEVS:DataTypes/<Name>).

The descriptor is an IFF FORM DTYP with NAME, FVER and DTHD chunks (and
DTCD with recognition code), the layout datatypes.library reads: DTHD holds
struct DataTypeHeader with its pointers stored as offsets from the start of
the chunk, then the mask (WORDs, 0xFFFF matches any byte), the name, the
base name and the pattern.

usage: mkdtdesc.py OUT NAME BASENAME GROUP ID PATTERN VERSION [MASK...]
  With no MASK the PATTERN alone decides (text formats with no signature).
  MASK items: a character in quotes ('R'), a number (0x52) or ANY.
  DT_PRIORITY in the environment sets the priority (default 0, -128 to
  127). AROS tries a higher one first where two descriptors match the same
  file; AmigaOS 3.2.3's datatypes.library was seen to try a longer mask
  first and, between equal masks, priority 0 before higher ones (as an
  unsigned word: 1 comes after 0, -1 after everything; openmodule/DESIGN.md,
  libraries/opendatatypes/dt_core.c before()), so a longer mask is the way
  to come first there. A mask with no fixed bytes still counts: ANY at the
  end makes a mask longer.
  The kind of data the descriptor is tried on (datatypes.library tries each
  kind's descriptors only on data of that kind):
    default     DTF_BINARY
    DT_TEXT=1   DTF_ASCII: a file whose first block reads as text (every
                byte but the C0 and C1 controls), so SVG, PostScript, RTF,
                CSV and the like need it or the ascii datatype takes them
    DT_IFF=1    DTF_IFF: a FORM, CAT or LIST (the mask then usually is
                'F' 'O' 'R' 'M' ANY ANY ANY ANY and the FORM type)
  DT_CASE=1 sets DTF_CASE: the mask's bytes are compared exactly (without
  it, as Latin-1 letters with no case, so 0xC5 also matches 0xE5).
  DT_CODE=FILE puts FILE, an AmigaOS load file, in a DTCD chunk: recognition
  code datatypes.library runs once the mask and the pattern fit, with the
  DTHookContext in A0 (Datatypes/hooks/); FALSE sends it on down the list.
  A mask longer than the 1084 bytes datatypes.library reads never matches,
  so it is refused.

MIT, Copyright (c) 2026 AmiCradle Team.
"""
import os
import struct
import sys

DTF_BINARY = 0x0000
DTF_ASCII = 0x0001
DTF_IFF = 0x0002
DTF_CASE = 0x0010
# How much of the data datatypes.library reads to recognise it
# (libraries/opendatatypes/opendatatypes_lib.c, PEEK).
PEEK = 1084


def chunk(cid, data):
    body = struct.pack(">4sI", cid, len(data)) + data
    return body + (b"\0" if len(data) & 1 else b"")


def mask_word(item):
    if item == "ANY":
        return 0xFFFF
    if len(item) == 3 and item[0] == item[2] == "'":
        return ord(item[1])
    return int(item, 0) & 0xFF


def main(argv):
    out, name, base, group, ident, pattern, version = argv[1:8]
    mask = [mask_word(m) for m in argv[8:]]
    if len(group) != 4 or not 1 <= len(ident) <= 4:
        sys.exit("GROUP is four characters, ID one to four (padded with NULs)")
    if len(mask) > PEEK:
        sys.exit(f"a mask of {len(mask)} bytes is longer than the {PEEK} datatypes.library reads")
    text, iff = os.environ.get("DT_TEXT") == "1", os.environ.get("DT_IFF") == "1"
    if text and iff:
        sys.exit("DT_TEXT and DT_IFF: one kind of data")
    flags = DTF_ASCII if text else DTF_IFF if iff else DTF_BINARY
    if os.environ.get("DT_CASE") == "1":
        flags |= DTF_CASE
    code = b""
    if os.environ.get("DT_CODE"):
        try:
            with open(os.environ["DT_CODE"], "rb") as f:
                code = f.read()
        except OSError as e:
            sys.exit(f"DT_CODE: {e} (hooks/build.sh writes the hooks to DTHOOKS)")
        if not code:
            sys.exit("DT_CODE: an empty file")
    header_size = 32
    mask_off = header_size
    name_off = mask_off + 2 * len(mask)
    base_off = name_off + len(name) + 1
    pattern_off = base_off + len(base) + 1
    dthd = struct.pack(">IIII4s4shhHH", name_off, base_off, pattern_off, mask_off if mask else 0,
                       group.encode(), ident.encode(), len(mask), 0, flags,
                       int(os.environ.get("DT_PRIORITY", "0")) & 0xFFFF)
    dthd += b"".join(struct.pack(">H", w) for w in mask)
    dthd += name.encode() + b"\0" + base.encode() + b"\0" + pattern.encode() + b"\0"
    # The same chunk order as the system's own descriptors.
    form = b"DTYP" + chunk(b"FVER", version.encode() + b"\0")
    form += chunk(b"NAME", name.encode())
    form += chunk(b"DTHD", dthd)
    if code:
        form += chunk(b"DTCD", code)
    with open(out, "wb") as f:
        f.write(struct.pack(">4sI", b"FORM", len(form)) + form)


if __name__ == "__main__":
    if len(sys.argv) < 8:
        sys.exit(__doc__)
    main(sys.argv)
