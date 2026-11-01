/* The picture and sound families' ABI tests: one set of calls run through
 * OS 3.2's class and through ours (the side-by-side open<name>.datatype),
 * each answer a line of text, and the lines compared.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * A test defines its set as  static void run_set(const char *cls)  where
 * cls is the public class name to make objects of ("ilbm.datatype" or
 * "gfopenilbm.datatype"), and DTC_TEST_CLASS (the class's file name), then
 * calls dtc_main(argc, argv). Inside the set: out("fmt", ...) writes a
 * line; probe("fmt", ...) writes one that answers an "ABI test" corner
 * of the autodocs, marked so PROBE's listing is the measured answers.
 *
 *   <Test> [PROBE] [LIB=dir] [DIR=dir]
 * PROBE prints OS 3.2's lines only. LIB=dir: where open<name>.datatype is
 * (default LIBS:datatypes). DIR=dir: where the test writes its files
 * (default T:). Exit 0 when nothing differs, 5 when something does, 20 on
 * an error.
 */
#ifndef DTC_ABITEST_H
#define DTC_ABITEST_H

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/classes.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/datatypes.h>
#include <clib/alib_protos.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

struct Library *DataTypesBase;
struct IntuitionBase *IntuitionBase;

#define OUTSIZE (256UL * 1024UL)
static char *out_buf;
static ULONG out_len;
static const char *dir = "T:";

static void out(const char *fmt, ...)
{
    va_list ap;
    int n;
    if (out_len + 512 >= OUTSIZE) return;
    va_start(ap, fmt);
    n = vsnprintf(out_buf + out_len, 510, fmt, ap);
    va_end(ap);
    if (n > 509) n = 509;
    out_len += n;
    out_buf[out_len++] = '\n';
    out_buf[out_len] = 0;
}
#define probe(...) out("PROBE " __VA_ARGS__)

static void path_of(char *buf, ULONG size, const char *name)
{
    strncpy(buf, dir, size - 1);
    buf[size - 1] = 0;
    AddPart((STRPTR)buf, (CONST_STRPTR)name, size);
}

static __attribute__((unused)) int write_file(const char *name, const UBYTE *data, ULONG len)
{
    char p[256];
    BPTR fh;
    LONG n;
    path_of(p, sizeof p, name);
    if (!(fh = Open((CONST_STRPTR)p, MODE_NEWFILE))) return 0;
    n = Write(fh, (APTR)data, len);
    Close(fh);
    return n == (LONG)len;
}

/* An object of the class under test, of a file the test wrote: the type
 * recognised by OS 3.2's datatypes.library, the class given by name */
static __attribute__((unused)) Object *new_file_obj(const char *cls, const char *name, struct TagItem *more)
{
    char p[256];
    BPTR lock;
    struct DataType *dt = NULL;
    Object *o;
    struct TagItem t[5];
    path_of(p, sizeof p, name);
    if ((lock = Lock((CONST_STRPTR)p, ACCESS_READ)) != 0) {
        dt = ObtainDataTypeA(DTST_FILE, (APTR)lock, NULL);
        UnLock(lock);
    }
    t[0].ti_Tag = DTA_Name; t[0].ti_Data = (ULONG)p;
    t[1].ti_Tag = DTA_SourceType; t[1].ti_Data = DTST_FILE;
    t[2].ti_Tag = DTA_DataType; t[2].ti_Data = (ULONG)dt;
    t[3].ti_Tag = more ? TAG_MORE : TAG_DONE; t[3].ti_Data = (ULONG)more;
    SetIoErr(0);
    o = NewObjectA(NULL, (CONST_STRPTR)cls, t);
    if (dt) ReleaseDataType(dt);
    return o;
}

static __attribute__((unused)) Object *new_ram_obj(const char *cls, struct TagItem *more)
{
    struct TagItem t[2];
    t[0].ti_Tag = DTA_SourceType; t[0].ti_Data = DTST_RAM;
    t[1].ti_Tag = more ? TAG_MORE : TAG_DONE; t[1].ti_Data = (ULONG)more;
    SetIoErr(0);
    return NewObjectA(NULL, (CONST_STRPTR)cls, t);
}

