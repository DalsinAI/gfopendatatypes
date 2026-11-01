/* DatatypesABITest: OS 3.2's datatypes.library and GFOpenDatatypes side by
 * side.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * Runs one set of calls through a datatypes.library base and writes every
 * answer as a line of text. With gfopendatatypes.library in LIBS: (or the
 * directory given with LIB=) it runs the set through OS 3.2's library and
 * through ours and prints each line that differs, both answers. Without
 * it, or with PROBE, it prints OS 3.2's lines: what the autodocs leave
 * open, measured.
 *
 * The set: GetDTString, GetDTTriggerMethodDataFlags, the V45 method tables,
 * ObtainDataTypeA by name (DTST_RAM) and every descriptor in
 * DEVS:DataTypes read by name. With DESC, descriptors made here to ask one
 * question each (masks, case, priorities, ties, patterns, IFF types,
 * recognition code, tools, text or binary) are written to
 * DEVS:DataTypes/ODT_* and given to OS 3.2 with AddDataTypes, and data
 * files made for them in DIR= (T:ODTData) are recognised by file
 * (DTST_FILE) and from memory (DTST_MEMORY). Only for a lab Amiga: it
 * adds files to DEVS:DataTypes and to OS 3.2's list of types. With
 * SCAN=dir, each entry of dir is recognised too. LIST (probe only) dumps
 * OS 3.2's own list of types, the "DataTypesList" semaphore, last.
 *
 * Before each call into a library it writes "@ call" and flushes (QUIET: not).
 *
 * With OBJ, the object calls (step D2): objects made from a text, an
 * ILBM and bytes written to T:, from memory, the clipboard and DTST_RAM
 * for each group; their attributes, classes, methods, DTM_WRITE,
 * SaveDTObjectA, StartDragSelect; and which private slots hold code.
 *
 *   DatatypesABITest [PROBE] [DESC] [OBJ] [LIST] [QUIET] [LIB=dir] [DIR=dir] [SCAN=dir] [HOOK=file]
 * HOOK=file puts that load file in ODT_Hook in place of the built-in probe code.
 * Exit 0 when nothing differs, 5 when something does, 20 on an error.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <exec/semaphores.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <utility/tagitem.h>
#include <intuition/intuition.h>
#include <intuition/classes.h>
#include <libraries/iffparse.h>
#include <proto/exec.h>
#include <proto/iffparse.h>
#include <proto/dos.h>
#include <proto/datatypes.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "dtprobe.h"

/* V44/V45 names, in case the NDK leaves them out */
#ifndef DTST_MEMORY
#define DTST_MEMORY 5
#endif
#ifndef DTA_SourceAddress
#define DTA_SourceAddress (DTA_Dummy + 39)
#endif
#ifndef DTA_SourceSize
#define DTA_SourceSize (DTA_Dummy + 40)
#endif
#ifndef DTERROR_UNKNOWN_DATATYPE
#define DTERROR_UNKNOWN_DATATYPE 2000
#endif
#ifndef STM_HELP
#define STM_HELP 17
#endif
#ifndef STM_USER
#define STM_USER 100
#endif
#ifndef STMD_VOID
#define STMD_VOID 0x00010000UL
#define STMD_ULONG 0x00020000UL
#define STMD_STRPTR 0x00030000UL
#define STMD_TAGLIST 0x00040000UL
#endif

struct Library *DataTypesBase;

/* ---- the answers, as lines of text ---------------------------------------- */

#define OUTSIZE (512UL * 1024UL)
static char *out_buf;
static ULONG out_len;

static void out(const char *fmt, ...)
{
    va_list ap;
    if (out_len > OUTSIZE - 1024) return;
    va_start(ap, fmt);
    out_len += vsprintf(out_buf + out_len, fmt, ap);
    va_end(ap);
}

static const char *s(const char *p) { return p ? p : "(null)"; }

/* "@ what": written and flushed before each call into the library, so a
 * run that stops shows the call it stopped in */
static int steps = 1;
static void step(const char *fmt, ...)
{
    va_list ap;
    if (!steps) return;
    va_start(ap, fmt);
    printf("@ ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    fflush(stdout);
}

/* the first n characters the same, without case (n 0: all of them) */
static int same_ci(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; !n || i < n; i++) {
        char x = a[i], y = b[i];
        if (x >= 'a' && x <= 'z') x -= 32;
        if (y >= 'a' && y <= 'z') y -= 32;
        if (x != y) return 0;
        if (!x) return 1;
    }
    return 1;
}

static void id4(ULONG id)
{
    int i;
    for (i = 24; i >= 0; i -= 8) {
        UBYTE c = id >> i;
        out(c >= 0x20 && c < 0x7f ? "%c" : "\\%02x", c);
    }
}

/* IoErr straight after the call (printing changes it) */
static LONG last_err;

/* a header string: "=header" when it points at the header itself */
static const char *hs(const struct DataTypeHeader *h, const char *p)
{
    return (const void *)p == (const void *)h ? "=header" : s(p);
}

/* a DataType as text: everything but its addresses */
static void out_dt(struct DataType *dt)
{
    struct DataTypeHeader *h;
    struct Node *n;
    int i;
    if (!dt) { out(" NULL ioerr %ld", (long)last_err); return; }
    h = dt->dtn_Header;
    if (!h) { out(" (no header)"); return; }
    out(" \"%s\" base \"%s\" pat \"%s\" gid ", hs(h, h->dth_Name), hs(h, h->dth_BaseName),
        hs(h, h->dth_Pattern));
    id4(h->dth_GroupID); out(" id "); id4(h->dth_ID);
    out(" flags %04x pri %u mask %d:", h->dth_Flags, h->dth_Priority, h->dth_MaskLen);
    for (i = 0; h->dth_Mask && i < h->dth_MaskLen && i < 40; i++) out("%04x", (UWORD)h->dth_Mask[i]);
    out(" fn \"%s\" attrs %d node1 %s/%d/%d node2 %s",
        s(dt->dtn_FunctionName), dt->dtn_AttrList ? 1 : 0,
        (APTR)dt->dtn_Node1.ln_Name == (APTR)h->dth_Name ? "name" : s(dt->dtn_Node1.ln_Name),
        dt->dtn_Node1.ln_Type, dt->dtn_Node1.ln_Pri,
        (APTR)dt->dtn_Node2.ln_Name == (APTR)h->dth_Name ? "name" : s(dt->dtn_Node2.ln_Name));
    for (n = dt->dtn_ToolList.lh_Head; n && n->ln_Succ; n = n->ln_Succ) {
        struct ToolNode *tn = (struct ToolNode *)n;
        out(" tool %u/%04x \"%s\"", tn->tn_Tool.tn_Which, tn->tn_Tool.tn_Flags, s(tn->tn_Tool.tn_Program));
    }
}

/* ---- strings, trigger flags, method tables, tools ---------------------------- */

static void strings(void)
{
    static const ULONG more[] = { 0, 1, 100, 1000, 1999, GID_SYSTEM, GID_TEXT, GID_DOCUMENT, GID_SOUND,
                                  GID_INSTRUMENT, GID_MUSIC, GID_PICTURE, GID_ANIMATION, GID_MOVIE,
                                  MAKE_ID('z','z','z','z'), 3000, 0xffffffffUL };
    ULONG id;
    unsigned i;
    for (id = 2000; id < 2020; id++) {
        step("GetDTString %lu", (unsigned long)id);
        out("str %lu \"%s\"\n", (unsigned long)id, s(GetDTString(id)));
    }
    for (id = 2095; id < 2130; id++) {
        step("GetDTString %lu", (unsigned long)id);
        out("str %lu \"%s\"\n", (unsigned long)id, s(GetDTString(id)));
    }
    for (i = 0; i < sizeof more / sizeof more[0]; i++) {
        step("GetDTString %08lx", (unsigned long)more[i]);
        out("str %08lx \"%s\"\n", (unsigned long)more[i], s(GetDTString(more[i])));
    }
}

static void trigger_flags(void)
{
    static const ULONG more[] = { 100, 101, 1000, 0xffff, STM_PLAY | STMD_ULONG, STM_COMMAND | STMD_VOID,
                                  STM_USER | STMD_TAGLIST, 0x00050000UL, 0x00ff0000UL, 0x01000002UL,
                                  0xffffffffUL };
    ULONG m;
    unsigned i;
    step("GetDTTriggerMethodDataFlags");
    for (m = 0; m < 26; m++) out("tflags %lu %08lx\n", (unsigned long)m, (unsigned long)GetDTTriggerMethodDataFlags(m));
    for (i = 0; i < sizeof more / sizeof more[0]; i++)
        out("tflags %08lx %08lx\n", (unsigned long)more[i], (unsigned long)GetDTTriggerMethodDataFlags(more[i]));
}

