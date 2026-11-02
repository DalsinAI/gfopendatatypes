#!/usr/bin/env python3
"""The datatypes' RomTags carry their version (common/dtlib.c's DT_VERSION).

ramlib, dos and amicradle-guest's packed ROM weigh a class by its RomTag's
rt_Version and the revision in its ID string (the first "V.R" after a space
whose V is rt_Version), and the newer copy runs. This checks, from the
sources, that dt_romtag's rt_Version is DT_VERSION and that every class's
LibVersion is DT_VERSION, its LibRevision the ID string's revision and its
LibName the ID string's first word. Given built classes (the Amiga build's
out/Classes/DataTypes/*.datatype), it also reads each one's RomTag and checks
rt_Version against its ID string.
MIT, Copyright (c) 2026 AmiCradle Team.

usage: python3 tests/test-romtag.py [CLASS FILE...]
"""
import glob
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
fails = 0


def check(what, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + what + ('' if ok else ': ' + detail))
    if not ok:
        fails += 1


def id_revision(idstr, version):
    """the revision of the first "V.R" after a space (or at the start)
    whose V is version; None when there is none"""
    for m in re.finditer(r'(?:(?<= )|^)(\d+)\.(\d+)', idstr):
        if int(m.group(1)) == version:
            return int(m.group(2))
    return None


def c_string(src, name):
    """a const char NAME[] = "..." "..."; definition, its literals joined"""
    m = re.search(r'const char ' + name + r'\[\]\s*=\s*((?:"(?:[^"\\]|\\.)*"\s*)+);', src)
    return ''.join(re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))) if m else None


def c_uword(src, name):
    m = re.search(r'const UWORD ' + name + r'\s*=\s*(\d+)\s*;', src)
    return int(m.group(1)) if m else None


lib = open(os.path.join(HERE, 'common', 'dtlib.c')).read()
m = re.search(r'#define DT_VERSION (\d+)', lib)
check('common/dtlib.c defines DT_VERSION', m is not None)
version = int(m.group(1)) if m else 0
tag = re.search(r'const struct Resident dt_romtag[^=]*=\s*\{([^}]*)\}', lib)
fields = [f.strip() for f in tag.group(1).split(',')] if tag else []
check('dt_romtag\'s rt_Version is DT_VERSION', len(fields) >= 5 and fields[4] == 'DT_VERSION',
      ', '.join(fields[:6]))

classes = sorted(p for p in glob.glob(os.path.join(HERE, '*', '*.c'))
                 if re.search(r'const char LibIdString\[\]\s*=', open(p, errors='replace').read()))
check('the classes are there', len(classes) >= 10, str(len(classes)))
for p in classes:
    src = open(p, errors='replace').read()
    rel = os.path.relpath(p, HERE)
    name, idstr = c_string(src, 'LibName'), c_string(src, 'LibIdString')
    v, r = c_uword(src, 'LibVersion'), c_uword(src, 'LibRevision')
    check(f'{rel}: LibVersion {v} is DT_VERSION', v == version, f'DT_VERSION {version}')
    check(f'{rel}: ID "{idstr}" names {name}', bool(idstr and name and idstr.split(' ')[0] == name))
    rev = id_revision(idstr or '', v or 0)
    check(f'{rel}: the ID\'s revision {rev} is LibRevision {r}', rev is not None and rev == r)


def romtags(data):
    """(rt_Version, ID string) of each RomTag in a load file's code and data
    hunks: a matchword whose next longword is its own offset"""
    def long(p):
        return struct.unpack_from('>I', data, p)[0]
    if len(data) < 24 or long(0) != 0x3F3:
        return []
    first, last = long(12), long(16)
    p = 20 + 4 * (last - first + 1)
    out = []
    while p + 8 <= len(data):
        t = long(p) & 0x1FFFFFFF
        if t in (0x3E9, 0x3EA):
            n = (long(p + 4) & 0x3FFFFFFF) * 4
            h = data[p + 8:p + 8 + n]
            for off in range(0, len(h) - 26, 2):
                if h[off:off + 2] == b'\x4a\xfc' and struct.unpack_from('>I', h, off + 2)[0] == off:
                    ida = struct.unpack_from('>I', h, off + 18)[0]
                    end = h.find(b'\0', ida)
                    out.append((h[off + 11], h[ida:end].decode('latin-1') if 0 <= ida < len(h) else ''))
            p += 8 + n
        elif t == 0x3EB:
            p += 8
        elif t == 0x3EC:
            p += 4
            while long(p):
                p += 8 + 4 * long(p)
            p += 4
        elif t == 0x3F0:
            p += 4
            while long(p) & 0xFFFFFF:
                p += 4 + 4 * ((long(p) & 0xFFFFFF) + 1)
            p += 4
        elif t == 0x3F1:
            p += 8 + 4 * long(p + 4)
        elif t == 0x3F2:
            p += 4
        else:
            break
    return out


for f in sys.argv[1:]:
    tags = romtags(open(f, 'rb').read())
    name = os.path.basename(f)
    check(f'{name}: one RomTag', len(tags) == 1, str(tags))
    if tags:
        v, idstr = tags[0]
        check(f'{name}: rt_Version {v} with ID "{idstr}"', v == version and id_revision(idstr, v) is not None)

print(f"datatype RomTags: {'all passed' if not fails else f'{fails} failed'}")
sys.exit(1 if fails else 0)
