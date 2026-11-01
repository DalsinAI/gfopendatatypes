/*
 * openamigaimage datatypes: the shared library glue (see dtlib.h).
 *
 * Each datatype is a plain Amiga library with its own ROMTag: Open, Close,
 * Expunge, the reserved vector and ObtainEngine(), which hands
 * datatypes.library the BOOPSI class. common/dtstart.c is linked first, so
 * the library's first code is a safe return.
 *
 * The C library pieces a decoder needs (malloc and friends) are task-safe
 * here: several programs may decode pictures at the same time.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include <exec/memory.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/datatypes.h>
#include <stdarg.h>
#include <string.h>

#include "dtlib.h"

#ifdef OAI_AC_HELPERS
/* Big copies and clears through AC090's native helpers (build.sh, AC_HELPERS):
 * host code on AmiCradle, 68k code written for the 68020 and 68040
 * elsewhere; under 64 bytes, the C library's as before. */
#include "ac_helpers.h"
#else
#define ac_memcpy_auto memcpy
#endif

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *UtilityBase, *DataTypesBase;

static struct Library *superBase;
static Class *dtClass;
static BPTR segList;

extern const char LibName[];
extern const char LibIdString[];
extern const UWORD LibVersion;
extern const UWORD LibRevision;

static ULONG dispatcher(REG(a0, Class *cl), REG(a2, Object *o), REG(a1, Msg msg))
{
    return dt_dispatch(cl, o, msg);
}

static void closeAll(void)
{
    if (dtClass) {
        RemoveClass(dtClass);
        FreeClass(dtClass);
        dtClass = NULL;
    }
    if (superBase)
        CloseLibrary(superBase);
    if (DataTypesBase)
        CloseLibrary(DataTypesBase);
    if (UtilityBase)
        CloseLibrary(UtilityBase);
    if (GfxBase)
        CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase)
        CloseLibrary((struct Library *)IntuitionBase);
    if (DOSBase)
        CloseLibrary((struct Library *)DOSBase);
    superBase = DataTypesBase = UtilityBase = NULL;
    GfxBase = NULL;
    IntuitionBase = NULL;
    DOSBase = NULL;
}

static BOOL openAll(struct Library *lib)
{
    char superName[64];

    DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 39);
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 39);
    UtilityBase = OpenLibrary((CONST_STRPTR)"utility.library", 39);
    DataTypesBase = OpenLibrary((CONST_STRPTR)"datatypes.library", 39);
    /* datatypesclass, the root (stream.datatype's superclass), lives in
     * datatypes.library itself; every other superclass is a library of its own. */
    if (strcmp(dt_superclass, "datatypesclass") != 0) {
        strcpy(superName, "datatypes/");
        strcat(superName, dt_superclass);
        if (!(superBase = OpenLibrary((CONST_STRPTR)superName, dt_superversion)))
            return FALSE;
    }
    if (!DOSBase || !IntuitionBase || !GfxBase || !UtilityBase || !DataTypesBase)
        return FALSE;
    if (!dt_init())
        return FALSE;
    dtClass = MakeClass((ClassID)LibName, (ClassID)dt_superclass, NULL, dt_instsize, 0);
    if (!dtClass) {
        dt_cleanup();
        return FALSE;
    }
    dtClass->cl_Dispatcher.h_Entry = (typeof(dtClass->cl_Dispatcher.h_Entry))dispatcher;
    dtClass->cl_UserData = (ULONG)lib;
    AddClass(dtClass);
    return TRUE;
}

/* exec calls this once, through MakeLibrary(), when the library loads. */
static struct Library *libInit(REG(d0, struct Library *lib), REG(a0, BPTR seg), REG(a6, struct ExecBase *sysBase))
{
    SysBase = sysBase;
    segList = seg;
    lib->lib_Node.ln_Type = NT_LIBRARY;
    lib->lib_Node.ln_Name = (char *)LibName;
    lib->lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    lib->lib_Version = LibVersion;
    lib->lib_Revision = LibRevision;
    lib->lib_IdString = (APTR)LibIdString;
    if (!openAll(lib)) {
        closeAll();
        FreeMem((UBYTE *)lib - lib->lib_NegSize, lib->lib_NegSize + lib->lib_PosSize);
        return NULL;
    }
    return lib;
}