static void out_methods(const char *what, const ULONG *base, const ULONG *r)
{
    int i;
    out("%s", what);
    if (!r) { out(" NULL\n"); return; }
    if (base && r == base) out(" same");
    for (i = 0; i < 20 && r[i] != ~0UL; i++) out(" %lu", (unsigned long)r[i]);
    out(i < 20 ? " end\n" : " ...\n");
}

static void out_triggers(const char *what, const struct DTMethod *base, const struct DTMethod *r)
{
    int i;
    out("%s", what);
    if (!r) { out(" NULL\n"); return; }
    for (i = 0; i < 20 && r[i].dtm_Label; i++)
        out(" [%s %s %08lx%s]", s(r[i].dtm_Label), s(r[i].dtm_Command), (unsigned long)r[i].dtm_Method,
            r[i].dtm_Label == base[0].dtm_Label || r[i].dtm_Label == base[1].dtm_Label ? " shared" : "");
    out(i < 20 ? " end %08lx\n" : " ...\n", i < 20 ? (unsigned long)r[i].dtm_Method : 0UL);
}

static void methods(void)
{
    static ULONG m[] = { 10, 20, 30, 20, ~0UL }, inc[] = { 40, 10, ~0UL }, exc[] = { 20, 99, ~0UL }, none[] = { ~0UL };
    static struct DTMethod t[] = {
        { "Play", "PLAY", STM_PLAY }, { "Stop", "STOP", STM_STOP }, { "Go", "GOTO", STM_LOCATE },
        { "Cmd", NULL, STM_COMMAND }, { "Stop2", "stop", STM_STOP | STMD_VOID }, { NULL, NULL, 0 } };
    static struct DTMethod ti[] = { { "Help", "HELP", STM_HELP }, { "Play", "PLAY", STM_PLAY }, { NULL, NULL, 0 } };
    static struct DTMethod te[] = { { "x", "GOTO", STM_STOP }, { NULL, NULL, 0 } };
    static struct DTMethod tn[] = { { NULL, NULL, 0 } };
    ULONG *r;
    struct DTMethod *rt;
    unsigned i;
    static const ULONG find[] = { 10, 20, 30, 40, ~0UL, 0 };

    for (i = 0; i < sizeof find / sizeof find[0]; i++) {
        ULONG *p;
        step("FindMethod %ld", (long)find[i]);
        p = FindMethod(m, find[i]);
        out("FindMethod %ld -> %ld\n", (long)find[i], p ? (long)(p - m) : -1L);
    }
    out("FindMethod none 10 -> %ld\n", FindMethod(none, 10) ? 0L : -1L);
#define COPY(a, b, c) step("CopyDTMethods " #a " " #b " " #c); r = CopyDTMethods(a, b, c); out_methods("CopyDTMethods " #a " " #b " " #c, a, r); if (r) FreeDTMethods(r)
    COPY(m, inc, exc);
    COPY(m, NULL, NULL);
    COPY(m, none, none);
    COPY(m, NULL, exc);
    COPY(m, inc, NULL);
    COPY(none, inc, exc);
    COPY(m, m, NULL);
    COPY(m, NULL, m);
#undef COPY
    {
        static const char *cmd[] = { "PLAY", "play", "STOP", "Stop", "GOTO", NULL, "NOPE", "", "Play" };
        static const ULONG meth[] = { ~0UL, ~0UL, ~0UL, STM_STOP, STM_PLAY, STM_LOCATE, ~0UL, ~0UL, STM_PLAY,
                                      STM_STOP | STMD_VOID, STM_COMMAND, STM_HELP, 0 };
        unsigned j;
        for (i = 0; i < sizeof cmd / sizeof cmd[0]; i++)
            for (j = 0; j < sizeof meth / sizeof meth[0]; j++) {
                struct DTMethod *p;
                step("FindTriggerMethod %s %08lx", s(cmd[i]), (unsigned long)meth[j]);
                p = FindTriggerMethod(t, (STRPTR)cmd[i], meth[j]);
                out("FindTriggerMethod %s %08lx -> %ld\n", s(cmd[i]), (unsigned long)meth[j], p ? (long)(p - t) : -1L);
            }
    }
#define COPYT(a, b, c) step("CopyDTTriggerMethods " #a " " #b " " #c); rt = CopyDTTriggerMethods(a, b, c); out_triggers("CopyDTTriggerMethods " #a " " #b " " #c, t, rt); if (rt) FreeDTMethods(rt)
    COPYT(t, ti, te);
    COPYT(t, NULL, NULL);
    COPYT(t, tn, tn);
    COPYT(t, NULL, te);
    COPYT(tn, ti, NULL);
    COPYT(t, t, NULL);
    COPYT(t, NULL, t);
#undef COPYT
}

/* ---- types by name ---------------------------------------------------------------- */

static void by_name(const char *name)
{
    struct DataType *dt;
    step("ObtainDataTypeA DTST_RAM \"%s\"", name);
    SetIoErr(0);
    dt = ObtainDataTypeA(DTST_RAM, (APTR)name, NULL);
    last_err = IoErr();
    out("ram \"%s\"", name);
    out_dt(dt);
    out("\n");
    if (dt) ReleaseDataType(dt);
}

static void ram_names(void)
{
    static const char *const names[] = { "ilbm", "ILBM", "IFF ILBM", "picture", "binary", "ascii", "iff",
                                         "directory", "text", "ftxt", "amigaguide", "AmigaGuide", "8svx",
                                         "odtprihi", "ODT PriHi", "odt prihi", "nonexistent", "" };
    unsigned i;
    struct DataType *a, *b;
    for (i = 0; i < sizeof names / sizeof names[0]; i++) by_name(names[i]);
    step("ObtainDataTypeA DTST_RAM ilbm twice");
    a = ObtainDataTypeA(DTST_RAM, (APTR)"ilbm", NULL);
    b = ObtainDataTypeA(DTST_RAM, (APTR)"ilbm", NULL);
    out("ram ilbm twice: %s\n", !a ? "none" : a == b ? "same" : "different");
    if (a) ReleaseDataType(a);
    if (b) ReleaseDataType(b);
}

/* Each descriptor file of OS 3.2's DEVS:DataTypes by its base name and its
 * name: the same descriptors read by both libraries */
static ULONG be32(const UBYTE *p) { return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3]; }

static void system_types(void)
{
    struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
    BPTR dir = Lock("DEVS:DataTypes", ACCESS_READ);
    static UBYTE buf[4096];
    if (dir && fib && Examine(dir, fib)) {
        BPTR old = CurrentDir(dir);
        while (ExNext(dir, fib)) {
            BPTR fh;
            LONG n, at;
            ULONG l = strlen(fib->fib_FileName);
            if (fib->fib_DirEntryType > 0 || (l > 5 && same_ci(fib->fib_FileName + l - 5, ".info", 0))) continue;
            if (!strncmp(fib->fib_FileName, "ODT_", 4)) continue;
            if (!(fh = Open(fib->fib_FileName, MODE_OLDFILE))) continue;
            n = Read(fh, buf, sizeof buf);
            Close(fh);
            if (n < 12 || be32(buf) != MAKE_ID('F','O','R','M') || be32(buf + 8) != ID_DTYP) continue;
            for (at = 12; at + 8 <= n; at += 8 + ((be32(buf + at + 4) + 1) & ~1UL)) {
                ULONG id = be32(buf + at), size = be32(buf + at + 4), o;
                if (id != ID_DTHD || size < 32 || at + 8 + (LONG)size > n) continue;
                o = be32(buf + at + 8 + 4);
                if (o && o < size) by_name((const char *)buf + at + 8 + o);
                o = be32(buf + at + 8);
                if (o && o < size) by_name((const char *)buf + at + 8 + o);
                break;
            }
        }
        CurrentDir(old);
    }
    if (dir) UnLock(dir);
    if (fib) FreeDosObject(DOS_FIB, fib);
}

/* ---- descriptors and data made here (DESC) ----------------------------------- */

/* The recognition code in the test's own descriptors (DTCD), 68000 and
 * position independent: it saves what it touches, finds the task's struct
 * dtprobe (tc_UserData, magic 'DTPH'), counts the call, writes down A0 and
 * the long at 4(sp), copies 40 bytes from A0 when A0 is an even address,
 * and answers the record's answer (1 with no record). Source:
 *     movem.l d1/a0-a1/a6,-(sp) ; move.l a0,d1 ; move.l 4.w,a6
 *     sub.l a1,a1 ; jsr -294(a6) ; move.l d0,a1 ; move.l 88(a1),a1
 *     move.l a1,d0 ; beq.s out ; cmp.l #'DTPH',(a1) ; bne.s out
 *     addq.l #1,8(a1) ; move.l d1,12(a1) ; move.l 20(sp),16(a1)
 *     move.l 4(a1),d0 ; move.l d1,a0 ; tst.l d1 ; beq.s done
 *     btst #0,d1 ; bne.s done ; move.l d0,d1 ; lea 20(a1),a1 ; moveq #9,d0
 * loop: move.l (a0)+,(a1)+ ; dbra d0,loop ; move.l d1,d0 ; bra.s done
 * out:  moveq #1,d0
 * done: movem.l (sp)+,d1/a0-a1/a6 ; rts ; nop
 * As a load file (HUNK_HEADER, one HUNK_CODE, HUNK_END) its header longs,
 * if run as code, are harmless ori.b instructions, so it is safe however
 * the system enters it. */
