#!/usr/bin/env python3
"""A sound read with OIA_Reader says its own rate.

OIM_READSOUND hands out frames at the sound's own rate, and a program reads
that rate from SDTA_SamplesPerSec (ogg2iff, SDL_mixer's dt_reader). The
system's sound.datatype keeps a rate as a Paula period, clamped to 124, so a
class that only sets SDTA_SamplesPerSec and leaves OM_GET to its superclass
gives 44.1 kHz back as 3546895 / 124 = 28603 Hz: the sound reads 1.54x slow
(AC-250). This checks, from the sources, that every class answering
OIM_READSOUND answers SDTA_SamplesPerSec in its own OM_GET, and SDTA_Frequency
where it answers that at all. Whether the answer is right on an Amiga is the
lab's to show (ogg2iff -i gives the file's own rate).
MIT, Copyright (c) 2026 AmiCradle Team.

usage: python3 tests/test-reader-rate.py
"""
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
fails = 0


def check(what, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + what + ('' if ok else ': ' + detail))
    if not ok:
        fails += 1


def om_get(src):
    """the text of the dispatcher's OM_GET case, up to the next case at its level"""
    m = re.search(r'\n(\s*)case OM_GET\s*:', src)
    if not m:
        return ''
    indent = m.group(1)
    rest = src[m.end():]
    end = re.search(r'\n' + re.escape(indent) + r'(case \w+\s*:|default\s*:)', rest)
    return rest[:end.start()] if end else rest


def answers(block, attr):
    return re.search(r'case ' + attr + r'\s*:|opg_AttrID\s*==\s*' + attr + r'\b', block) is not None


readers = sorted(p for p in glob.glob(os.path.join(HERE, '*', '*.c'))
                 if re.search(r'case OIM_READSOUND\s*:', open(p, errors='replace').read()))
check('the classes that answer OIM_READSOUND are there', len(readers) >= 3, str(len(readers)))
for p in readers:
    rel = os.path.relpath(p, HERE)
    block = om_get(open(p, errors='replace').read())
    check(f'{rel}: has an OM_GET of its own', bool(block))
    check(f'{rel}: OM_GET answers SDTA_SamplesPerSec itself', answers(block, 'SDTA_SamplesPerSec'))

src = open(os.path.join(HERE, 'opensound', 'soundclass.c'), errors='replace').read()
block = om_get(src)
check('opensound: OM_GET answers SDTA_Frequency too', answers(block, 'SDTA_Frequency'))
check('opensound: the reader keeps its rate (si->rate = rate)', re.search(r'si->rate\s*=\s*rate\s*;', src) is not None)

print(f"reader rates: {'all passed' if not fails else f'{fails} failed'}")
sys.exit(1 if fails else 0)
