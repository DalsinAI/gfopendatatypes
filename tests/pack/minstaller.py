"""A small interpreter for the part of the Commodore Installer's language
that tools/build_pack.py writes, over a stand-in Amiga disk, so the tests can
run Install-GFOpenDatatypes and Uninstall-GFOpenDatatypes and check what they leave.

It is strict where the real Installer would fail: makedir needs its parent,
copyfiles needs its destination folder, delete fails on a missing file or a
full folder unless told "nofail". It does not stand in for a run on an Amiga.

MIT, Copyright (c) 2026 AmiCradle Team.
"""
from __future__ import annotations

from pathlib import Path


class Abort(Exception):
    pass


class Exit(Exception):
    pass


class Sym(str):
    pass


def parse(text: str) -> list:
    out, stack, i, n = [], [], 0, len(text)
    cur = out
    while i < n:
        c = text[i]
        if c == ";":
            while i < n and text[i] != "\n":
                i += 1
        elif c.isspace():
            i += 1
        elif c == "(":
            stack.append(cur)
            new: list = []
            cur.append(new)
            cur = new
            i += 1
        elif c == ")":
            if not stack:
                raise SyntaxError(f"unmatched ) at {i}")
            cur = stack.pop()
            i += 1
        elif c == '"':
            i += 1
            s = []
            while text[i] != '"':
                if text[i] == "\\":
                    i += 1
                    s.append({"n": "\n", '"': '"', "\\": "\\"}.get(text[i], text[i]))
                else:
                    s.append(text[i])
                i += 1
            cur.append("".join(s))
            i += 1
        else:
            j = i
            while j < n and not text[j].isspace() and text[j] not in "()\";":
                j += 1
            tok = text[i:j]
            cur.append(int(tok) if tok.lstrip("-").isdigit() else Sym(tok))
            i = j
    if stack:
        raise SyntaxError("unclosed (")
    return out


ASSIGNS = {"devs:": "sys:devs/", "libs:": "sys:libs/", "c:": "sys:c/"}


def norm(path: str) -> str:
    p = path.lower()
    for a, t in ASSIGNS.items():
        if p.startswith(a):
            p = t + p[len(a):]
    return p.rstrip("/")


class Disk:
    """Files (path -> bytes) and folders, case-insensitive like AmigaDOS."""

    def __init__(self, files: dict[str, bytes], dirs: list[str]):
        self.files = {norm(k): v for k, v in files.items()}
        self.dirs = {norm(d) for d in dirs} | {"sys:"}

    def parent(self, p: str) -> str:
        if "/" in p:
            return p.rsplit("/", 1)[0]
        return p.split(":")[0] + ":"

    def exists(self, path: str) -> int:
        p = norm(path)
        return 1 if p in self.files else 2 if (p in self.dirs or p.endswith(":")) else 0

    def snapshot(self) -> tuple:
        return dict(self.files), set(self.dirs)


def tackon(d: str, n: str) -> str:
    return d + n if d.endswith((":", "/")) or not d else d + "/" + n


