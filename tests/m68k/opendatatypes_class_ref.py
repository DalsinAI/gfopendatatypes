#!/usr/bin/env python3
"""GFOpenDatatypes' datatypesclass: python's own model of the class, for
tests/m68k/test_opendatatypes_class.sh. Writes a random script of objects,
layouts, scrolls, drags and drag selections, and the log
opendatatypes_class_test.c must print for it.

The model is written from the datatypesclass autodoc's behaviour as
dtclass_core.c means to have it (scroll numbers in the object's units,
clamped; GM_LAYOUT's domain and visible numbers; DoAsyncLayout's
DTA_Busy and DTA_Sync; drag scrolling and the drag-select band), not from
the C code.
   python3 opendatatypes_class_ref.py script.txt expected.txt [seed [datatypes.cd]]
Copyright (c) 2026 AmiCradle Team. MIT.
"""
import random
import sys

GL, GT = 5, 6                  # the gadget's place
DL, DT = 10, 20                # the GadgetInfo's domain
MEM = [(i * 7 + 1) & 255 for i in range(300)]


def tdiv(a, b):
    """C's division, toward zero"""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def clamp(top, vis, tot):
    if top > tot - vis:
        top = tot - vis
    return max(top, 0)


def inside(v, size):
    if v < 0:
        return 0
    if v >= size:
        return size - 1 if size > 0 else 0
    return v


class Obj:
    def __init__(self, kind, totv, toth, nosel):
        self.kind, self.ptv, self.pth, self.nosel = kind, totv, toth, nosel
        self.tv = self.vv = self.tov = self.th = self.vh = self.toh = 0
        self.otv = self.oth = 0
        self.vu = self.hu = 1
        self.w = self.h = 0
        self.dom = (0, 0, 0, 0)
        self.busy = self.sync = 0
        self.dragging = self.dragsel = self.band_on = 0
        self.dx = self.dy = self.lx = self.ly = self.dtv = self.dth = 0
        self.active = 0
        self.layout_proc = None
        self.printer_proc = None


