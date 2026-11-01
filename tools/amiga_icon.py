"""Classic Workbench icons (.info), four colours, for the pack's drawer.

The writer is openamigaprint's (tools/make_package.py, MIT, AmiCradle
Team), copied with its picture format: one character a pixel in the
Workbench 2 pens, '.' grey (the background), '#' black, 'w' white, 'b' blue.
Classic icons look the same on every Workbench from 2.04 on.

MIT, Copyright (c) 2026 AmiCradle Team.
"""
from __future__ import annotations

import struct

NO_POSITION = -0x80000000
WBDISK, WBDRAWER, WBTOOL, WBPROJECT = 1, 2, 3, 4
PENS = {".": 0, "#": 1, "w": 2, "b": 3}

ART = {
    # a box of files with a blue arrow going in: the Installer scripts
    "install": """
...............bbbbbb...................
...............bbbbbb...................
...............bbbbbb...................
...............bbbbbb...................
...........bbbbbbbbbbbbbb...............
.............bbbbbbbbbb.................
...............bbbbbb...................
.................bb.....................
....################################....
....#wwwwwwwwwwwwwwwwwwwwwwwwwwwwww#....
....#w##########wwwwwwww##########w#....
....#wwwwwwwwwwwwwwwwwwwwwwwwwwwwww#....
....#w###############wwwwwwwwwwwwww#....
....#wwwwwwwwwwwwwwwwwwwwwwwwwwwwww#....
....#w########wwwwwwww############w#....
....#wwwwwwwwwwwwwwwwwwwwwwwwwwwwww#....
....################################....
""",
    # a picture, a note and a film frame on a page: the datatypes
    "datatypes": """
....##############################......
....#wwwwwwwwwwwwwwwwwwwwwwwwwwww##.....
....#wbbbbbbbbbbwwwwwwww#wwwwwwww#w#....
....#wbwwwwwwwwbwwwwwww##wwwwwwww####...
....#wbww##wwwwbwwwwww#w#wwwwwwwwwww#...
....#wbw####wwwbwwwwww#w#wwwwwwwwwww#...
....#wb######wwbwwww###wwwwwwwwwwwww#...
....#wbbbbbbbbbbwwww###wwwwwwwwwwwww#...
....#wwwwwwwwwwwwwwwwwwwwwwwwwwwwwww#...
....#w#########################wwwww#...
....#w#b#bbbbbbb#bbbbbbb#b#wwwwwwwww#...
....#w#################b#wwwwwwwwwww#...
....#wwwwwwwwwwwwwwwwwwwwwwwwwwwwwww#...
....#w################wwwwwwwwwwwww#....
....#wwwwwwwwwwwwwwwwwwwwwwwwwwwwwww#...
....################################....
""",
}


def picture(name: str) -> tuple[int, int, list[list[int]]]:
    rows = [r for r in ART[name].strip("\n").split("\n")]
    w = max(len(r) for r in rows)
    return w, len(rows), [[PENS[c] for c in r.ljust(w, ".")] for r in rows]


def image_record(w: int, h: int, px: list[list[int]]) -> bytes:
    rowbytes = ((w + 15) // 16) * 2
    planes = bytearray()
    for plane in range(2):
        for y in range(h):
            row = bytearray(rowbytes)
            for x in range(w):
                if px[y][x] & (1 << plane):
                    row[x // 8] |= 0x80 >> (x % 8)
            planes += row
    return struct.pack(">hhhhhIBBI", 0, 0, w, h, 2, 1, 0x03, 0, 0) + bytes(planes)


def string(s: str) -> bytes:
    b = s.encode("latin-1") + b"\0"
    return struct.pack(">I", len(b)) + b


def drawer_data(width: int, height: int) -> bytes:
    nw = struct.pack(">hhhhBBIIIIIIIhhHHH", 60, 40, width, height, 0xFF, 0xFF, 0, 0, 0, 0, 0, 0, 0, 90, 40, 0xFFFF, 0xFFFF, 1)
    return nw + struct.pack(">ii", 0, 0)


def icon(kind: int, art: str, default_tool: str | None = None, tool_types: list[str] | None = None,
         stack: int = 4096, window: tuple[int, int] = (400, 200)) -> bytes:
    """A classic DiskObject: one picture, complemented when selected."""
    w, h, px = picture(art)
    tool_types = tool_types or []
    drawer = kind in (WBDISK, WBDRAWER)
    out = bytearray(struct.pack(">HH", 0xE310, 1))
    # the Gadget: GADGIMAGE with complement highlight, RELVERIFY|GADGIMMEDIATE, BOOLGADGET; UserData 1 = an OS 2.x icon
    out += struct.pack(">IhhhhHHHIIIIIHI", 0, 0, 0, w, h, 0x0004, 0x0003, 0x0001, 1, 0, 0, 0, 0, 0, 1)
    out += struct.pack(">BB", kind, 0)
    out += struct.pack(">IIiiIIi", 1 if default_tool else 0, 1 if tool_types else 0, NO_POSITION, NO_POSITION,
                       1 if drawer else 0, 0, stack)
    if drawer:
        out += drawer_data(*window)
    out += image_record(w, h, px)
    if default_tool:
        out += string(default_tool)
    if tool_types:
        out += struct.pack(">I", (len(tool_types) + 1) * 4)
        for t in tool_types:
            out += string(t)
    if drawer:
        out += struct.pack(">IH", 0, 0)            # dd_Flags, dd_ViewModes: as Workbench chooses
    return bytes(out)


def parse(data: bytes) -> dict:
    """Reads back what icon() writes (for the tests): kind, size, tool, tool types."""
    magic, ver = struct.unpack_from(">HH", data, 0)
    assert magic == 0xE310 and ver == 1, "not a DiskObject"
    w, h = struct.unpack_from(">hh", data, 4 + 8)
    kind = data[4 + 44]
    has_tool, has_types, _, _, has_drawer, _, stack = struct.unpack_from(">IIiiIIi", data, 4 + 44 + 2)
    off = 4 + 44 + 2 + 28
    if has_drawer:
        off += 56
    iw, ih, depth = struct.unpack_from(">hhh", data, off + 4)
    off += 20 + ((iw + 15) // 16) * 2 * ih * depth
    tool = None
    if has_tool:
        n, = struct.unpack_from(">I", data, off)
        tool = data[off + 4:off + 4 + n - 1].decode("latin-1")
        off += 4 + n
    types = []
    if has_types:
        n, = struct.unpack_from(">I", data, off)
        off += 4
        for _ in range(n // 4 - 1):
            m, = struct.unpack_from(">I", data, off)
            types.append(data[off + 4:off + 4 + m - 1].decode("latin-1"))
            off += 4 + m
    if has_drawer:
        off += 6
    assert off == len(data), f"{len(data) - off} bytes left over"
    return {"kind": kind, "w": w, "h": h, "tool": tool, "types": types, "stack": stack}