static const UBYTE probe_code[92] = {
    0x48, 0xe7, 0x40, 0xc2, 0x22, 0x08, 0x2c, 0x78, 0x00, 0x04, 0x93, 0xc9, 0x4e, 0xae, 0xfe, 0xda,
    0x22, 0x40, 0x22, 0x69, 0x00, 0x58, 0x20, 0x09, 0x67, 0x38, 0x0c, 0x91, 0x44, 0x54, 0x50, 0x48,
    0x66, 0x30, 0x52, 0xa9, 0x00, 0x08, 0x23, 0x41, 0x00, 0x0c, 0x23, 0x6f, 0x00, 0x14, 0x00, 0x10,
    0x20, 0x29, 0x00, 0x04, 0x20, 0x41, 0x4a, 0x81, 0x67, 0x1a, 0x08, 0x01, 0x00, 0x00, 0x66, 0x14,
    0x22, 0x00, 0x43, 0xe9, 0x00, 0x14, 0x70, 0x09, 0x22, 0xd8, 0x51, 0xc8, 0xff, 0xfc, 0x20, 0x01,
    0x60, 0x02, 0x70, 0x01, 0x4c, 0xdf, 0x43, 0x02, 0x4e, 0x75, 0x4e, 0x71 };

/* probe_code as a load file */
static UBYTE probe_seg[32 + 92 + 4];

static void make_probe_seg(void)
{
    static const ULONG head[8] = { 0x3f3, 0, 1, 0, 0, 92 / 4, 0x3e9, 92 / 4 };
    ULONG end = 0x3f2;
    memcpy(probe_seg, head, 32);
    memcpy(probe_seg + 32, probe_code, 92);
    memcpy(probe_seg + 32 + 92, &end, 4);
}


static UBYTE form[4096];
static ULONG flen;
static UBYTE *hook_file;                 /* HOOK=file: other recognition code for ODT_Hook */
static ULONG hook_file_len;