class Installer:
    def __init__(self, disk: Disk, drawer: Path, cpu: str = "68040", os_version: int = 47,
                 answers: dict | None = None):
        self.disk, self.drawer, self.cpu, self.os = disk, drawer, cpu, os_version
        self.vars: dict = {"@askoptions-help": "", "@askbool-help": "", "@user-level": 0}
        self.procs: dict = {}
        self.asked: list[str] = []
        self.answers = answers or {}
        self.ran: list[str] = []
        self.exit_text = ""
        self.messages: list[str] = []

    def run(self, text: str) -> None:
        try:
            for st in parse(text):
                self.ev(st)
        except Exit:
            pass

    # source files: relative ones are in the pack's drawer
    def read(self, path: str) -> bytes:
        if ":" not in path:
            return (self.drawer / path).read_bytes()
        return self.disk.files[norm(path)]

    def has(self, path: str) -> bool:
        return (self.drawer / path).is_file() if ":" not in path else self.disk.exists(path) == 1

    def opts(self, args: list) -> tuple[list, dict]:
        plain, named = [], {}
        for a in args:
            if isinstance(a, list) and a and isinstance(a[0], Sym) and a[0] in (
                    "source", "dest", "newname", "infos", "nogauge", "optional", "noreq", "prompt", "help",
                    "choices", "default", "append", "all"):
                named[a[0]] = [self.ev(x) for x in a[1:]]
            else:
                plain.append(a)
        return plain, named

    def ev(self, x):
        if isinstance(x, (int,)) or (isinstance(x, str) and not isinstance(x, Sym)):
            return x
        if isinstance(x, Sym):
            if x not in self.vars:
                raise NameError(f"{x} used before it is set")
            return self.vars[x]
        if not x:
            return None
        head = x[0]
        if isinstance(head, list):                 # a block
            r = None
            for st in x:
                r = self.ev(st)
            return r
        f, args = str(head).lower(), x[1:]
        if f == "set":
            for k in range(0, len(args), 2):
                self.vars[args[k]] = self.ev(args[k + 1])
            return self.vars[args[-2]]
        if f == "procedure":
            self.procs[args[0]] = args[1:]
            return 0
        if head in self.procs:
            r = None
            for st in self.procs[head]:
                r = self.ev(st)
            return r
        if f == "if":
            if truth(self.ev(args[0])):
                return self.ev(args[1])
            return self.ev(args[2]) if len(args) > 2 else 0
        if f == "and":
            return 1 if all(truth(self.ev(a)) for a in args) else 0
        if f == "or":
            return 1 if any(truth(self.ev(a)) for a in args) else 0
        if f == "not":
            return 0 if truth(self.ev(args[0])) else 1
        v = None
        if f in ("=", "<>", "<", ">", "+", "/", "bitand", "cat", "tackon", "database", "getversion"):
            v = [self.ev(a) for a in args]
        if f == "=":
            return 1 if v[0] == v[1] else 0
        if f == "<>":
            return 1 if v[0] != v[1] else 0
        if f == "<":
            return 1 if v[0] < v[1] else 0
        if f == ">":
            return 1 if v[0] > v[1] else 0
        if f == "+":
            return sum(v)
        if f == "/":
            return v[0] // v[1]
        if f == "bitand":
            return v[0] & v[1]
        if f == "cat":
            return "".join(str(s) for s in v)
        if f == "tackon":
            return tackon(v[0], v[1])
        if f == "database":
            assert v[0] == "cpu"
            return self.cpu
        if f == "getversion":
            assert not v, "getversion of a file isn't used"
            return self.os << 16
        plain, named = self.opts(args)
        if f == "message":
            self.messages.append("".join(str(self.ev(a)) for a in plain))
            return 0
        if f in ("welcome", "working", "complete"):
            return 0
        if f == "abort":
            raise Abort(self.ev(plain[0]))
        if f == "exit":
            self.exit_text = "".join(str(self.ev(a)) for a in plain)
            raise Exit()
        if f == "run":
            self.ran.append(self.ev(plain[0]))
            return 0
        if f in ("askoptions", "askbool"):
            prompt = named["prompt"][0]
            self.asked.append(prompt)
            for key, val in self.answers.items():
                if key in prompt:
                    return val
            return named["default"][0]
        if f == "exists":
            return self.disk.exists(self.ev(plain[0]))
        if f == "makedir":
            p = norm(self.ev(plain[0]))
            if self.disk.exists(self.disk.parent(p)) != 2:
                raise OSError(f"makedir {p}: no parent")
            if self.disk.exists(p):
                raise OSError(f"makedir {p}: already there")
            self.disk.dirs.add(p)
            return 0
        if f == "copyfiles":
            src, dest = named["source"][0], norm(named["dest"][0])
            if self.disk.exists(dest) != 2:
                raise OSError(f"copyfiles: no folder {dest}")
            name = named["newname"][0] if "newname" in named else src.replace(":", "/").rsplit("/", 1)[-1]
            self.disk.files[norm(tackon(dest, name))] = self.read(src)
            if "infos" in named and self.has(src + ".info"):
                self.disk.files[norm(tackon(dest, name + ".info"))] = self.read(src + ".info")
            return 1
        if f == "delete":
            p = norm(self.ev(plain[0]))
            nofail = "nofail" in named.get("optional", [])
            if p in self.disk.files:
                del self.disk.files[p]
            elif p in self.disk.dirs and not any(k.startswith(p + "/") for k in list(self.disk.files) + list(self.disk.dirs)):
                self.disk.dirs.discard(p)
            elif not nofail:
                raise OSError(f"delete {p}: missing or not empty")
            return 0
        if f == "textfile":
            dest = norm(named["dest"][0])
            if self.disk.exists(self.disk.parent(dest)) != 2:
                raise OSError(f"textfile {dest}: no folder")
            self.disk.files[dest] = "".join(named.get("append", [])).encode("latin-1")
            return 0
        raise NotImplementedError(f"({head} ...) isn't in the subset")


def truth(v) -> bool:
    return v not in (0, "", None)