/* Open, Close and Expunge run under Forbid(). */
static struct Library *libOpen(REG(a6, struct Library *lib))
{
    lib->lib_OpenCnt++;
    lib->lib_Flags &= ~LIBF_DELEXP;
    return lib;
}

static BPTR libExpunge(REG(a6, struct Library *lib))
{
    BPTR seg;
    if (lib->lib_OpenCnt) {
        lib->lib_Flags |= LIBF_DELEXP;
        return 0;
    }
    /* Objects of the class still alive keep it (and so the library). */
    if (dtClass) {
        RemoveClass(dtClass);
        if (!FreeClass(dtClass)) {
            AddClass(dtClass);
            lib->lib_Flags |= LIBF_DELEXP;
            return 0;
        }
        dtClass = NULL;
    }
    Remove(&lib->lib_Node);
    dt_cleanup();
    closeAll();
    seg = segList;
    FreeMem((UBYTE *)lib - lib->lib_NegSize, lib->lib_NegSize + lib->lib_PosSize);
    return seg;
}

static BPTR libClose(REG(a6, struct Library *lib))
{
    if (--lib->lib_OpenCnt == 0 && (lib->lib_Flags & LIBF_DELEXP))
        return libExpunge(lib);
    return 0;
}

static ULONG libNull(void)
{
    return 0;
}

/* LVO -30: the class datatypes.library builds objects from. */
static Class *obtainEngine(REG(a6, struct Library *lib))
{
    (void)lib;
    return dtClass;
}

static const APTR funcTable[] = {
    (APTR)libOpen,
    (APTR)libClose,
    (APTR)libExpunge,
    (APTR)libNull,
    (APTR)obtainEngine,
    (APTR)-1
};

static const ULONG initTable[] = {
    sizeof(struct Library),
    (ULONG)funcTable,
    0,
    (ULONG)libInit
};

/* rt_Version is the class's version: ramlib, dos and the packed ROM's files
 * on demand weigh a copy by it and the revision in its ID string, and the
 * newer one runs (amigachrome-guest's common/include/okvr.h). With 0 every
 * class compared as 0.0, older than any copy on a disk. LibVersion is
 * another file's constant, so the RomTag takes DT_VERSION, every class's
 * version; tests/test-romtag.py checks each class's LibVersion, LibRevision
 * and ID string against it. */
#ifndef DT_VERSION
#define DT_VERSION 47
#endif

const struct Resident dt_romtag __attribute__((used)) = {
    RTC_MATCHWORD,
    (struct Resident *)&dt_romtag,
    (APTR)(&dt_romtag + 1),
    RTF_AUTOINIT,
    DT_VERSION,
    NT_LIBRARY,
    0,
    (char *)LibName,
    (char *)LibIdString,
    (APTR)initTable
};

/* --- task-safe memory for the decoders ------------------------------------ */

/* A datatype with an allocator of its own (gfopenmodule's arenas) builds this
 * file with DT_OWN_MALLOC and supplies malloc, calloc, free and realloc. */
#ifndef DT_OWN_MALLOC

void *malloc(size_t size)
{
    ULONG *block = AllocVec(size + 8, MEMF_ANY);
    if (!block)
        return NULL;
    block[0] = size;
    return block + 2;
}

void *calloc(size_t count, size_t size)
{
    size_t total = count * size;
    ULONG *block;
    if (size && total / size != count)
        return NULL;
#ifdef OAI_AC_HELPERS
    /* cleared here rather than by exec (MEMF_CLEAR), whose loop is 68k code */
    block = AllocVec(total + 8, MEMF_ANY);
    if (!block)
        return NULL;
    ac_memset_auto(block, 0, total + 8);
#else
    block = AllocVec(total + 8, MEMF_ANY | MEMF_CLEAR);
    if (!block)
        return NULL;
#endif
    block[0] = total;
    return block + 2;
}

