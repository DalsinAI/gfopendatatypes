#!/usr/bin/env python3
# Copyright (c) 2026 AmiCradle Team. MIT licence (see LICENSE at the repository root).
"""Compare an open library's .lvo table with the NDK's _lib.fd: same call at every LVO,
same registers. A slot the fd keeps private may be private here, or named
with the fd's registers (a call the vendor documents but keeps out of its
fd's public part). A call after the fd's last slot is a difference unless
its line says "beyond the fd" (a later version's call the fd predates).
Exit 1 on any difference, listing each."""
import os, re, sys

def ndk32_fd(path):
    """The stoves keep NDK 1.3's fds in lib/fd13 beside NDK 3.2's in lib/fd, and
    the build scripts' `find ... | head -1` can land on either. An OS 3.2
    library is checked against 3.2's table: given an fd13 file, use the fd
    file of the same name next to it when there is one."""
    d, name = os.path.split(os.path.abspath(path))
    if os.path.basename(d) == "fd13":
        alt = os.path.join(os.path.dirname(d), "fd", name)
        if os.path.isfile(alt):
            print("%s is NDK 1.3's: checking against NDK 3.2's %s" % (path, alt))
            return alt
    return path

def read_fd(path):
    bias, public, out = 30, True, {}
    for line in open(path, encoding="latin-1"):
        line = line.strip()
        if line.startswith("##bias"): bias = int(line.split()[1]); continue
        if line.startswith("##private"): public = False; continue
        if line.startswith("##public"): public = True; continue
        if line.startswith("##end"): break
        if not line or line.startswith("*") or line.startswith("##"): continue
        m = re.match(r"(\w+)\(([^)]*)\)\(([^)]*)\)", line)
        if not m: continue
        regs = ",".join(r.strip().lower() for r in re.split(r"[,/]", m.group(3)) if r.strip())
        out[-bias] = (m.group(1) if public else "private", regs)
        bias += 6
    return out

BEYOND = set()

def read_lvo(path):
    out = {}
    for raw in open(path):
        # ">REGS": registers the call leaves for its caller besides d0
        # (openexec's gen_stubs.py); the fd has no such column
        line = [t for t in raw.split("#")[0].split() if not t.startswith(">")]
        if not line: continue
        out[int(line[0])] = (line[1], line[2] if len(line) > 2 else "")
        if "beyond the fd" in raw: BEYOND.add(int(line[0]))
    return out

sys.argv[1] = ndk32_fd(sys.argv[1])
fd, ours = read_fd(sys.argv[1]), read_lvo(sys.argv[2])
bad = 0
for lvo in sorted(set(fd) | set(ours), reverse=True):
    f, o = fd.get(lvo), ours.get(lvo)
    if f is None and o[0] == "private":
        pass                                  # a slot the NDK leaves out is private
    elif f is None and lvo in BEYOND and lvo < min(fd):
        print("%d: %s, beyond the fd (not checked)" % (lvo, o[0]))
    elif f is None:
        print("%d: ours has %s, the NDK has nothing" % (lvo, o[0])); bad = 1
    elif o is None:
        print("%d: the NDK has %s(%s), ours has nothing" % (lvo, f[0], f[1])); bad = 1
    elif f[0] == "private" and o[0] == "private":
        pass
    elif f[0] == "private" and o[1] == f[1]:
        pass                                  # named here, private in the fd: same registers
    elif f != o:
        print("%d: the NDK has %s(%s), ours %s(%s)" % (lvo, f[0], f[1], o[0], o[1])); bad = 1
print("%s %s the NDK's %s" % (sys.argv[2], "differs from" if bad else "matches", sys.argv[1]))
sys.exit(bad)