class Model:
    def __init__(self):
        self.out = []
        self.o = None
        self.async_on = 0
        self.selfdispose = 0
        self.pend = []          # [printing, stopped, initial]

    def p(self, s):
        self.out.append(s)

    def notify(self, interim, extra=()):
        o = self.o
        s = "notify" + (" interim" if interim else "")
        s += " tv=%d vv=%d tov=%d th=%d vh=%d toh=%d" % (o.tv, o.vv, o.tov, o.th, o.vh, o.toh)
        for k, v in extra:
            s += " %s=%d" % (k, v)
        self.p(s + " id=3")

    def render(self):
        o = self.o
        self.p("render 2 %d %d %d %d" % (o.tv, o.th, o.otv, o.oth))

    def band(self):
        o = self.o
        x0, y0 = o.dom[0] + o.dx, o.dom[1] + o.dy
        x1, y1 = o.dom[0] + o.lx, o.dom[1] + o.ly
        self.p("band %d %d %d %d" % (min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1)))
        o.band_on ^= 1

    def new(self, kind, totv, toth, nosel):
        if kind in ("file", "iff", "failfile"):
            self.p("open data 1005")
        if kind == "failfile":
            self.p("new fail 205")
            self.o = None
            return
        if kind == "iff":
            self.p("iffdos 7 0")
        if kind == "clip":
            self.p("iffclip 0 0")
        # cliph: the program's IFFHandle (DTA_Handle), used as it is
        h = {"file": "7", "iff": "iff", "clip": "iff", "cliph": "iff", "ram": "0", "mem": "0"}[kind]
        st = {"file": 2, "iff": 2, "clip": 3, "cliph": 3, "ram": 1, "mem": 5}[kind]
        self.p("new ok 2 h=%s st=%d" % (h, st))
        self.o = Obj(kind, totv, toth, nosel)

    def units(self, vu, hu):
        self.o.vu, self.o.hu = vu or 1, hu or 1

    def set_totals(self, tov, toh):
        o = self.o
        if (tov, toh) != (o.tov, o.toh):
            o.tov, o.toh = tov, toh
            self.notify(0)

    def layout(self, w, h, initial):
        o = self.o
        o.w, o.h = w, h
        o.dom = (GL + DL, GT + DT, w, h)
        o.vv, o.vh = tdiv(h, o.vu), tdiv(w, o.hu)
        o.tv, o.th = clamp(o.tv, o.vv, o.tov), clamp(o.th, o.vh, o.toh)
        o.sync = 0
        if o.layout_proc is not None:
            self.p("stop %d" % o.layout_proc)
            self.pend[o.layout_proc][1] = 1
        o.busy = 1
        self.notify(1, [("busy", 1)])
        if self.async_on:
            self.p("start %d layout" % len(self.pend))
            o.layout_proc = len(self.pend)
            self.pend.append([0, 0, initial])
            return
        self.run_layout(initial)

    def run_layout(self, initial):
        o = self.o
        self.p("asynclayout %d" % initial)
        self.set_totals(o.ptv, o.pth)
        o.sync, o.busy = 1, 0
        o.layout_proc = None
        self.notify(0, [("busy", 0), ("sync", 1)])

    def runproc(self):
        o = self.o
        gone = 0
        for printing, stopped, initial in self.pend:
            if printing is None:        # waited for by a dispose already
                continue
            if printing:
                self.p("print")
                self.notify(0, [("pstat", 0)])
                if self.selfdispose:
                    # the target disposes it from the print process: still printing
                    self.selfdispose = 0
                    self.dispose(1)
                    gone = 1
                    continue
                o.printer_proc = None
            elif not stopped:
                self.run_layout(initial)
                if self.selfdispose:
                    self.selfdispose = 0
                    self.dispose(1)
                    gone = 1
        self.pend = []
        if gone:
            self.p("dispose 0 0")

    def print_(self):
        o = self.o
        if o.printer_proc is not None or not self.async_on:
            self.p("print 0")
            return
        self.p("start %d print" % len(self.pend))
        o.printer_proc = len(self.pend)
        self.pend.append([1, 0, 0])
        self.p("print 1")

    def tops(self, tv, th, update):
        o = self.o
        ntv, nth = clamp(tv, o.vv, o.tov), clamp(th, o.vh, o.toh)
        if (ntv, nth) != (o.tv, o.th):
            o.otv, o.oth = o.tv, o.th
            o.tv, o.th = ntv, nth
            if update:
                self.render()
            else:
                self.notify(0)

    def inactive(self):
        o = self.o
        if o.band_on:
            self.band()
        o.dragging = o.dragsel = 0
        o.active = 0
        self.p("inactive")

    def down(self, x, y):
        o = self.o
        o.dx = o.lx = inside(x, o.dom[2])
        o.dy = o.ly = inside(y, o.dom[3])
        if o.dragsel:
            self.band()
            r = 0
        elif o.tov > o.vv or o.toh > o.vh:
            o.dragging, o.dtv, o.dth = 1, o.tv, o.th
            r = 0
        else:
            r = 2
        self.p("active %d" % r)
        if r:
            self.inactive()
        else:
            o.active = 1

    def move(self, x, y):
        o = self.o
        if not o.active:
            return
        x, y = inside(x, o.dom[2]), inside(y, o.dom[3])
        if (x, y) != (o.lx, o.ly):
            if o.dragsel:
                if o.band_on:
                    self.band()
                o.lx, o.ly = x, y
                self.band()
            elif o.dragging:
                o.lx, o.ly = x, y
                ntv = clamp(o.dtv - tdiv(y - o.dy, o.vu), o.vv, o.tov)
                nth = clamp(o.dth - tdiv(x - o.dx, o.hu), o.vh, o.toh)
                if (ntv, nth) != (o.tv, o.th):
                    o.otv, o.oth, o.tv, o.th = o.tv, o.th, ntv, nth
                    self.render()
                    self.notify(1)
        self.p("input 0")

    def up(self, x, y):
        o = self.o
        if not o.active:
            return
        x, y = inside(x, o.dom[2]), inside(y, o.dom[3])
        if o.dragsel:
            if o.band_on:
                self.band()
            o.lx, o.ly = x, y
            o.dragsel = 0
            ox, oy = o.th * o.hu, o.tv * o.vu
            self.p("select %d %d %d %d" % (min(o.dx, o.lx) + ox, min(o.dy, o.ly) + oy,
                                           max(o.dx, o.lx) + ox, max(o.dy, o.ly) + oy))
        elif o.dragging:
            o.dragging = 0
            self.notify(0)
        self.p("input 10")
        self.inactive()

    def menu(self):
        o = self.o
        if not o.active:
            return
        if o.dragging and (o.dtv, o.dth) != (o.tv, o.th):
            o.otv, o.oth, o.tv, o.th = o.tv, o.th, o.dtv, o.dth
            self.render()
            self.notify(0)
        self.p("input 2")
        self.inactive()

    def startsel(self):
        o = self.o
        r = 0
        if not (o.dragging or o.dragsel) and not o.nosel:
            o.dragsel = r = 1
        self.p("startsel %d" % r)

    def get(self):
        o = self.o
        self.p("get 9 %d %d %d %d %d %d %d %d dom %d %d %d %d 5" %
               ((o.tv, o.vv, o.tov, o.th, o.vh, o.toh, o.busy, o.sync) + o.dom))

    def write(self):
        o = self.o
        if o.kind == "mem":
            self.p("write 9 300 %02x..%02x" % (MEM[0], MEM[-1]))
            self.p("written 1")
        elif o.kind in ("file", "iff"):
            # the file's bytes from the start, the position put back
            self.p("seek 7 0 -1")
            self.p("seek 7 0 -1")
            self.p("written 1")
        else:
            self.p("written 0")

    def dispose(self, from_proc=0):
        o = self.o
        # a print not yet run: asked to stop, then waited for (it holds
        # proc_lock from before PrintDTObjectA answers); from the print's
        # own notification, it is only asked
        if o.printer_proc is not None:
            self.p("stop %d" % o.printer_proc)
        if o.layout_proc is not None:
            self.p("stop %d" % o.layout_proc)
        if o.printer_proc is not None and not from_proc:
            self.p("print")
            o.printer_proc = None
            self.notify(0, [("pstat", 0)])
        # a layout asked to stop before it began ends at once (layout_body)
        if o.kind in ("iff", "clip"):
            self.p("iffclose")
        if o.kind in ("iff", "file"):
            self.p("close 7")
        self.p("selfdispose 0" if from_proc else "dispose 0 0")
        self.o = None
        self.pend = [[None, 1, 0] for _ in self.pend]