void free(void *p)
{
    if (p)
        FreeVec((ULONG *)p - 2);
}

void *realloc(void *p, size_t size)
{
    void *n;
    size_t old;
    if (!p)
        return malloc(size);
    if (!size) {
        free(p);
        return NULL;
    }
    old = ((ULONG *)p)[-2];
    if (size <= old)
        return p;
    n = malloc(size);
    if (n) {
        ac_memcpy_auto(n, p, old);
        free(p);
    }
    return n;
}

#endif /* DT_OWN_MALLOC */

/* A small vsnprintf for decoders' error messages (%s %c %d %u %x %%), so
 * libnix's stdio (which needs a program's start-up) stays out. */
int vsnprintf(char *buffer, size_t size, const char *format, va_list ap)
{
    size_t n = 0;
    char digits[12];
#define PUT(ch) do { if (n + 1 < size) buffer[n] = (ch); n++; } while (0)
    for (; *format; format++) {
        const char *text = NULL;
        unsigned long value;
        int base = 10, negative = 0, i, width = 0;
        char pad = ' ';
        if (*format != '%') {
            PUT(*format);
            continue;
        }
        format++;
        /* a width for numbers ("%02x", "%4d"); other flags are skipped */
        if (*format == '0')
            pad = '0';
        while (*format == '-' || (*format >= '0' && *format <= '9')) {
            if (*format >= '0' && *format <= '9')
                width = width * 10 + (*format - '0');
            format++;
        }
        if (*format == '.')                    /* a precision: skipped */
            for (format++; *format >= '0' && *format <= '9'; format++)
                ;
        while (*format == 'l' || *format == 'z')
            format++;
        switch (*format) {
        case 's':
            text = va_arg(ap, const char *);
            for (text = text ? text : "(null)"; *text; text++)
                PUT(*text);
            continue;
        case 'c':
            PUT((char)va_arg(ap, int));
            continue;
        case 'd':
        case 'i': {
            long v = va_arg(ap, long);
            negative = v < 0;
            value = negative ? -(unsigned long)v : (unsigned long)v;
            break;
        }
        case 'x':
        case 'X':
            base = 16;
            value = va_arg(ap, unsigned long);
            break;
        case 'u':
            value = va_arg(ap, unsigned long);
            break;
        case '\0':
            format--;
            continue;
        default:
            PUT(*format);
            continue;
        }
        i = 0;
        do {
            digits[i++] = "0123456789abcdef"[value % base];
            value /= base;
        } while (value && i < (int)sizeof digits);
        if (negative && pad == '0')
            PUT('-');
        for (width -= i + negative; width > 0; width--)
            PUT(pad);
        if (negative && pad != '0')
            PUT('-');
        while (i)
            PUT(digits[--i]);
    }
    if (size)
        buffer[n < size ? n : size - 1] = 0;
#undef PUT
    return (int)n;
}

/* --- reading the source ----------------------------------------------------- */

UBYTE *dt_read_source(Object *o, ULONG *size)
{
    ULONG sourceType = DTST_FILE;
    BPTR fh = 0;
    LONG length;
    UBYTE *data;

    GetDTAttrs(o, DTA_SourceType, (ULONG)&sourceType, DTA_Handle, (ULONG)&fh, TAG_DONE);
    if (sourceType != DTST_FILE || !fh) {
        SetIoErr(ERROR_OBJECT_WRONG_TYPE);
        return NULL;
    }
    if (Seek(fh, 0, OFFSET_END) < 0)
        return NULL;
    length = Seek(fh, 0, OFFSET_BEGINNING);
    if (length <= 0) {
        SetIoErr(ERROR_OBJECT_WRONG_TYPE);
        return NULL;
    }
    data = AllocVec(length, MEMF_ANY);
    if (!data) {
        SetIoErr(ERROR_NO_FREE_STORE);
        return NULL;
    }
    if (Read(fh, data, length) != length) {
        FreeVec(data);
        return NULL;
    }
    *size = length;
    return data;
}