static void put32(UBYTE *p, ULONG v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void put16(UBYTE *p, UWORD v) { p[0] = v >> 8; p[1] = v; }

static ULONG chunk(ULONG id, ULONG n)
{
    ULONG at = flen + 8;
    put32(form + flen, id);
    put32(form + flen + 4, n);
    memset(form + at, 0, (n + 1) & ~1UL);
    flen = at + ((n + 1) & ~1UL);
    return at;
}

/* mask: '?' is -1, '^' makes the next byte's word 0x80xx */
static void desc(const char *file, const char *name, const char *base, const char *pat, const char *mask,
                 ULONG gid, ULONG id, UWORD flags, UWORD pri, int hook, int tools, const char *namechunk)
{
    ULONG ln = name ? strlen(name) + 1 : 0, lb = strlen(base) + 1, lp = pat ? strlen(pat) + 1 : 0;
    ULONG lm = 0, at, o = 32, i;
    char path[128];
    BPTR fh;
    const char *m;
    for (m = mask; m && *m; m++) { if (*m == '^') m++; lm++; }
    memcpy(form, "FORM\0\0\0\0DTYP", 12);
    flen = 12;
    if (namechunk) { at = chunk(ID_NAME, strlen(namechunk) + 1); memcpy(form + at, namechunk, strlen(namechunk) + 1); }
    at = chunk(ID_DTHD, 32 + ln + lb + lp + 2 * lm);
    if (name) { put32(form + at, o); memcpy(form + at + o, name, ln); o += ln; }
    put32(form + at + 4, o); memcpy(form + at + o, base, lb); o += lb;
    if (pat) { put32(form + at + 8, o); memcpy(form + at + o, pat, lp); o += lp; }
    if (lm) {
        put32(form + at + 12, o);
        for (m = mask, i = 0; *m; m++, i++) {
            UWORD w = *m == '?' ? 0xffff : (UBYTE)*m;
            if (*m == '^') w = 0x8000 | (UBYTE)*++m;
            put16(form + at + o + 2 * i, w);
        }
    }
    put32(form + at + 16, gid); put32(form + at + 20, id);
    put16(form + at + 24, lm); put16(form + at + 28, flags); put16(form + at + 30, pri);
    /* hook 1: probe_code as a load file (or HOOK=file), 2: probe_code bare */
    if (hook == 1 && hook_file) { at = chunk(ID_CODE, hook_file_len); memcpy(form + at, hook_file, hook_file_len); }
    else if (hook == 1) { at = chunk(ID_CODE, sizeof probe_seg); memcpy(form + at, probe_seg, sizeof probe_seg); }
    else if (hook == 2) { at = chunk(ID_CODE, sizeof probe_code); memcpy(form + at, probe_code, sizeof probe_code); }
    if (tools) {
        static const struct { UWORD which, flags; const char *prog; } t[3] = {
            { TW_BROWSE, TF_WORKBENCH, "SYS:Utilities/MultiView" }, { TW_EDIT, TF_SHELL, "C:Ed" },
            { TW_INFO, TF_RX, "REXX:info.rexx" } };
        for (i = 0; i < (tools == 2 ? 1U : 3U); i++) {
            ULONG lt = strlen(t[i].prog) + 1;
            at = chunk(ID_TOOL, 8 + lt);
            put16(form + at, t[i].which); put16(form + at + 2, t[i].flags); put32(form + at + 4, 8);
            memcpy(form + at + 8, t[i].prog, lt);
        }
    }
    put32(form + 4, flen - 8);
    strcpy(path, "DEVS:DataTypes/");
    strcat(path, file);
    if ((fh = Open(path, MODE_NEWFILE))) { Write(fh, form, flen); Close(fh); }
    else printf("couldn't write %s\n", path);
}

#define ID(a, b, c, d) MAKE_ID(a, b, c, d)

static void make_descriptors(void)
{
    desc("ODT_Case", "ODT Case", "odtcase", "#?", "QzQz", GID_SYSTEM, ID('O','D','T','a'), DTF_BINARY, 0, 0, 0, NULL);
    desc("ODT_CaseS", "ODT CaseS", "odtcases", "#?", "CsCs", GID_SYSTEM, ID('O','D','T','b'), DTF_BINARY | DTF_CASE, 0, 0, 0, NULL);
    desc("ODT_HiBit", "ODT HiBit", "odthibit", "#?", "^AB", GID_SYSTEM, ID('O','D','T','c'), DTF_BINARY | DTF_CASE, 0, 0, 0, NULL);
    desc("ODT_Long", "ODT Long", "odtlong", "#?", "LONGMASK", GID_SYSTEM, ID('O','D','T','d'), DTF_BINARY, 0, 0, 0, NULL);
    desc("ODT_TieS", "ODT TieS", "odties", "#?", "TIE", GID_SYSTEM, ID('O','D','T','e'), DTF_BINARY, 0, 0, 0, NULL);
    desc("ODT_TieL", "ODT TieL", "odtiel", "#?", "TIE!", GID_SYSTEM, ID('O','D','T','f'), DTF_BINARY, 0, 0, 0, NULL);
    desc("ODT_TieA", "ODT TieA", "odtiea", "#?", "TA", GID_SYSTEM, ID('O','D','T','g'), DTF_BINARY, 0, 0, 0, NULL);
    desc("ODT_TieB", "ODT TieB", "odtieb", "#?", "TA", GID_SYSTEM, ID('O','D','T','h'), DTF_BINARY, 0, 0, 0, NULL);
    desc("ODT_PriLo", "ODT PriLo", "odtprilo", "#?", "PR", GID_SYSTEM, ID('O','D','T','i'), DTF_BINARY, 0, 0, 0, NULL);
    desc("ODT_PriHi", "ODT PriHi", "odtprihi", "#?", "PR", GID_SYSTEM, ID('O','D','T','j'), DTF_BINARY, 5, 0, 0, NULL);
    desc("ODT_PriNeg", "ODT PriNeg", "odtprineg", "#?", "PN", GID_SYSTEM, ID('O','D','T','k'), DTF_BINARY, 0xffff, 0, 0, NULL);
    desc("ODT_PriOne", "ODT PriOne", "odtprione", "#?", "PN", GID_SYSTEM, ID('O','D','T','l'), DTF_BINARY, 1, 0, 0, NULL);
    desc("ODT_BinPat", "ODT BinPat", "odtbinpat", "#?.odtb", NULL, GID_SYSTEM, ID('O','D','T','m'), DTF_BINARY, 0, 0, 0, NULL);
    desc("ODT_AscPat", "ODT AscPat", "odtascpat", "#?.odta", NULL, GID_TEXT, ID('O','D','T','n'), DTF_ASCII, 0, 0, 0, NULL);
    desc("ODT_IffMask", "ODT IffMask", "odtiffmask", "#?", "FORM????ODTF", GID_SYSTEM, ID('O','D','T','F'), DTF_IFF, 0, 0, 0, NULL);
    desc("ODT_IffId", "ODT IffId", "odtiffid", "#?", NULL, GID_SYSTEM, ID('O','D','T','G'), DTF_IFF, 0, 0, 0, NULL);
    desc("ODT_IffOther", "ODT IffOther", "odtiffother", "#?", "FORM????ODTX", GID_SYSTEM, ID('O','D','T','H'), DTF_IFF, 0, 0, 0, NULL);
    desc("ODT_Hook", "ODT Hook", "odthook", "#?", "HOOK", GID_SYSTEM, ID('O','D','T','o'), DTF_BINARY | DTF_CASE, 0, 1, 0, NULL);
    desc("ODT_IffHook", "ODT IffHook", "odtiffhook", "#?", "FORM????ODTK", GID_SYSTEM, ID('O','D','T','K'), DTF_IFF, 0, 1, 0, NULL);
    desc("ODT_HookRaw", "ODT HookRaw", "odthookraw", "#?", "HKRW", GID_SYSTEM, ID('O','D','T','v'), DTF_BINARY | DTF_CASE, 0, 2, 0, NULL);
    desc("ODT_Tools", "ODT Tools", "odttools", "#?", "TOOLS", GID_SYSTEM, ID('O','D','T','p'), DTF_BINARY | DTF_CASE, 0, 0, 1, NULL);
    desc("ODT_AscMask", "ODT AscMask", "odtascmask", "#?", "odt-text:", GID_TEXT, ID('O','D','T','q'), DTF_ASCII, 0, 0, 0, NULL);
    desc("ODT_NoName", NULL, "odtnoname", "#?", "NN", GID_SYSTEM, ID('O','D','T','r'), DTF_BINARY | DTF_CASE, 0, 0, 0, "ODT FromNAME");
    desc("ODT_Exact", "ODT Exact", "odtexact", "odtexact.bin", NULL, GID_SYSTEM, ID('O','D','T','s'), DTF_BINARY, 0, 0, 0, NULL);
    desc("ODT_PatMask", "ODT PatMask", "odtpatmask", "#?.odtpm", "PM", GID_SYSTEM, ID('O','D','T','t'), DTF_BINARY | DTF_CASE, 0, 0, 0, NULL);
    desc("ODT_Misc", "ODT Misc", "odtmisc", "#?.odtmisc", NULL, GID_SYSTEM, ID('O','D','T','u'), DTF_MISC, 0, 0, 0, NULL);
    /* priority against age, and against mask length */
    desc("ODT_PQa", "ODT PQa", "odtpqa", "#?", "PQ", GID_SYSTEM, ID('O','D','T','w'), DTF_BINARY, 0, 0, 0, NULL);
    desc("ODT_PQb", "ODT PQb", "odtpqb", "#?", "PQ", GID_SYSTEM, ID('O','D','T','x'), DTF_BINARY, 5, 0, 0, NULL);
    desc("ODT_PLa", "ODT PLa", "odtpla", "#?", "PL!", GID_SYSTEM, ID('O','D','T','y'), DTF_BINARY, 0, 0, 0, NULL);
    desc("ODT_PLb", "ODT PLb", "odtplb", "#?", "PL", GID_SYSTEM, ID('O','D','T','z'), DTF_BINARY, 5, 0, 0, NULL);
    desc("ODT_PKa", "ODT PKa", "odtpka", "#?", "PK!", GID_SYSTEM, ID('O','D','T','I'), DTF_BINARY, 5, 0, 0, NULL);
    desc("ODT_PKb", "ODT PKb", "odtpkb", "#?", "PK", GID_SYSTEM, ID('O','D','T','J'), DTF_BINARY, 0, 0, 0, NULL);
    /* a mask-less "#?" binary type (newest of the mask-less ones) */
    desc("ODT_ZZAny", "ODT ZZAny", "odtzzany", "#?", NULL, GID_SYSTEM, ID('O','D','T','L'), DTF_BINARY, 0, 0, 0, NULL);
    /* a pattern without wildcards: case, and a mask beside it */
    desc("ODT_Lit", "ODT Lit", "odtlit", "LitFile.DAT", "LT", GID_SYSTEM, ID('O','D','T','A'), DTF_BINARY, 0, 0, 0, NULL);
    /* the name or the whole path? */
    desc("ODT_PathPat", "ODT PathPat", "odtpathpat", "#?ODTData#?", "PP", GID_SYSTEM, ID('O','D','T','B'), DTF_BINARY | DTF_CASE, 0, 0, 0, NULL);
    /* a pattern and no mask, binary and IFF (newer than ODT_Exact) */
    desc("ODT_ZPat", "ODT ZPat", "odtzpat", "#?.zp", NULL, GID_SYSTEM, ID('O','D','T','C'), DTF_BINARY, 0, 0, 0, NULL);
    desc("ODT_ZIff", "ODT ZIff", "odtziff", "#?.zi", NULL, GID_SYSTEM, ID('O','D','T','D'), DTF_IFF, 0, 0, 0, NULL);
    /* one DTTL chunk */
    desc("ODT_Tool1", "ODT Tool1", "odttool1", "#?", "TOOL1", GID_SYSTEM, ID('O','D','T','E'), DTF_BINARY | DTF_CASE, 0, 0, 2, NULL);
}

/* the data files: name, bytes (len -1: a C string) */
static const struct { const char *name; const char *data; LONG len; } files[] = {
    { "qzqz", "qZqZ\1\2", 6 }, { "casesA", "CsCs\1", 5 }, { "casesB", "cScS\1", 5 },
    { "hibitA", "AB\1", 3 }, { "hibitB", "\301B\1", 3 }, { "long7", "LONGMAS", 7 }, { "long8", "LONGMASK", 8 },
    { "tie", "TIE!\1\1", 6 }, { "ta", "TA\1", 3 }, { "pr", "PR\1", 3 }, { "pn", "PN\1", 3 },
    { "text.odtb", "hello\n", -1 }, { "bin.odta", "\1\2\3", 3 },
    { "iffF", "FORM\0\0\0\14ODTFNAME\0\0\0\0", 20 }, { "iffG", "FORM\0\0\0\14ODTGNAME\0\0\0\0", 20 },
    { "iffX", "FORM\0\0\0\14ODTXNAME\0\0\0\0", 20 }, { "iffZ", "FORM\0\0\0\14ZZZZNAME\0\0\0\0", 20 },
    { "iffK", "FORM\0\0\0\14ODTKNAME\0\0\0\0", 20 }, { "catZ", "CAT \0\0\0\14ZZZZNAME\0\0\0\0", 20 },
    { "listZ", "LIST\0\0\0\14ODTFNAME\0\0\0\0", 20 }, { "formshort", "FORM\0\0\0\4ODT", 11 },
    { "hook", NULL, 2000 }, { "hookraw", NULL, 2001 }, { "tools", "TOOLS\1", 6 }, { "ascmask", "ODT-TEXT: hi\n", -1 },
    { "nn", "NN\1", 3 }, { "odtexact.bin", "\1\2", 2 }, { "x.odtpm", "PM\1", 3 }, { "y.other", "PM\1", 3 },
    { "z.odtpm", "QQ\1", 3 }, { "w.odtmisc", "\1\2", 2 },
    { "t_plain", "Hello world, plain text.\n", -1 }, { "t_bell", "Ring \7 the bell\n", -1 },
    { "t_latin", "caf\351 cr\350me\n", -1 }, { "t_c1", "next \205 line\n", -1 }, { "t_crlf", "line\r\nline\r\n", -1 },
    { "t_esc", "\33[1mbold\33[0m\n", -1 }, { "t_tab", "a\tb\tc\n", -1 }, { "t_del", "rub\177out\n", -1 },
    { "t_ff", "page\fnext\n", -1 }, { "t_vt", "a\13b\n", -1 }, { "t_short", "a", 1 }, { "t_empty", "", 0 },
    { "t_utf8", "h\303\251llo\n", -1 }, { "t_nonl", "no newline", -1 }, { "t_ctrlz", "end\32", -1 },
    { "t_nul40", NULL, -40 }, { "t_nul70", NULL, -70 }, { "t_nul130", NULL, -130 }, { "t_nul300", NULL, -300 },
    { "t_nul600", NULL, -600 }, { "t_nul1100", NULL, -1100 }, { "t_nul5000", NULL, -5000 },
    { "bin4", "\0\1\2\3", 4 }, { "one0", "\0", 1 },
    { "pq", "PQ\1", 3 }, { "pl", "PL!\1", 4 }, { "lt1", "LT\1", 3 }, { "litfile.dat", "LT\1", 3 },
    { "pp", "PP\1", 3 }, { "a.zp", "\1\2", 2 }, { "a.zi", "FORM\0\0\0\14ZZZZNAME\0\0\0\0", 20 },
    { "tool1", "TOOL1\1", 6 }, { "hibitC", "ZB\1", 3 }, { "t_bs", "a\bb\n", -1 }, { "form4", "FORM", 4 },
    { "prop", "PROP\0\0\0\4ILBM", 12 }, { "t_nul1000", NULL, -1000 }, { "t_nul1083", NULL, -1083 },
    { "t_nul1084", NULL, -1084 }, { "pk", "PK!\1", 4 }, { "form5", "FORM\0", 5 }, { "form6", "FORM\0\0", 6 },
    { "form7", "FORM\0\0\0", 7 }, { "form8", "FORM\0\0\0\0", 8 }, { "cat5", "CAT \0", 5 },
};
#define NFILES (sizeof files / sizeof files[0])

static UBYTE data[6000];

/* a file's bytes into data[]; its length */
static LONG file_bytes(int i)
{
    LONG len = files[i].len;
    if (files[i].data) {
        if (len < 0) len = strlen(files[i].data);
        memcpy(data, files[i].data, len);
    } else if (len > 0) {                   /* "hook", "hookraw": HOOK or HKRW and then 0x01s */
        memset(data, 1, len);
        memcpy(data, len == 2001 ? "HKRW" : "HOOK", 4);
    } else {                                /* t_nulN: text with a NUL at N, 1200 bytes or N+100 */
        LONG at = -len, j;
        len = at + 100 < 1200 ? 1200 : at + 100;
        for (j = 0; j < len; j++) data[j] = j % 50 == 49 ? '\n' : 'a' + j % 26;
        data[at] = 0;
    }
    return len;
}

static char data_dir[128] = "T:ODTData";

static void make_files(void)
{
    BPTR l, fh, old;
    unsigned i;
    if ((l = CreateDir(data_dir))) UnLock(l);
    if (!(l = Lock(data_dir, ACCESS_READ))) { printf("no %s\n", data_dir); return; }
    old = CurrentDir(l);
    for (i = 0; i < NFILES; i++) {
        LONG len = file_bytes(i);
        if ((fh = Open(files[i].name, MODE_NEWFILE))) { Write(fh, data, len); Close(fh); }
    }
    CurrentDir(old);
    UnLock(l);
}

static struct dtprobe rec;

/* the rest of a line: what recognition code was given, when it ran. The
 * context (when A0 was one) as DTHookContext lays it out: the four bases,
 * lock, FIB, file handle, IFF handle, buffer, buffer length. */
static ULONG abs_exec(void)
{
    ULONG v;
    __asm volatile ("move.l 4.w,%0" : "=d"(v));
    return v;
}

static int mem_ok(const void *p, ULONG n);
static int mem_case;                     /* recognising memory: say what the context's file is */

/* a message port's task name */
static const char *port_task(const struct MsgPort *mp)
{
    const struct Task *t;
    if (!mem_ok(mp, sizeof *mp)) return "?";
    t = mp->mp_SigTask;
    if (!mem_ok(t, sizeof *t) || !mem_ok(t->tc_Node.ln_Name, 1)) return "?";
    return t->tc_Node.ln_Name;
}

/* what the lock, FIB and handle given with memory were (read after the
 * call: they may be freed by then, so only read, and only checked memory) */
static void mem_ctx(const ULONG *c)
{
    const struct FileLock *fl = (const struct FileLock *)BADDR((BPTR)c[4]);
    const struct FileInfoBlock *fib = (const struct FileInfoBlock *)c[5];
    const struct FileHandle *fh = (const struct FileHandle *)BADDR((BPTR)c[6]);
    if (c[4] && mem_ok(fl, sizeof *fl)) {
        const struct DosList *vol = (const struct DosList *)BADDR(fl->fl_Volume);
        const UBYTE *vn = mem_ok(vol, sizeof *vol) ? (const UBYTE *)BADDR(vol->dol_Name) : NULL;
        out(" lockport %s vol %.*s", port_task(fl->fl_Task), mem_ok(vn, 1) ? vn[0] : 1,
            mem_ok(vn, 1) ? (const char *)vn + 1 : "?");
    }
    if (fib && mem_ok(fib, sizeof *fib))
        /* not fib_DirEntryType: read after the call it is whatever the freed
         * FIB's memory holds by then (OS 3.2 gave 1 or 3, ours 0) */
        out(" fibname \"%.30s\" fibsize %ld", fib->fib_FileName, (long)fib->fib_Size);
    if (c[6] && mem_ok(fh, sizeof *fh)) out(" fhport %s", port_task(fh->fh_Type));
}

static void end(void)
{
    if (rec.calls) {
        ULONG *c = rec.ctx;
        /* not the stack: OS 3.2 puts something else there, ours the context
         * as well, for recognition code written with stack arguments */
        out(" | hook calls %lu a0 %s", (unsigned long)rec.calls,
            !rec.a0 ? "0" : rec.a0 & 1 ? "odd" : "set");
        if (rec.a0 && !(rec.a0 & 1))
            out(" sys %s dos %d iffparse %d utility %d lock %d fib %d fh %d iff %d buf %d len %lu",
                c[0] == abs_exec() ? "ok" : "other", c[1] != 0, c[2] != 0, c[3] != 0, c[4] != 0,
                c[5] != 0, c[6] != 0, c[7] != 0, c[8] != 0, (unsigned long)c[9]);
        if (mem_case && rec.a0 && !(rec.a0 & 1)) mem_ctx(c);
    }
    out("\n");
}

static struct DataType *obtain(ULONG type, APTR handle, struct TagItem *tags, ULONG answer)
{
    struct Task *me = FindTask(NULL);
    APTR ud = me->tc_UserData;
    struct DataType *dt;
    memset(&rec, 0, sizeof rec);
    rec.magic = DTPROBE_MAGIC;
    rec.answer = answer;
    me->tc_UserData = &rec;
    step("ObtainDataTypeA type %lu, hook answers %lu", (unsigned long)type, (unsigned long)answer);
    SetIoErr(0);
    dt = ObtainDataTypeA(type, handle, tags);
    last_err = IoErr();
    step("  back: %s", dt ? "a type" : "NULL");
    me->tc_UserData = ud;
    return dt;
}

static void desc_cases(void)
{
    BPTR dir = Lock(data_dir, ACCESS_READ), old, l;
    unsigned i, pass;
    struct DataType *dt;
    if (!dir) { out("no %s\n", data_dir); return; }
    old = CurrentDir(dir);
    for (i = 0; i < NFILES; i++)
        for (pass = 0; pass < 2; pass++) {
            /* the second pass, only for recognition code: the hook says no */
            ULONG answer = !pass;
            LONG len = file_bytes(i);
            struct TagItem mt[3];
            if (pass && strncmp(files[i].name, "hook", 4) && strcmp(files[i].name, "iffK")) break;
            step("file %s", files[i].name);
            out("file %s%s", files[i].name, answer ? "" : " (hook says no)");
            if ((l = Lock(files[i].name, ACCESS_READ))) {
                dt = obtain(DTST_FILE, (APTR)l, NULL, answer);
                out_dt(dt); end();
                if (dt) ReleaseDataType(dt);
                UnLock(l);
            } else out(" no file\n");
            mt[0].ti_Tag = DTA_SourceAddress; mt[0].ti_Data = (ULONG)data;
            mt[1].ti_Tag = DTA_SourceSize; mt[1].ti_Data = len;
            mt[2].ti_Tag = TAG_DONE;
            out("mem %s%s", files[i].name, answer ? "" : " (hook says no)");
            dt = obtain(DTST_MEMORY, NULL, mt, answer);
            mem_case = 1;
            out_dt(dt); end();
            mem_case = 0;
            if (dt) ReleaseDataType(dt);
        }
    /* the directory itself, and memory with no size or no address */
    out("dir %s", data_dir);
    dt = obtain(DTST_FILE, (APTR)dir, NULL, 1);
    out_dt(dt); end();
    if (dt) ReleaseDataType(dt);
    {
        struct TagItem t0[] = { { DTA_SourceAddress, (ULONG)"HOOK\1\1" }, { DTA_SourceSize, 0 }, { TAG_DONE, 0 } };
        struct TagItem t1[] = { { DTA_SourceSize, 6 }, { TAG_DONE, 0 } };
        struct TagItem t2[] = { { DTA_SourceAddress, (ULONG)"GIF89a\1\0\1\0\0\0\0" }, { TAG_DONE, 0 } };
        out("mem size 0"); dt = obtain(DTST_MEMORY, NULL, t0, 1); out_dt(dt); end(); if (dt) ReleaseDataType(dt);
        out("mem no address"); dt = obtain(DTST_MEMORY, NULL, t1, 1); out_dt(dt); end(); if (dt) ReleaseDataType(dt);
        out("mem no size"); dt = obtain(DTST_MEMORY, NULL, t2, 1); out_dt(dt); end(); if (dt) ReleaseDataType(dt);
        out("mem no tags"); dt = obtain(DTST_MEMORY, NULL, NULL, 1); out_dt(dt); end(); if (dt) ReleaseDataType(dt);
    }
    /* every byte value in text, from memory: a ascii, b binary, i iff, m misc, - none */
    {
        unsigned b;
        static const char k[] = "baim";
        for (b = 0; b < 256; b++) {
            struct TagItem mt[] = { { DTA_SourceAddress, (ULONG)data }, { DTA_SourceSize, 6 }, { TAG_DONE, 0 } };
            if (b % 64 == 0) out("bytes %02x", b);
            memcpy(data, "ab?cd\n", 6);
            data[2] = b;
            SetIoErr(0);
            dt = ObtainDataTypeA(DTST_MEMORY, NULL, mt);
            out("%c", dt ? k[dt->dtn_Header->dth_Flags & 3] : '-');
            if (dt) ReleaseDataType(dt);
            if (b % 64 == 63) out("\n");
        }
    }
    out("type 0"); dt = obtain(0, (APTR)"ilbm", NULL, 1); out_dt(dt); end(); if (dt) ReleaseDataType(dt);
    out("type 99"); dt = obtain(99, (APTR)"ilbm", NULL, 1); out_dt(dt); end(); if (dt) ReleaseDataType(dt);
    CurrentDir(old);
    UnLock(dir);
}

/* the order the made descriptors are in on the disk, for reading ties */
static void desc_order(void)
{
    struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
    BPTR dir = Lock("DEVS:DataTypes", ACCESS_READ);
    printf("DEVS:DataTypes order of ODT_ files:");
    if (dir && fib && Examine(dir, fib))
        while (ExNext(dir, fib))
            if (!strncmp(fib->fib_FileName, "ODT_", 4)) printf(" %s", fib->fib_FileName + 4);
    printf("\n");
    if (dir) UnLock(dir);
    if (fib) FreeDosObject(DOS_FIB, fib);
}

/* ---- SCAN=dir ------------------------------------------------------------------ */

static void scan(const char *path)
{
    struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
    BPTR dir = Lock(path, ACCESS_READ);
    int n = 0;
    if (dir && fib && Examine(dir, fib)) {
        BPTR old = CurrentDir(dir);
        while (ExNext(dir, fib) && n++ < 400) {
            BPTR l = Lock(fib->fib_FileName, ACCESS_READ);
            struct DataType *dt;
            step("scan %s", fib->fib_FileName);
            out("scan %s", fib->fib_FileName);
            if (!l) { out(" no lock\n"); continue; }
            dt = obtain(DTST_FILE, (APTR)l, NULL, 1);
            if (dt) {
                out(" \"%s\" base \"%s\" gid ", s(dt->dtn_Header->dth_Name), s(dt->dtn_Header->dth_BaseName));
                id4(dt->dtn_Header->dth_GroupID);
                out("\n");
                ReleaseDataType(dt);
            } else out(" NULL ioerr %ld\n", (long)last_err);
            UnLock(l);
        }
        CurrentDir(old);
    } else out("scan %s: no such directory\n", path);
    if (dir) UnLock(dir);
    if (fib) FreeDosObject(DOS_FIB, fib);
}

/* ---- LIST: OS 3.2's own list, for study ------------------------------------ */

static int mem_ok(const void *p, ULONG n)
{
    return p && !((ULONG)p & 1) && TypeOfMem((APTR)p) && TypeOfMem((APTR)((const UBYTE *)p + n - 1));
}

static void hex(const UBYTE *p, int n)
{
    int i;
    for (i = 0; i < n; i++) printf("%02x%s", p[i], i % 4 == 3 ? " " : "");
    printf("\n");
}

static void list_dump(void)
{
    struct SignalSemaphore *ss;
    UBYTE *p;
    int off;
    Forbid();
    ss = FindSemaphore((STRPTR)"DataTypesList");
    Permit();
    printf("LIST: DataTypesList semaphore at %p\n", (void *)ss);
    if (!ss) return;
    ObtainSemaphoreShared(ss);
    p = (UBYTE *)ss;
    printf("LIST: first 128 bytes: ");
    hex(p, 128);
    for (off = (int)sizeof(struct SignalSemaphore); off <= (int)sizeof(struct SignalSemaphore) + 72; off += 2) {
        struct MinList *ml = (struct MinList *)(p + off);
        struct MinNode *n;
        int count = 0;
        if (!mem_ok(ml->mlh_Head, 8) || ml->mlh_Tail != NULL) continue;
        if ((UBYTE *)ml->mlh_TailPred != (UBYTE *)ml && !mem_ok(ml->mlh_TailPred, 8)) continue;
        printf("LIST: a list at +%d:", off);
        for (n = ml->mlh_Head; mem_ok(n, 16) && n->mln_Succ && count < 200; n = n->mln_Succ, count++) {
            struct Node *nn = (struct Node *)n;
            struct DataType *d1 = (struct DataType *)n, *d2 = (struct DataType *)((UBYTE *)n - 14);
            const char *name = mem_ok(nn->ln_Name, 1) ? nn->ln_Name : "?";
            int which = 0;
            if (mem_ok(d1->dtn_Header, 32) && (APTR)d1->dtn_Header->dth_Name == (APTR)nn->ln_Name) which = 1;
            else if (mem_ok(d2, 58) && mem_ok(d2->dtn_Header, 32) && (APTR)d2->dtn_Header->dth_Name == (APTR)nn->ln_Name) which = 2;
            printf(" [%s n%d pri %d]", name, which, nn->ln_Pri);
            if (count < 3 && which) {
                struct DataType *d = which == 1 ? d1 : d2;
                printf("\nLIST:   %s: DataType and the 96 bytes after: ", name);
                if (mem_ok(d, 58 + 96)) hex((UBYTE *)d, 58 + 96);
                printf("LIST:   header at DataType%+ld, fn \"%s\", length %lu\n",
                       (long)((UBYTE *)d->dtn_Header - (UBYTE *)d),
                       mem_ok(d->dtn_FunctionName, 1) ? (const char *)d->dtn_FunctionName : "(none)", (unsigned long)d->dtn_Length);
            }
        }
        printf(" (%d)\n", count);
    }
    ReleaseSemaphore(ss);
}

/* ---- OBJ: the object calls and datatypesclass (step D2) ----------------------- */

/* Data files made here (no copyrighted media): a text, a 4x2 ILBM of one
 * plane, bytes nothing claims; the clipboard gets an FTXT. Each answer is
 * a line without addresses. */
static const UBYTE ilbm_bytes[] = {
    'F','O','R','M', 0,0,0,74, 'I','L','B','M',
    'B','M','H','D', 0,0,0,20, 0,4, 0,2, 0,0, 0,0, 1, 0, 0, 0, 0,0, 10,11, 0,4, 0,2,
    'C','M','A','P', 0,0,0,6, 0,0,0, 255,255,255,
    'B','O','D','Y', 0,0,0,4, 0xa0,0, 0x50,0,
    'A','N','N','O', 0,0,0,8, 'o','d','t',' ','a','b','i',0
};
static const char text_bytes[] = "GFOpenDatatypes ABI test\nline two\nline three\n";
static const char *const obj_text = "T:ODT_text.txt";
static const char *const obj_ilbm = "T:ODT_pic.ilbm";
static const char *const obj_bin = "T:ODT_bytes.bin";

static void write_file(const char *name, const void *p, LONG n)
{
    BPTR fh = Open(name, MODE_NEWFILE);
    if (fh) { Write(fh, (APTR)p, n); Close(fh); }
}

static LONG file_size(const char *name)
{
    BPTR l = Lock(name, ACCESS_READ);
    struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
    LONG n = -1;
    if (l && fib && Examine(l, fib)) n = fib->fib_Size;
    if (fib) FreeDosObject(DOS_FIB, fib);
    if (l) UnLock(l);
    return n;
}

static void obj_class(Object *o)
{
    struct IClass *cl;
    out(" classes");
    for (cl = OCLASS(o); cl; cl = cl->cl_Super) {
        const char *id = cl->cl_ID ? (const char *)cl->cl_ID : "(private)";
        /* the twin's own class answers as OS 3.2's does */
        if (!strcmp(id, "opendatatypesclass")) id = "datatypesclass";
        out(" %s", id);
    }
}

static void obj_attrs(const char *what, Object *o)
{
    ULONG tv = 7, vv = 7, tov = 7, th = 7, vh = 7, toh = 7, vu = 7, hu = 7, st = 7, h = 7, dt = 7, dom = 7;
    ULONG busy = 7, sync = 7, methods = 0, trig = 7, nothing = 7, name = 7, gid = 7, base = 7, fi = 7;
    ULONG n;
    struct Gadget *g = (struct Gadget *)o;
    struct DTSpecialInfo *si = g->SpecialInfo;
    step("GetDTAttrs %s", what);
    n = GetDTAttrs(o, DTA_TopVert, (ULONG)&tv, DTA_VisibleVert, (ULONG)&vv, DTA_TotalVert, (ULONG)&tov,
                   DTA_TopHoriz, (ULONG)&th, DTA_VisibleHoriz, (ULONG)&vh, DTA_TotalHoriz, (ULONG)&toh,
                   DTA_VertUnit, (ULONG)&vu, DTA_HorizUnit, (ULONG)&hu, DTA_SourceType, (ULONG)&st,
                   DTA_Handle, (ULONG)&h, DTA_DataType, (ULONG)&dt, DTA_Domain, (ULONG)&dom, DTA_Busy, (ULONG)&busy,
                   DTA_Sync, (ULONG)&sync, DTA_Methods, (ULONG)&methods, DTA_TriggerMethods, (ULONG)&trig,
                   TAG_USER + 0x7700099UL, (ULONG)&nothing, DTA_Name, (ULONG)&name, DTA_GroupID, (ULONG)&gid,
                   DTA_BaseName, (ULONG)&base, DTA_FrameInfo, (ULONG)&fi, TAG_DONE);
    out("OBJ %s getattrs %lu: top %ld/%ld vis %ld/%ld tot %ld/%ld unit %ld/%ld st %lu handle %s dt %s domain %s"
        " busy %lu sync %lu trig %s nothing %lu name %s gid ", what, (unsigned long)n, (long)tv, (long)th,
        (long)vv, (long)vh, (long)tov, (long)toh, (long)vu, (long)hu, (unsigned long)st,
        h == 7 ? "unset" : h ? "set" : "NULL", dt == 7 ? "unset" : dt ? "set" : "NULL",
        dom == 7 ? "unset" : dom ? "set" : "NULL", (unsigned long)busy, (unsigned long)sync,
        trig == 7 ? "unset" : trig ? "set" : "NULL", (unsigned long)nothing,
        name == 7 ? "unset" : name ? "set" : "NULL");
    id4(gid);
    out(" base %s fi %s si %s", base == 7 || !base ? "-" : (const char *)base, fi == 7 ? "unset" : fi ? "set" : "NULL",
        si ? "set" : "NULL");
    if (si) out(" siflags %08lx units %ld/%ld", (unsigned long)si->si_Flags, (long)si->si_VertUnit, (long)si->si_HorizUnit);
    obj_class(o);
    out("\nOBJ %s methods", what);
    if (methods) {
        const ULONG *m;
        for (m = (const ULONG *)methods; *m != ~0UL; m++) out(" %lx", (unsigned long)*m);
    }
    out(" gadget flags %04x act %04x type %04x\n", g->Flags, g->Activation, g->GadgetType);
}

static Object *obj_new(const char *what, APTR name, ULONG tag1, ...)
{
    Object *o;
    va_list ap;
    struct TagItem t[8];
    int i = 0;
    ULONG tag = tag1;
    va_start(ap, tag1);
    while (tag != TAG_DONE && i < 7) {
        t[i].ti_Tag = tag;
        t[i++].ti_Data = va_arg(ap, ULONG);
        tag = va_arg(ap, ULONG);
    }
    va_end(ap);
    t[i].ti_Tag = TAG_DONE;
    step("NewDTObject %s", what);
    SetIoErr(0);
    o = NewDTObjectA(name, t);
    last_err = IoErr();
    out("OBJ %s new %s ioerr %ld", what, o ? "ok" : "NULL", (long)last_err);
    if (o) obj_class(o);
    out("\n");
    return o;
}

static void obj_dispose(const char *what, Object *o)
{
    if (!o) return;
    step("DisposeDTObject %s", what);
    DisposeDTObject(o);
}

static void obj_file_cases(const char *what, const char *file)
{
    Object *o = obj_new(what, (APTR)file, TAG_DONE);
    ULONG r, tv = 7;
    struct dtWrite w;
    BPTR fh;
    if (!o) return;
    obj_attrs(what, o);
    step("SetDTAttrs %s top", what);
    r = SetDTAttrs(o, NULL, NULL, DTA_TopVert, 1000, DTA_TopHoriz, (ULONG)-5, TAG_DONE);
    GetDTAttrs(o, DTA_TopVert, (ULONG)&tv, TAG_DONE);
    out("OBJ %s settop %lu then %ld\n", what, (unsigned long)r, (long)tv);
    step("StartDragSelect %s", what);
    out("OBJ %s startdragselect %lu", what, (unsigned long)StartDragSelect(o));
    out(" again %lu\n", (unsigned long)StartDragSelect(o));
    step("GetDTMethods %s", what);
    out("OBJ %s getdtmethods %s triggers %s\n", what, GetDTMethods(o) ? "set" : "NULL", GetDTTriggerMethods(o) ? "set" : "NULL");
    step("DTM_WRITE raw %s", what);
    if ((fh = Open("T:ODT_out", MODE_NEWFILE))) {
        w.MethodID = DTM_WRITE; w.dtw_GInfo = NULL; w.dtw_FileHandle = fh; w.dtw_Mode = DTWM_RAW; w.dtw_AttrList = NULL;
        SetIoErr(0);
        r = DoDTMethodA(o, NULL, NULL, (Msg)&w);
        last_err = IoErr();
        Close(fh);
        out("OBJ %s write raw %lu ioerr %ld size %ld\n", what, (unsigned long)r, (long)last_err, (long)file_size("T:ODT_out"));
    }
    step("SaveDTObjectA %s", what);
    DeleteFile("T:ODT_save");
    SetIoErr(0);
    r = SaveDTObjectA(o, NULL, NULL, "T:ODT_save", DTWM_RAW, FALSE, NULL);
    last_err = IoErr();
    out("OBJ %s save raw %lu ioerr %ld size %ld\n", what, (unsigned long)r, (long)last_err, (long)file_size("T:ODT_save"));
    step("DTM_CLEARSELECTED %s", what);
    out("OBJ %s clearselected %lu\n", what, (unsigned long)DoDTMethod(o, NULL, NULL, DTM_CLEARSELECTED, NULL));
    obj_dispose(what, o);
}

/* where each slot jumps: the private slots told apart from the null one */
static void obj_slots(void)
{
    static const int slots[] = { 30, 120, 126, 132, 144, 150, 156, 162, 168, 174, 180, 186, 192, 198, 204, 210,
                                 216, 222, 228, 234, 240, 246, 252 };
    const UBYTE *b = (const UBYTE *)DataTypesBase;
    ULONG null_at = 0;
    unsigned i;
    out("OBJ slots");
    for (i = 0; i < sizeof slots / sizeof slots[0]; i++) {
        const UBYTE *v = b - slots[i];
        ULONG at = (v[0] == 0x4e && v[1] == 0xf9) ? ((ULONG)v[2] << 24 | (ULONG)v[3] << 16 | (ULONG)v[4] << 8 | v[5]) : 0;
        if (i == 0) null_at = at;
        out(" -%d:%s", slots[i], !at ? "?" : i && at == null_at ? "null" : "own");
    }
    out("\n");
}

static void obj_clip(void)
{
    struct IFFHandle *iff;
    struct ClipboardHandle *ch;
    struct Library *IFFParseBase = OpenLibrary("iffparse.library", 39);
    Object *o;
    if (!IFFParseBase) return;
    step("clipboard FTXT");
    if ((iff = AllocIFF())) {
        if ((ch = OpenClipboard(0))) {
            iff->iff_Stream = (ULONG)ch;
            InitIFFasClip(iff);
            if (!OpenIFF(iff, IFFF_WRITE)) {
                PushChunk(iff, MAKE_ID('F','T','X','T'), ID_FORM, IFFSIZE_UNKNOWN);
                PushChunk(iff, 0, MAKE_ID('C','H','R','S'), IFFSIZE_UNKNOWN);
                WriteChunkBytes(iff, (APTR)"clip text", 9);
                PopChunk(iff);
                PopChunk(iff);
                CloseIFF(iff);
            }
            CloseClipboard(ch);
        }
        FreeIFF(iff);
    }
    o = obj_new("clip", (APTR)0, DTA_SourceType, DTST_CLIPBOARD, TAG_DONE);
    if (o) obj_attrs("clip", o);
    obj_dispose("clip", o);
    /* ObtainDataTypeA with an IFFHandle on the clipboard */
    if ((iff = AllocIFF())) {
        if ((ch = OpenClipboard(0))) {
            iff->iff_Stream = (ULONG)ch;
            InitIFFasClip(iff);
            if (!OpenIFF(iff, IFFF_READ)) {
                struct DataType *dt;
                step("ObtainDataTypeA clipboard");
                SetIoErr(0);
                dt = ObtainDataTypeA(DTST_CLIPBOARD, iff, NULL);
                last_err = IoErr();
                out("OBJ obtain clipboard"); out_dt(dt); out("\n");
                if (dt) ReleaseDataType(dt);
                CloseIFF(iff);
            }
            CloseClipboard(ch);
        }
        FreeIFF(iff);
    }
    CloseLibrary(IFFParseBase);
}

static void obj_cases(void)
{
    static const struct { const char *what; ULONG gid; } rams[] = {
        { "ram-pict", GID_PICTURE }, { "ram-soun", GID_SOUND }, { "ram-inst", GID_INSTRUMENT },
        { "ram-text", GID_TEXT }, { "ram-anim", GID_ANIMATION }, { "ram-movi", GID_MOVIE },
        { "ram-docu", GID_DOCUMENT }, { "ram-syst", GID_SYSTEM }, { "ram-musi", GID_MUSIC },
    };
    unsigned i;
    Object *o;
    write_file(obj_text, text_bytes, sizeof text_bytes - 1);
    write_file(obj_ilbm, ilbm_bytes, sizeof ilbm_bytes);
    {
        UBYTE b[300];
        for (i = 0; i < sizeof b; i++) b[i] = (UBYTE)(i * 37 + 0x81);
        write_file(obj_bin, b, sizeof b);
    }
    obj_slots();
    obj_file_cases("text", obj_text);
    obj_file_cases("ilbm", obj_ilbm);
    obj_dispose("missing", obj_new("missing", (APTR)"T:ODT_no_such_file", TAG_DONE));
    obj_dispose("bytes", obj_new("bytes", (APTR)obj_bin, TAG_DONE));
    obj_dispose("wronggroup", obj_new("wronggroup", (APTR)obj_ilbm, DTA_GroupID, GID_SOUND, TAG_DONE));
    obj_dispose("rightgroup", obj_new("rightgroup", (APTR)obj_ilbm, DTA_GroupID, GID_PICTURE, TAG_DONE));
    obj_dispose("noname", obj_new("noname", NULL, TAG_DONE));
    obj_dispose("hotlink", obj_new("hotlink", (APTR)obj_text, DTA_SourceType, DTST_HOTLINK, TAG_DONE));
    for (i = 0; i < sizeof rams / sizeof rams[0]; i++) {
        o = obj_new(rams[i].what, NULL, DTA_SourceType, DTST_RAM, DTA_GroupID, rams[i].gid, TAG_DONE);
        if (o) obj_attrs(rams[i].what, o);
        obj_dispose(rams[i].what, o);
    }
    o = obj_new("mem-ilbm", NULL, DTA_SourceType, DTST_MEMORY, DTA_SourceAddress, (ULONG)ilbm_bytes,
                DTA_SourceSize, sizeof ilbm_bytes, TAG_DONE);
    if (o) obj_attrs("mem-ilbm", o);
    obj_dispose("mem-ilbm", o);
    o = obj_new("mem-text", NULL, DTA_SourceType, DTST_MEMORY, DTA_SourceAddress, (ULONG)text_bytes,
                DTA_SourceSize, sizeof text_bytes - 1, TAG_DONE);
    if (o) obj_attrs("mem-text", o);
    obj_dispose("mem-text", o);
    obj_clip();
    step("GetDTAttrs NULL");
    out("OBJ null getattrs %lu\n", (unsigned long)GetDTAttrsA(NULL, NULL));
    step("DisposeDTObject NULL");
    DisposeDTObject(NULL);
    out("OBJ null dispose ok\n");
}

/* ---- the run ------------------------------------------------------------------- */

static int do_desc, do_obj;
static const char *scan_dir;

static void run_set(void)
{
    out_len = 0;
    strings();
    trigger_flags();
    methods();
    ram_names();
    system_types();
    if (do_desc) desc_cases();
    if (do_obj) obj_cases();
    if (scan_dir) scan(scan_dir);
    out_buf[out_len] = 0;
}

static int word(const char *a, const char *w, int prefix)
{
    return same_ci(a, w, prefix ? strlen(w) : 0);
}

static void load_hook(const char *path)
{
    BPTR fh = Open(path, MODE_OLDFILE);
    if (!fh) { printf("no %s: ODT_Hook gets the built-in code\n", path); return; }
    if ((hook_file = AllocVec(4096, MEMF_ANY))) {
        LONG n = Read(fh, hook_file, 4096);
        hook_file_len = n > 0 && n < 4096 ? n : 0;
        if (!hook_file_len) printf("%s: not read (bigger than 4096 bytes?)\n", path);
    }
    Close(fh);
}

int main(int argc, char **argv)
{
    struct Library *os_base = NULL, *our_base = NULL;
    char *os_out = NULL, path[256];
    int i, probe = 0, list = 0, rc = 0;
    ULONG diffs = 0, lines = 0;
    const char *dir = NULL, *hook = NULL;

    for (i = 1; i < argc; i++) {
        if (word(argv[i], "PROBE", 0)) probe = 1;
        else if (word(argv[i], "DESC", 0)) do_desc = 1;
        else if (word(argv[i], "OBJ", 0)) do_obj = 1;
        else if (word(argv[i], "LIST", 0)) list = 1;
        else if (word(argv[i], "LIB=", 1)) dir = argv[i] + 4;
        else if (word(argv[i], "DIR=", 1)) { strncpy(data_dir, argv[i] + 4, sizeof data_dir - 1); }
        else if (word(argv[i], "SCAN=", 1)) scan_dir = argv[i] + 5;
        else if (word(argv[i], "HOOK=", 1)) hook = argv[i] + 5;
        else if (word(argv[i], "QUIET", 0)) steps = 0;
    }
    /* a DOS requester would wait for a click nobody makes: fail instead */
    ((struct Process *)FindTask(NULL))->pr_WindowPtr = (APTR)-1;
    out_buf = AllocVec(OUTSIZE, MEMF_ANY);
    os_out = AllocVec(OUTSIZE, MEMF_ANY);
    if (!out_buf || !os_out) { printf("no memory\n"); rc = 20; goto done; }
    if (!(os_base = OpenLibrary("datatypes.library", 39))) { printf("no datatypes.library\n"); rc = 20; goto done; }
    printf("datatypes.library %d.%d\n", os_base->lib_Version, os_base->lib_Revision);

    if (do_desc) {
        make_probe_seg();
        if (hook) load_hook(hook);
        make_descriptors();
        make_files();
        desc_order();
        step("AddDataTypes");
        if (SystemTags("C:AddDataTypes DEVS:DataTypes/ODT_#? QUIET", TAG_DONE))
            printf("AddDataTypes failed\n");
    }
    /* ours after the descriptors are on the disk: it reads them the first
     * time it is asked */
    if (dir) {
        strcpy(path, dir); AddPart(path, "gfopendatatypes.library", sizeof path);
        our_base = OpenLibrary(path, 47);
    } else our_base = OpenLibrary("gfopendatatypes.library", 47);
    if (our_base) printf("gfopendatatypes.library %d.%d\n", our_base->lib_Version, our_base->lib_Revision);
    fflush(stdout);

    DataTypesBase = os_base;
    step("the set through OS 3.2's library");
    run_set();
    if (probe || !our_base) {
        fwrite(out_buf, 1, out_len, stdout);
        printf("%s: %lu bytes of answers\n", our_base ? "PROBE" : "no gfopendatatypes.library, OS 3.2's answers",
               (unsigned long)out_len);
    } else {
        ULONG os_len = out_len;
        char *a, *b, *ae, *be;
        memcpy(os_out, out_buf, os_len + 1);
        DataTypesBase = our_base;
        step("the set through ours");
        run_set();
        for (a = os_out, b = out_buf; *a || *b; a = ae + (*ae != 0), b = be + (*be != 0)) {
            ae = strchr(a, '\n'); if (!ae) ae = a + strlen(a);
            be = strchr(b, '\n'); if (!be) be = b + strlen(b);
            lines++;
            if (ae - a != be - b || memcmp(a, b, ae - a)) {
                diffs++;
                printf("DIFF\n  OS:   %.*s\n  ours: %.*s\n", (int)(ae - a), a, (int)(be - b), b);
            }
        }
        printf("%lu lines, %lu differ\n", (unsigned long)lines, (unsigned long)diffs);
    }
    fflush(stdout);
    if (list) { step("LIST"); list_dump(); }
    if (diffs) rc = 5;
done:
    if (our_base) CloseLibrary(our_base);
    if (os_base) CloseLibrary(os_base);
    if (hook_file) FreeVec(hook_file);
    if (os_out) FreeVec(os_out);
    if (out_buf) FreeVec(out_buf);
    return rc;
}