def main():
    script, expected = sys.argv[1], sys.argv[2]
    rnd = random.Random(int(sys.argv[3]) if len(sys.argv) > 3 else 3210)
    m = Model()
    lines = []

    def cmd(s):
        lines.append(s)

    kinds = ["file", "iff", "ram", "mem", "clip", "failfile", "cliph"]
    for n in range(40):
        kind = kinds[n % len(kinds)] if n < len(kinds) else rnd.choice(kinds)
        totv, toth = rnd.choice([0, 5, 50, 300, 1000]), rnd.choice([0, 7, 80, 640, 2000])
        nosel = rnd.random() < 0.2
        cmd("new %s %d %d%s" % (kind, totv, toth, " nosel" if nosel else ""))
        m.new(kind, totv, toth, nosel)
        if m.o is None:
            continue
        if rnd.random() < 0.6:
            vu, hu = rnd.randint(1, 9), rnd.randint(1, 9)
            cmd("units %d %d" % (vu, hu))
            m.units(vu, hu)
        initial = 1
        for _ in range(rnd.randint(4, 14)):
            what = rnd.random()
            o = m.o
            if what < 0.2 or o.dom[2] == 0:
                w, h = rnd.randint(0, 400), rnd.randint(0, 300)
                cmd("layout %d %d %d" % (w, h, initial))
                m.layout(w, h, initial)
                initial = 0
            elif what < 0.35:
                tv, th = rnd.randint(-50, 1200), rnd.randint(-50, 2100)
                cmd("set %d %d" % (tv, th))
                m.tops(tv, th, 0)
            elif what < 0.5:
                tv, th = rnd.randint(-50, 1200), rnd.randint(-50, 2100)
                cmd("update %d %d" % (tv, th))
                m.tops(tv, th, 1)
            elif what < 0.8:
                if rnd.random() < 0.4:
                    cmd("startsel")
                    m.startsel()
                x, y = rnd.randint(-10, o.dom[2] + 10), rnd.randint(-10, o.dom[3] + 10)
                cmd("down %d %d" % (x, y))
                m.down(x, y)
                for _ in range(rnd.randint(0, 6)):
                    x, y = x + rnd.randint(-60, 60), y + rnd.randint(-60, 60)
                    cmd("move %d %d" % (x, y))
                    m.move(x, y)
                if rnd.random() < 0.2:
                    cmd("menu")
                    m.menu()
                else:
                    x, y = x + rnd.randint(-5, 5), y + rnd.randint(-5, 5)
                    cmd("up %d %d" % (x, y))
                    m.up(x, y)
            elif what < 0.85:
                cmd("get")
                m.get()
            elif what < 0.9:
                a = rnd.randint(0, 1)
                cmd("async %d" % a)
                m.async_on = a
            elif what < 0.97:
                if rnd.random() < 0.35:
                    cmd("runproc")
                    m.runproc()
                else:
                    cmd("print")
                    m.print_()
            else:
                cmd("write")
                m.write()
        cmd("get")
        m.get()
        cmd("runproc")
        m.runproc()
        cmd("dispose")
        m.dispose()

    # disposed with its print, its layout or both started but not yet run:
    # OM_DISPOSE waits for them before it frees the object
    for kind, what in (("file", "p"), ("mem", "p"), ("file", "l"), ("mem", "lp")):
        cmd("new %s 50 80" % kind)
        m.new(kind, 50, 80, False)
        cmd("async 1")
        m.async_on = 1
        if "l" in what:
            cmd("layout 120 90 1")
            m.layout(120, 90, 1)
        if "p" in what:
            cmd("print")
            m.print_()
        cmd("dispose")
        m.dispose()

    # the completion's target disposes the object, from the process itself:
    # the dispose waits for the notification to unwind (selftest); and with
    # a print from the completion whose own completion disposes it too, the
    # object goes once, after the last process has unwound (*overlap); a
    # DisposeDTObject from another task meanwhile joins it (dtjoin), and one
    # from another task first is joined by the completion's (dtfirst)
    for name in ("dispose", "print", "dtdispose", "relayout", "overlap", "dtoverlap", "mixoverlap",
                 "dtjoin", "dtfirst"):
        cmd("selftest %s" % name)
        m.p("selftest %s ok" % name)

    # LaunchToolA's command lines, AmigaDOS quoting (* before " and *)
    def tool(prog, proj, size):
        dec = lambda w: w.replace("~", " ").replace("'", '"')
        p, j = dec(prog), dec(proj)
        line = p
        if proj != "-" and j:
            line += ' "' + "".join("*" + c if c in '"*' else c for c in j) + '"'
        return line if len(line) < size else None
    alpha = "abcXYZ019~'*:/."
    for _ in range(60):
        prog = "".join(rnd.choice(alpha.replace("~", "").replace("'", "")) for _ in range(rnd.randint(1, 12)))
        proj = rnd.choice(["-", "".join(rnd.choice(alpha) for _ in range(rnd.randint(1, 20)))])
        size = rnd.choice([1, 5, 12, 20, 40, 200])
        cmd("tool %s %s %d" % (prog, proj, size))
        r = tool(prog, proj, size)
        m.p("tool %d [%s]" % (len(r), r) if r is not None else "tool 0 []")
    # A project with a control character in its name (^ is a line feed,
    # ` a carriage return) is refused: the Shell would end the command at
    # a line feed and run what follows as a second command
    for prog, proj in (("Edit", "a^Delete~x"), ("Edit", "^"), ("RX~Ed", "my`pic.iff"), ("Edit", "a^"),
                       ("Edit", "ok~name.iff")):
        cmd("tool %s %s 200" % (prog, proj))
        r = None if any(c in "^`" for c in proj) else tool(prog, proj, 200)
        m.p("tool %d [%s]" % (len(r), r) if r is not None else "tool 0 []")
    groups = {"pict": "picture", "soun": "sound", "inst": "sound", "text": "text", "anim": "animation",
              "movi": "animation", "docu": "amigaguide", "syst": None, "musi": None, "zzzz": None}
    for g, b in groups.items():
        cmd("group %d" % int.from_bytes(g.encode(), "big"))
        m.p("group %s" % (b or "-"))
    # sys/datatypes.catalog's descriptor: the built-in strings, id by id
    cd = open(sys.argv[4]).read().split("\n") if len(sys.argv) > 4 else []
    for i, l in enumerate(cd):
        if l and not l.startswith(";") and "(" in l and l.endswith("//)"):
            ident = int(l[l.index("(") + 1:l.index("/")])
            cmd("string %d" % ident)
            m.p("string %d [%s]" % (ident, cd[i + 1]))

    with open(script, "w") as f:
        f.write("\n".join(lines) + "\n")
    with open(expected, "w") as f:
        f.write("\n".join(m.out) + "\n")


if __name__ == "__main__":
    main()