static ULONG get1(Object *o, ULONG attr)
{
    ULONG v = 0xdeadbeefUL;
    if (!GetAttr(attr, o, &v)) return 0xfeedfaceUL;      /* not an attribute it knows */
    return v;
}

static void run_set(const char *cls);

static char *run_one(const char *cls)
{
    char *b = AllocVec(OUTSIZE, MEMF_ANY | MEMF_CLEAR);
    if (!b) return NULL;
    out_buf = b;
    out_len = 0;
    run_set(cls);
    return b;
}

static int dtc_main(int argc, char **argv)
{
    int probe_only = 0, i, diffs = 0;
    const char *libdir = "LIBS:datatypes";
    char libpath[256], ours[64];
    struct Library *oslib, *ourlib = NULL;
    char *a, *b;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "PROBE")) probe_only = 1;
        else if (!strncmp(argv[i], "LIB=", 4)) libdir = argv[i] + 4;
        else if (!strncmp(argv[i], "DIR=", 4)) dir = argv[i] + 4;
    }
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR) "intuition.library", 39);
    DataTypesBase = OpenLibrary((CONST_STRPTR) "datatypes.library", 39);
    if (!IntuitionBase || !DataTypesBase) return 20;
    oslib = OpenLibrary((CONST_STRPTR) "datatypes/" DTC_TEST_CLASS, 0);
    if (!oslib) { printf("no OS 3.2 %s\n", DTC_TEST_CLASS); return 20; }
    if (!probe_only) {
        strncpy(libpath, libdir, sizeof libpath - 1);
        libpath[sizeof libpath - 1] = 0;
        AddPart((STRPTR)libpath, (CONST_STRPTR) "open" DTC_TEST_CLASS, sizeof libpath);
        ourlib = OpenLibrary((CONST_STRPTR)libpath, 0);
        if (!ourlib) probe_only = 1;
    }
    printf("OS 3.2 %s %u.%u\n", DTC_TEST_CLASS, oslib->lib_Version, oslib->lib_Revision);
    a = run_one(DTC_TEST_CLASS);
    if (!a) return 20;
    if (probe_only) {
        fputs(a, stdout);
    } else {
        char *pa, *pb;
        printf("ours %s %u.%u\n", libpath, ourlib->lib_Version, ourlib->lib_Revision);
        strcpy(ours, "open" DTC_TEST_CLASS);
        b = run_one(ours);
        if (!b) return 20;
        pa = a;
        pb = b;
        while (*pa || *pb) {
            char *ea = strchr(pa, '\n'), *eb = strchr(pb, '\n');
            size_t la = ea ? (size_t)(ea - pa) : strlen(pa), lb = eb ? (size_t)(eb - pb) : strlen(pb);
            if (la != lb || memcmp(pa, pb, la)) {
                printf("OS:   %.*s\nours: %.*s\n", (int)la, pa, (int)lb, pb);
                diffs++;
            }
            pa += la + (ea ? 1 : 0);
            pb += lb + (eb ? 1 : 0);
        }
        printf("%d lines differ\n", diffs);
        FreeVec(b);
        CloseLibrary(ourlib);
    }
    FreeVec(a);
    CloseLibrary(oslib);
    CloseLibrary(DataTypesBase);
    CloseLibrary((struct Library *)IntuitionBase);
    return diffs ? 5 : 0;
}

/* ---- IFF, made here -------------------------------------------------------- */

struct iffw { UBYTE buf[16384]; ULONG len, chunk; };

static __attribute__((unused)) void iff_begin(struct iffw *w, const char *type)
{
    w->len = 12;
    memcpy(w->buf, "FORM", 4);
    memcpy(w->buf + 8, type, 4);
}
static void put32(UBYTE *p, ULONG v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static __attribute__((unused)) void iff_chunk(struct iffw *w, const char *id, const void *data, ULONG n)
{
    if (w->len + 8 + n + 1 > sizeof w->buf) return;
    memcpy(w->buf + w->len, id, 4);
    put32(w->buf + w->len + 4, n);
    memcpy(w->buf + w->len + 8, data, n);
    w->len += 8 + n;
    if (n & 1) w->buf[w->len++] = 0;
}
static __attribute__((unused)) void iff_end(struct iffw *w) { put32(w->buf + 4, w->len - 8); }

#endif
