/* GFOpenDatatypes: an open datatypes.library for AmigaOS 3.2, API and ABI
 * compatible.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * Clean-room: written from datatypes.library's autodocs, the NDK 3.2
 * headers' published layouts and OS 3.2's answers measured by
 * tests/DatatypesABITest, never from Kickstart, Hyperion or AROS code or
 * bytes. The work is done in dt_core.c, which is plain C and is tested
 * under qemu-m68k (tests/m68k/test_opendatatypes.sh); this file is the
 * library around it: the RomTag, the vector table in datatypes.library's
 * LVO order (datatypes.lvo, which build-amiga.sh checks against the NDK's
 * datatypes_lib.fd), each call taking its arguments in the documented
 * registers, the descriptor files in DEVS:DataTypes and their recognition
 * code.
 *
 * Step D1: the descriptor database, ObtainDataTypeA and ReleaseDataType,
 * the V45 method tables and strings. Step D2: datatypesclass (its work in
 * dtclass_core.c, tested under qemu-m68k by
 * tests/m68k/test_opendatatypes_class.sh; its env here) and the object
 * calls, NewDTObjectA to StartDragSelect, with DTST_CLIPBOARD. Step D3:
 * LockDataType, FindToolNodeA and LaunchToolA, sys/datatypes.catalog, and
 * the private call our AddDataTypes command makes.
 *
 * Built twice: datatypes.library, the replacement, makes the public
 * "datatypesclass"; gfopendatatypes.library, the side-by-side twin
 * tests/DatatypesABITest opens next to OS 3.2's own, makes
 * "opendatatypesclass" so it doesn't take OS 3.2's name. Class libraries
 * opened by the twin subclass OS 3.2's datatypesclass, so for their
 * objects DoAsyncLayout lays out on the caller's task and PrintDTObjectA
 * and StartDragSelect answer 0.
 */
#include <stddef.h>
#include <exec/types.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <exec/interrupts.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/doshunks.h>
#include <dos/dostags.h>
#include <libraries/iffparse.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <utility/tagitem.h>
#include <utility/hooks.h>
#include <intuition/intuition.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <intuition/gadgetclass.h>
#include <intuition/cghooks.h>
#include <graphics/rastport.h>
#include <libraries/locale.h>
#include <devices/clipboard.h>
#include <workbench/workbench.h>
#include <workbench/icon.h>
#include "../../common/include/oa_protos_begin.h"   /* the protos' unused varargs wrappers dropped */
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/locale.h>
#include <proto/icon.h>
#include <proto/wb.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>
#include <proto/iffparse.h>
#include "../../common/include/oa_protos_end.h"

#include "dt_core.h"
#include "dtclass_env.h"

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
#ifndef WBOPENA_ArgLock
#define WBOPENA_ArgLock (TAG_USER + 0xA000 + 1)
#define WBOPENA_ArgName (TAG_USER + 0xA000 + 2)
#endif
#ifndef DTCLASSNAME
#define DTCLASSNAME "datatypesclass"
#endif

#define REG(r, decl) register decl __asm(#r)

#ifndef LIBNAME
#define LIBNAME "gfopendatatypes.library"
#endif
#define LIB_VERSION  47
#define LIB_REVISION 100                /* 47.100 and up: GFOpenDatatypes */

/* How much of the data recognition looks at (measured on OS 3.2: the hook's
 * BufferLength is the size up to 1084, from a file or from memory) */
#define PEEK 1084
/* The biggest descriptor file read */
#define MAX_DESCRIPTOR 65536

struct ODBase {
    struct Library lib;
    BPTR seglist;
    struct SignalSemaphore lock;        /* the database and every entry's uses */
    struct odt_db db;
    UBYTE loaded;                       /* DEVS:DataTypes read */
    UBYTE catalog_tried;                /* sys/datatypes.catalog asked for */
    struct Catalog *catalog;
    struct IClass *dtclass;             /* datatypesclass */
    struct MinList objects;             /* struct objrec: NewDTObjectA's */
};

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
struct Library *UtilityBase;         /* as NDK 3.2's proto/utility.h has it */
struct Library *IFFParseBase;
struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct LocaleBase *LocaleBase;

/* Run as a program, the library does nothing. This must stay the first code
 * in the file (-fno-toplevel-reorder). */
int start(void) { return -1; }

static const char lib_name[] = LIBNAME;
static const char lib_id[] =
    "datatypes.library 47.100 (13.10.2026) GFOpenDatatypes, AmiCradle Team\r\n";
static const char lib_ver[] __attribute__((used)) =
    "\0$VER: datatypes.library 47.100 (13.10.2026) GFOpenDatatypes, AmiCradle Team";
static const char dos_name[] = "dos.library";
static const char utility_name[] = "utility.library";
static const char iffparse_name[] = "iffparse.library";
static const char devs_datatypes[] = "DEVS:DataTypes";
static const char intuition_name[] = "intuition.library";
static const char graphics_name[] = "graphics.library";
static const char dtclass_name[] = DTCLASSNAME;
static const char gadgetclass_name[] = "gadgetclass";

/* ---- what the core asks of the system --------------------------------------- */

static ULONG strlen_(const char *s)
{
    ULONG n = 0;
    while (s[n]) n++;
    return n;
}

static void memset_(void *p, ULONG n)
{
    UBYTE *b = p;
    while (n--) *b++ = 0;
}

static void *env_alloc(void *ctx, odt_u32 n) { return AllocVec(n, MEMF_ANY | MEMF_CLEAR); }
static void env_free(void *ctx, void *p) { FreeVec(p); }

static int env_match(void *ctx, const char *pattern, const char *name)
{
    ULONG n = 2 * strlen_(pattern) + 2;
    STRPTR tok;
    LONG wild;
    int r;
    if (!(tok = AllocVec(n, MEMF_ANY))) return 0;
    wild = ParsePatternNoCase((CONST_STRPTR)pattern, tok, n);
    if (wild < 0) r = 0;
    else if (wild == 0) r = -1;             /* no wildcards: the core compares */
    else r = MatchPatternNoCase(tok, (STRPTR)name) != 0;
    FreeVec(tok);
    return r;
}

/* Recognition code: a DTCD chunk is a load file whose first code is
 * BOOL hook(struct DTHookContext *) taking the context in A0 (and, for
 * code written with stack arguments, on the stack as well). It is loaded
 * from memory with InternalLoadSeg the first time it is needed. */
struct memfile { const UBYTE *at; ULONG left; };

static LONG ls_read(REG(d1, struct memfile *f), REG(d2, UBYTE *buf), REG(d3, LONG len))
{
    LONG n = len < 0 ? 0 : ((ULONG)len > f->left ? (LONG)f->left : len);
    CopyMem((APTR)f->at, buf, n);
    f->at += n;
    f->left -= n;
    return n;
}

static APTR ls_alloc(REG(d0, ULONG size), REG(d1, ULONG flags), REG(a6, struct ExecBase *sys))
{
    return AllocMem(size, flags);
}

static void ls_free(REG(a1, APTR mem), REG(d0, ULONG size), REG(a6, struct ExecBase *sys))
{
    FreeMem(mem, size);
}

static const APTR ls_funcs[] = { (APTR)ls_read, (APTR)ls_alloc, (APTR)ls_free };

static ULONG call_hook(APTR fn, struct DTHookContext *dthc)
{
    register ULONG r __asm("d0");
    register APTR a0 __asm("a0") = dthc;
    register APTR a1 __asm("a1") = fn;
    __asm volatile ("move.l %1,-(%%sp)\n\tjsr (%2)\n\taddq.l #4,%%sp"
                    : "=d"(r), "+a"(a0), "+a"(a1) : : "d1", "cc", "memory");
    return r;
}

static int env_hook(void *ctx, struct odt_entry *e, struct odt_DTHookContext *dthc)
{
    if (!e->seg) {
        struct memfile f;
        LONG stack = 0;
        f.at = e->code;
        f.left = e->code_len;
        e->seg = (void *)InternalLoadSeg((BPTR)&f, 0, (LONG *)ls_funcs, &stack);
        /* measured: code that isn't a load file is no code; the mask decides */
        if (!e->seg) { e->code = NULL; return 1; }
    }
    if (dthc->dthc_FileHandle) Seek((BPTR)dthc->dthc_FileHandle, 0, OFFSET_BEGINNING);
    /* BOOL: the low word */
    return (call_hook(BADDR((BPTR)e->seg) + 4, (struct DTHookContext *)dthc) & 0xffff) != 0;
}

static const struct odt_env env = { NULL, env_alloc, env_free, env_match, env_hook };

static void free_entry(struct odt_entry *e)
{
    if (e->seg) InternalUnLoadSeg((BPTR)e->seg, (VOID (*)())ls_free);
    odt_free_entry(&env, e);
}

/* ---- data in memory as a file ------------------------------------------------- */

/* Recognition code for data in memory gets a lock, a FIB and a file handle
 * all the same, as OS 3.2's does (measured: their port's SigTask is an
 * object named "datatypes.library", the FIB is empty). Ours is a
 * PA_SOFTINT port: each packet DOS sends it is answered in the software
 * interrupt, before the sender's WaitPort returns. It reads, seeks and
 * examines the memory; anything else fails with ERROR_ACTION_NOT_KNOWN. */
struct memstream { const UBYTE *at; LONG size, pos; BPTR lock; };

static struct MsgPort mem_port;
static struct Interrupt mem_int;
static const char mem_name[] = "datatypes.library";

static struct memstream *stream_of_lock(LONG lock)
{
    return (struct memstream *)((struct FileLock *)BADDR((BPTR)lock))->fl_Key;
}

static void mem_examine(struct memstream *ms, struct FileInfoBlock *fib)
{
    memset_(fib, sizeof *fib);
    fib->fib_DirEntryType = ST_FILE;
    fib->fib_EntryType = ST_FILE;
    fib->fib_Size = ms->size;
    fib->fib_NumBlocks = (ms->size + 511) / 512;
    /* the names are BSTRs here: empty */
}

static void mem_packet(struct DosPacket *dp)
{
    struct memstream *ms;
    LONG r1 = DOSFALSE, r2 = 0, n;
    switch (dp->dp_Type) {
    case ACTION_READ:
        ms = (struct memstream *)dp->dp_Arg1;
        n = dp->dp_Arg3;
        if (n < 0) n = 0;
        if (n > ms->size - ms->pos) n = ms->size - ms->pos;
        CopyMem((APTR)(ms->at + ms->pos), (APTR)dp->dp_Arg2, n);
        ms->pos += n;
        r1 = n;
        break;
    case ACTION_SEEK: {
        LONG to = dp->dp_Arg2;
        ms = (struct memstream *)dp->dp_Arg1;
        if (dp->dp_Arg3 == OFFSET_CURRENT) to += ms->pos;
        else if (dp->dp_Arg3 == OFFSET_END) to += ms->size;
        if (to < 0 || to > ms->size) { r1 = -1; r2 = ERROR_SEEK_ERROR; break; }
        r1 = ms->pos;
        ms->pos = to;
        break;
    }
    case ACTION_EXAMINE_OBJECT:
        mem_examine(stream_of_lock(dp->dp_Arg1), (struct FileInfoBlock *)BADDR((BPTR)dp->dp_Arg2));
        r1 = DOSTRUE;
        break;
    case ACTION_EXAMINE_FH:
        mem_examine((struct memstream *)dp->dp_Arg1, (struct FileInfoBlock *)BADDR((BPTR)dp->dp_Arg2));
        r1 = DOSTRUE;
        break;
    case ACTION_COPY_DIR:                   /* DupLock: the one lock serves */
        r1 = dp->dp_Arg1;
        break;
    case ACTION_COPY_DIR_FH:
        r1 = (LONG)((struct memstream *)dp->dp_Arg1)->lock;
        break;
    case ACTION_FREE_LOCK:
    case ACTION_END:                        /* the library frees them */
        r1 = DOSTRUE;
        break;
    case ACTION_PARENT:
    case ACTION_PARENT_FH:
    case ACTION_IS_FILESYSTEM:
        break;
    default:
        r2 = ERROR_ACTION_NOT_KNOWN;
        break;
    }
    dp->dp_Res1 = r1;
    dp->dp_Res2 = r2;
}

static void mem_softint(REG(a1, struct MsgPort *port))
{
    struct Message *m;
    while ((m = GetMsg(port))) {
        struct DosPacket *dp = (struct DosPacket *)m->mn_Node.ln_Name;
        struct MsgPort *reply = dp->dp_Port;
        mem_packet(dp);
        dp->dp_Port = port;
        PutMsg(reply, dp->dp_Link);
    }
}

static void mem_port_init(void)
{
    if (mem_int.is_Code) return;
    mem_int.is_Node.ln_Type = NT_INTERRUPT;
    mem_int.is_Node.ln_Name = (char *)mem_name;
    mem_int.is_Data = &mem_port;
    mem_int.is_Code = (VOID (*)())(void (*)(void))mem_softint;
    mem_port.mp_Node.ln_Type = NT_MSGPORT;
    mem_port.mp_Node.ln_Name = (char *)mem_name;
    mem_port.mp_Flags = PA_SOFTINT;
    mem_port.mp_SigTask = &mem_int;
    mem_port.mp_MsgList.lh_Head = (struct Node *)&mem_port.mp_MsgList.lh_Tail;
    mem_port.mp_MsgList.lh_Tail = NULL;
    mem_port.mp_MsgList.lh_TailPred = (struct Node *)&mem_port.mp_MsgList.lh_Head;
    mem_port.mp_MsgList.lh_Type = NT_MESSAGE;
}

/* ---- the database --------------------------------------------------------------- */

static void add_entry(struct ODBase *base, struct odt_entry *e)
{
    struct odt_entry *old;
    if (!e) return;
    old = odt_db_add(&base->db, e);
    /* an old one still in use stays allocated (it is out of the lists) */
    if (old && !old->uses) free_entry(old);
}

/* Each file in DEVS:DataTypes, in the directory's order, as AddDataTypes
 * reads them (guess: until the probe measures OS 3.2's order). */
static void load_dir(struct ODBase *base)
{
    struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
    BPTR dir = Lock((CONST_STRPTR)devs_datatypes, ACCESS_READ), old;
    UBYTE *buf = AllocVec(MAX_DESCRIPTOR, MEMF_ANY);

    if (fib && dir && buf && Examine(dir, fib) && fib->fib_DirEntryType > 0) {
        old = CurrentDir(dir);
        while (ExNext(dir, fib)) {
            BPTR fh;
            LONG n;
            ULONG l = strlen_((char *)fib->fib_FileName);
            struct odt_entry *e;
            if (fib->fib_DirEntryType > 0) continue;
            if (l > 5 && !Stricmp((CONST_STRPTR)fib->fib_FileName + l - 5, (CONST_STRPTR)".info"))
                continue;
            if (!(fh = Open((CONST_STRPTR)fib->fib_FileName, MODE_OLDFILE))) continue;
            n = Read(fh, buf, MAX_DESCRIPTOR);
            Close(fh);
            if (n > 0 && odt_parse(&env, buf, (ULONG)n, &e) == ODT_OK) add_entry(base, e);
        }
        CurrentDir(old);
    }
    FreeVec(buf);
    if (dir) UnLock(dir);
    if (fib) FreeDosObject(DOS_FIB, fib);
}

static void load(struct ODBase *base)
{
    if (base->loaded) return;
    base->loaded = 1;
    if (!IFFParseBase) IFFParseBase = OpenLibrary((CONST_STRPTR)iffparse_name, 39);
    mem_port_init();
    add_entry(base, odt_builtin(&env, "binary", "binary", GID_SYSTEM, MAKE_ID('b','i','n','a'), DTF_BINARY, 0));
    add_entry(base, odt_builtin(&env, "ascii", "ascii", GID_TEXT, MAKE_ID('a','s','c','i'), DTF_ASCII, 0));
    add_entry(base, odt_builtin(&env, "iff", "iff", GID_SYSTEM, MAKE_ID('i','f','f',0), DTF_IFF, 0));
    add_entry(base, odt_builtin(&env, "directory", "directory", GID_SYSTEM, MAKE_ID('d','i','r','e'), DTF_MISC, 0));
    load_dir(base);
}

static void new_list(struct MinList *l)
{
    l->mlh_Head = (struct MinNode *)&l->mlh_Tail;
    l->mlh_Tail = NULL;
    l->mlh_TailPred = (struct MinNode *)&l->mlh_Head;
}

/* datatypesclass and the object calls (further down) */
static struct MinList iff_recs;
static struct SignalSemaphore iff_lock;
static struct odt_cenv cenv;
static struct ODBase *the_base;
static ULONG dt_dispatch(REG(a0, struct IClass *cl), REG(a2, Object *o), REG(a1, Msg msg));

/* ---- Library housekeeping -------------------------------------------- */

static struct Library *lib_init(REG(d0, struct ODBase *base), REG(a0, BPTR seglist),
                                REG(a6, struct ExecBase *sys))
{
    SysBase = sys;
    base->seglist = seglist;
    base->lib.lib_Node.ln_Type = NT_LIBRARY;
    base->lib.lib_Node.ln_Name = (char *)lib_name;
    base->lib.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    base->lib.lib_Version = LIB_VERSION;
    base->lib.lib_Revision = LIB_REVISION;
    base->lib.lib_IdString = (APTR)lib_id;
    DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)dos_name, 39);
    UtilityBase = OpenLibrary((CONST_STRPTR)utility_name, 39);
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)intuition_name, 39);
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)graphics_name, 39);
    if (DOSBase && UtilityBase && IntuitionBase && GfxBase)
        base->dtclass = MakeClass((ClassID)dtclass_name, (ClassID)gadgetclass_name, NULL, odt_dtclass_inst_size(), 0);
    if (!base->dtclass) {
        if (GfxBase) CloseLibrary((struct Library *)GfxBase);
        if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
        if (DOSBase) CloseLibrary((struct Library *)DOSBase);
        if (UtilityBase) CloseLibrary(UtilityBase);
        FreeMem((UBYTE *)base - base->lib.lib_NegSize, base->lib.lib_NegSize + base->lib.lib_PosSize);
        return NULL;
    }
    InitSemaphore(&base->lock);
    InitSemaphore(&iff_lock);
    new_list(&iff_recs);
    new_list(&base->objects);
    odt_db_init(&base->db);
    the_base = base;
    cenv.ctx = base;
    cenv.dtclass = (struct odt_IClass *)base->dtclass;
    base->dtclass->cl_Dispatcher.h_Entry = (void *)dt_dispatch;
    base->dtclass->cl_UserData = (ULONG)&cenv;
    AddClass(base->dtclass);
    return &base->lib;
}

static struct Library *lib_open(REG(a6, struct ODBase *base))
{
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    return &base->lib;
}

static BPTR lib_expunge(REG(a6, struct ODBase *base));

static BPTR lib_close(REG(a6, struct ODBase *base))
{
    if (base->lib.lib_OpenCnt)
        base->lib.lib_OpenCnt--;
    if (base->lib.lib_OpenCnt == 0 && (base->lib.lib_Flags & LIBF_DELEXP))
        return lib_expunge(base);
    return 0;
}

static BPTR lib_expunge(REG(a6, struct ODBase *base))
{
    BPTR seglist;
    struct odt_Node *n;

    if (base->lib.lib_OpenCnt || !base->seglist || base->objects.mlh_Head->mln_Succ ||
        base->dtclass->cl_ObjectCount || base->dtclass->cl_SubclassCount) {
        base->lib.lib_Flags |= LIBF_DELEXP;
        return 0;
    }
    /* a DataType somebody still holds keeps the library */
    for (n = base->db.all.lh_Head; n->ln_Succ; n = n->ln_Succ) {
        struct odt_entry *e = (struct odt_entry *)((UBYTE *)n - offsetof(struct odt_DataType, dtn_Node2));
        if (e->uses) {
            base->lib.lib_Flags |= LIBF_DELEXP;
            return 0;
        }
    }
    while ((n = base->db.all.lh_Head)->ln_Succ) {
        struct odt_entry *e = (struct odt_entry *)((UBYTE *)n - offsetof(struct odt_DataType, dtn_Node2));
        odt_db_remove(e);
        free_entry(e);
    }
    seglist = base->seglist;
    Remove(&base->lib.lib_Node);
    RemoveClass(base->dtclass);
    FreeClass(base->dtclass);
    if (base->catalog) CloseCatalog(base->catalog);
    if (LocaleBase) CloseLibrary((struct Library *)LocaleBase);
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);
    if (IFFParseBase) CloseLibrary(IFFParseBase);
    CloseLibrary(UtilityBase);
    CloseLibrary((struct Library *)DOSBase);
    FreeMem((UBYTE *)base - base->lib.lib_NegSize,
            base->lib.lib_NegSize + base->lib.lib_PosSize);
    return seglist;
}

static ULONG lib_null(void)
{
    return 0;
}

/* ---- ObtainDataTypeA and friends ------------------------------------------------ */

/* Recognition with the context recognition code gets (parsed as IFF when
 * the data is IFF) */
static struct odt_entry *recognize(struct ODBase *base, const UBYTE *buf, LONG n, const char *name,
                                   BPTR lock, struct FileInfoBlock *fib, BPTR fh)
{
    struct DTHookContext dthc;
    struct IFFHandle *iff = NULL;
    struct odt_entry *e;
    odt_u32 form;

    memset_(&dthc, sizeof dthc);
    dthc.dthc_SysBase = (struct Library *)SysBase;
    dthc.dthc_DOSBase = (struct Library *)DOSBase;
    dthc.dthc_UtilityBase = UtilityBase;
    dthc.dthc_IFFParseBase = IFFParseBase;  /* measured: always there */
    dthc.dthc_Lock = lock;
    dthc.dthc_FIB = fib;
    dthc.dthc_FileHandle = fh;
    dthc.dthc_Buffer = (STRPTR)buf;
    dthc.dthc_BufferLength = n;
    if (n && fh && odt_classify(buf, n, &form) == DTF_IFF && IFFParseBase && (iff = AllocIFF())) {
        Seek(fh, 0, OFFSET_BEGINNING);
        iff->iff_Stream = fh;
        InitIFFasDOS(iff);
        if (OpenIFF(iff, IFFF_READ)) { FreeIFF(iff); iff = NULL; }
        dthc.dthc_IFF = iff;
    }
    e = odt_recognize(&env, &base->db, buf, n, name, (struct odt_DTHookContext *)&dthc);
    if (iff) { CloseIFF(iff); FreeIFF(iff); }
    return e;
}

/* Recognition for memory: the bytes, and a lock, empty FIB and file
 * handle on them through mem_port */
static struct odt_entry *recognize_memory(struct ODBase *base, const UBYTE *at, ULONG size)
{
    struct memstream ms;
    struct FileLock *fl = AllocVec(sizeof *fl, MEMF_PUBLIC | MEMF_CLEAR);
    struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
    struct FileHandle *fh = AllocDosObject(DOS_FILEHANDLE, NULL);
    struct odt_entry *e = NULL;

    if (fl && fib && fh) {
        ms.at = at;
        ms.size = (LONG)size;
        ms.pos = 0;
        ms.lock = MKBADDR(fl);
        fl->fl_Key = (LONG)&ms;
        fl->fl_Access = SHARED_LOCK;
        fl->fl_Task = &mem_port;
        /* fh_Type and fh_Arg1, by their place in the 68k layout (the
         * NDKs name the second one differently) */
        ((LONG *)fh)[2] = (LONG)&mem_port;
        ((LONG *)fh)[9] = (LONG)&ms;
        e = recognize(base, at, size < PEEK ? size : PEEK, NULL, ms.lock, fib, MKBADDR(fh));
    }
    if (fh) FreeDosObject(DOS_FILEHANDLE, fh);
    if (fib) FreeDosObject(DOS_FIB, fib);
    FreeVec(fl);
    return e;
}

/* Recognition for a file: its first bytes, its name, and the context its
 * recognition code gets (the file open, and parsed as IFF when it is). */
static struct odt_entry *recognize_file(struct ODBase *base, BPTR lock)
{
    struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
    struct odt_entry *e = NULL;
    UBYTE *buf = AllocVec(PEEK, MEMF_ANY);
    BPTR dup, fh = 0;
    LONG n;

    if (!fib || !buf) goto out;
    if (!Examine(lock, fib)) goto out;
    if (fib->fib_DirEntryType > 0) {
        e = odt_db_find_name(&base->db, "directory");
        goto out;
    }
    if (!(dup = DupLock(lock))) goto out;
    if (!(fh = OpenFromLock(dup))) { UnLock(dup); goto out; }
    n = Read(fh, buf, PEEK);
    if (n < 0) goto out;
    e = recognize(base, buf, n, (const char *)fib->fib_FileName, lock, fib, fh);
out:
    if (fh) Close(fh);
    if (fib) FreeDosObject(DOS_FIB, fib);
    FreeVec(buf);
    return e;
}

static struct odt_entry *recognize_iff(struct ODBase *base, struct IFFHandle *iff);
static const char *dt_string(struct ODBase *base, ULONG id);

static struct DataType *L_ObtainDataTypeA(REG(d0, ULONG type), REG(a0, APTR handle),
                                          REG(a1, struct TagItem *attrs),
                                          REG(a6, struct ODBase *base))
{
    struct odt_entry *e = NULL;

    ObtainSemaphore(&base->lock);
    load(base);
    switch (type) {
    case DTST_RAM:
        /* OS 3.2: not done, IoErr ERROR_NOT_IMPLEMENTED */
        ReleaseSemaphore(&base->lock);
        SetIoErr(ERROR_NOT_IMPLEMENTED);
        return NULL;
    case DTST_FILE:
        if (handle) e = recognize_file(base, (BPTR)handle);
        break;
    case DTST_MEMORY: {
        UBYTE *at = (UBYTE *)GetTagData(DTA_SourceAddress, 0, attrs);
        ULONG size = GetTagData(DTA_SourceSize, 0, attrs);
        if (at && size) e = recognize_memory(base, at, size);
        break;
    }
    case DTST_CLIPBOARD:
        /* the handle is an IFFHandle open on the clipboard */
        if (handle) e = recognize_iff(base, (struct IFFHandle *)handle);
        break;
    default:
        /* measured: OS 3.2 answers types 0 and 99 so */
        SetIoErr(ERROR_NOT_IMPLEMENTED);
        break;
    }
    if (e) e->uses++;
    ReleaseSemaphore(&base->lock);
    /* measured: no data (or no address or size) leaves IoErr as it was */
    return e ? (struct DataType *)&e->dt : NULL;
}

static void L_ReleaseDataType(REG(a0, struct DataType *dt), REG(a6, struct ODBase *base))
{
    struct odt_entry *e = (struct odt_entry *)dt;
    if (!e) return;
    ObtainSemaphore(&base->lock);
    if (e->uses && !--e->uses && !e->dt.dtn_Node2.ln_Succ) free_entry(e);   /* replaced meanwhile */
    ReleaseSemaphore(&base->lock);
}


/* ---- tools, method tables, strings ------------------------------------------------ */


static ULONG *L_FindMethod(REG(a0, ULONG *methods), REG(a1, ULONG id))
{
    return (ULONG *)odt_FindMethod((odt_u32 *)methods, id);
}

static struct DTMethod *L_FindTriggerMethod(REG(a0, struct DTMethod *methods),
                                            REG(a1, STRPTR command), REG(d0, ULONG method))
{
    return (struct DTMethod *)odt_FindTriggerMethod((struct odt_DTMethod *)methods,
                                                    (const char *)command, method);
}

static ULONG *L_CopyDTMethods(REG(a0, ULONG *methods), REG(a1, ULONG *include),
                              REG(a2, ULONG *exclude))
{
    return (ULONG *)odt_CopyDTMethods(&env, (const odt_u32 *)methods, (const odt_u32 *)include,
                                      (const odt_u32 *)exclude);
}

static struct DTMethod *L_CopyDTTriggerMethods(REG(a0, struct DTMethod *methods),
                                               REG(a1, struct DTMethod *include),
                                               REG(a2, struct DTMethod *exclude))
{
    return (struct DTMethod *)odt_CopyDTTriggerMethods(&env, (const struct odt_DTMethod *)methods,
                                                       (const struct odt_DTMethod *)include,
                                                       (const struct odt_DTMethod *)exclude);
}

static void L_FreeDTMethods(REG(a0, APTR methods))
{
    FreeVec(methods);
}

static ULONG L_GetDTTriggerMethodDataFlags(REG(d0, ULONG method))
{
    return odt_GetDTTriggerMethodDataFlags(method);
}

static CONST_STRPTR L_GetDTString(REG(d0, ULONG id), REG(a6, struct ODBase *base))
{
    return (CONST_STRPTR)dt_string(base, id);
}

/* ---- datatypesclass's env: the system under dtclass_core.c --------------------------- */

/* amiga.lib's DoMethodA, DoSuperMethodA and CoerceMethodA, through
 * utility.library's CallHookPkt (the library links no amiga.lib) */
static ULONG do_method(Object *o, APTR msg)
{
    return o ? CallHookPkt(&OCLASS(o)->cl_Dispatcher, o, msg) : 0;
}

static odt_u32 ce_do_method(const struct odt_cenv *e, void *obj, void *msg) { return do_method(obj, msg); }
static odt_u32 ce_do_super(const struct odt_cenv *e, struct odt_IClass *cl, void *obj, void *msg)
{
    return CallHookPkt(&((struct IClass *)cl)->cl_Super->cl_Dispatcher, obj, msg);
}
static odt_u32 ce_coerce(const struct odt_cenv *e, struct odt_IClass *cl, void *obj, void *msg)
{
    return CallHookPkt(&((struct IClass *)cl)->cl_Dispatcher, obj, msg);
}
static void *ce_new_object(const struct odt_cenv *e, struct odt_IClass *cl, const char *id, struct odt_TagItem *tags)
{
    return NewObjectA((struct IClass *)cl, (ClassID)id, (struct TagItem *)tags);
}
static void ce_dispose(const struct odt_cenv *e, void *obj) { DisposeObject(obj); }
static void *ce_alloc(const struct odt_cenv *e, odt_u32 n) { return AllocVec(n, MEMF_ANY | MEMF_CLEAR); }
static void ce_free(const struct odt_cenv *e, void *p) { FreeVec(p); }
static void ce_sem_init(const struct odt_cenv *e, struct odt_SignalSemaphore *s) { InitSemaphore((struct SignalSemaphore *)s); }
static void ce_sem_obtain(const struct odt_cenv *e, struct odt_SignalSemaphore *s) { ObtainSemaphore((struct SignalSemaphore *)s); }
static void ce_sem_shared(const struct odt_cenv *e, struct odt_SignalSemaphore *s) { ObtainSemaphoreShared((struct SignalSemaphore *)s); }
static int ce_sem_attempt(const struct odt_cenv *e, struct odt_SignalSemaphore *s) { return AttemptSemaphore((struct SignalSemaphore *)s) != 0; }
static void ce_sem_release(const struct odt_cenv *e, struct odt_SignalSemaphore *s) { ReleaseSemaphore((struct SignalSemaphore *)s); }
static odt_s32 ce_io_err(const struct odt_cenv *e) { return IoErr(); }
static void ce_set_io_err(const struct odt_cenv *e, odt_s32 c) { SetIoErr(c); }
static odt_u32 ce_open(const struct odt_cenv *e, const char *name, odt_s32 mode) { return (odt_u32)Open((CONST_STRPTR)name, mode); }
static void ce_close(const struct odt_cenv *e, odt_u32 fh) { if (fh) Close((BPTR)fh); }
static odt_s32 ce_read(const struct odt_cenv *e, odt_u32 fh, void *buf, odt_s32 n) { return Read((BPTR)fh, buf, n); }
static odt_s32 ce_write(const struct odt_cenv *e, odt_u32 fh, const void *buf, odt_s32 n) { return Write((BPTR)fh, (APTR)buf, n); }
static odt_s32 ce_seek(const struct odt_cenv *e, odt_u32 fh, odt_s32 off, odt_s32 mode) { return Seek((BPTR)fh, off, mode); }

/* IFF handles the env made, so iff_close knows what is under each: a DOS
 * file (the caller's to close), the clipboard, or memory through a
 * stream hook */
#define IFFK_DOS  0
#define IFFK_CLIP 1
#define IFFK_MEM  2
struct iffrec {
    struct MinNode node;
    struct IFFHandle *iff;
    UBYTE kind;
    struct ClipboardHandle *clip;
    struct Hook hook;
    const UBYTE *at;
    LONG size, pos;
};

static LONG mem_stream(REG(a0, struct Hook *hook), REG(a2, struct IFFHandle *iff), REG(a1, struct IFFStreamCmd *sc))
{
    struct iffrec *r = hook->h_Data;
    LONG to;
    switch (sc->sc_Command) {
    case IFFCMD_INIT:
    case IFFCMD_CLEANUP:
        return 0;
    case IFFCMD_READ:
        if (sc->sc_NBytes < 0 || sc->sc_NBytes > r->size - r->pos) return IFFERR_READ;
        CopyMem((APTR)(r->at + r->pos), sc->sc_Buf, sc->sc_NBytes);
        r->pos += sc->sc_NBytes;
        return 0;
    case IFFCMD_SEEK:
        to = r->pos + sc->sc_NBytes;
        if (to < 0 || to > r->size) return IFFERR_SEEK;
        r->pos = to;
        return 0;
    default:
        return IFFERR_WRITE;            /* memory is read only */
    }
}

static void *iff_make(UBYTE kind, ULONG stream, const void *at, ULONG size, int write)
{
    struct iffrec *r = AllocVec(sizeof *r, MEMF_ANY | MEMF_CLEAR);
    struct IFFHandle *iff = IFFParseBase ? AllocIFF() : NULL;
    if (!r || !iff) goto fail;
    r->iff = iff;
    r->kind = kind;
    switch (kind) {
    case IFFK_DOS:
        iff->iff_Stream = stream;
        InitIFFasDOS(iff);
        break;
    case IFFK_CLIP:
        if (!(r->clip = OpenClipboard(stream))) goto fail;
        iff->iff_Stream = (ULONG)r->clip;
        InitIFFasClip(iff);
        break;
    default:
        r->at = at;
        r->size = (LONG)size;
        r->hook.h_Entry = (void *)mem_stream;
        r->hook.h_Data = r;
        iff->iff_Stream = (ULONG)r;
        InitIFF(iff, IFFF_RSEEK | IFFF_FSEEK, &r->hook);
        break;
    }
    if (OpenIFF(iff, write ? IFFF_WRITE : IFFF_READ)) goto fail;
    ObtainSemaphore(&iff_lock);
    AddTail((struct List *)&iff_recs, (struct Node *)&r->node);
    ReleaseSemaphore(&iff_lock);
    return iff;
fail:
    if (r && r->clip) CloseClipboard(r->clip);
    if (iff) FreeIFF(iff);
    FreeVec(r);
    return NULL;
}

static void *ce_iff_dos(const struct odt_cenv *e, odt_u32 fh, int write) { return iff_make(IFFK_DOS, fh, NULL, 0, write); }
static void *ce_iff_clip(const struct odt_cenv *e, odt_u32 unit, int write) { return iff_make(IFFK_CLIP, unit, NULL, 0, write); }
static void *ce_iff_mem(const struct odt_cenv *e, const void *at, odt_u32 size) { return iff_make(IFFK_MEM, 0, at, size, 0); }

static void ce_iff_close(const struct odt_cenv *e, void *iff)
{
    struct MinNode *n;
    struct iffrec *r = NULL;
    if (!iff) return;
    ObtainSemaphore(&iff_lock);
    for (n = iff_recs.mlh_Head; n->mln_Succ; n = n->mln_Succ)
        if (((struct iffrec *)n)->iff == iff) { r = (struct iffrec *)n; break; }
    if (r) Remove((struct Node *)&r->node);
    ReleaseSemaphore(&iff_lock);
    CloseIFF(iff);
    if (r && r->clip) CloseClipboard(r->clip);
    FreeIFF(iff);
    FreeVec(r);
}

static odt_s32 ce_iff_parse(const struct odt_cenv *e, void *iff, odt_s32 mode) { return ParseIFF(iff, mode); }
static odt_s32 ce_iff_prop(const struct odt_cenv *e, void *iff, odt_u32 t, odt_u32 id) { return PropChunk(iff, t, id); }
static odt_s32 ce_iff_stop(const struct odt_cenv *e, void *iff, odt_u32 t, odt_u32 id) { return StopChunk(iff, t, id); }
static odt_s32 ce_iff_stop_exit(const struct odt_cenv *e, void *iff, odt_u32 t, odt_u32 id) { return StopOnExit(iff, t, id); }
static odt_s32 ce_iff_coll(const struct odt_cenv *e, void *iff, odt_u32 t, odt_u32 id) { return CollectionChunk(iff, t, id); }
static const void *ce_iff_find_prop(const struct odt_cenv *e, void *iff, odt_u32 t, odt_u32 id, odt_s32 *size)
{
    struct StoredProperty *sp = FindProp(iff, t, id);
    if (!sp) return NULL;
    if (size) *size = sp->sp_Size;
    return sp->sp_Data;
}
static odt_u32 ce_iff_current(const struct odt_cenv *e, void *iff, odt_u32 *type, odt_s32 *size)
{
    struct ContextNode *cn = CurrentChunk(iff);
    if (!cn) return 0;
    if (type) *type = cn->cn_Type;
    if (size) *size = cn->cn_Size;
    return cn->cn_ID;
}
static odt_s32 ce_iff_read(const struct odt_cenv *e, void *iff, void *buf, odt_s32 n) { return ReadChunkBytes(iff, buf, n); }
static odt_s32 ce_iff_push(const struct odt_cenv *e, void *iff, odt_u32 t, odt_u32 id, odt_s32 n) { return PushChunk(iff, t, id, n); }
static odt_s32 ce_iff_write(const struct odt_cenv *e, void *iff, const void *buf, odt_s32 n) { return WriteChunkBytes(iff, (APTR)buf, n); }
static odt_s32 ce_iff_pop(const struct odt_cenv *e, void *iff) { return PopChunk(iff); }

static void *ce_obtain_rp(const struct odt_cenv *e, struct odt_GadgetInfo *gi)
{
    return gi ? ObtainGIRPort((struct GadgetInfo *)gi) : NULL;
}
static void ce_release_rp(const struct odt_cenv *e, void *rp) { if (rp) ReleaseGIRPort(rp); }

/* A complemented outline: four lines that never cross a pixel twice, so
 * drawing it again takes it away */
static void line(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1)
{
    Move(rp, x0, y0);
    Draw(rp, x1, y1);
}
static void ce_complement_box(const struct odt_cenv *e, void *rpv, odt_s32 x0, odt_s32 y0, odt_s32 x1, odt_s32 y1)
{
    struct RastPort *rp = rpv;
    ULONG mode = GetDrMd(rp);
    SetDrMd(rp, COMPLEMENT);
    line(rp, x0, y0, x1, y0);
    if (y1 != y0) line(rp, x0, y1, x1, y1);
    if (y1 - y0 > 1) {
        line(rp, x0, y0 + 1, x0, y1 - 1);
        if (x1 != x0) line(rp, x1, y0 + 1, x1, y1 - 1);
    }
    SetDrMd(rp, mode);
}

/* The layout and printing processes: each gets its work as a message on
 * its pr_MsgPort, runs it and ends */
struct startup {
    struct Message msg;
    const struct odt_cenv *env;
    APTR obj;
    struct Task *parent;                /* signalled once the process holds proc_lock */
    UBYTE printing;
    struct gpLayout gpl;
    struct GadgetInfo gi;
    struct dtPrint pr;
};

static void dispose_dt(struct ODBase *base, Object *o, int object);

static void proc_entry(void)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    struct startup *s;
    int dispose = 0;
    WaitPort(&me->pr_MsgPort);
    s = (struct startup *)GetMsg(&me->pr_MsgPort);
    odt_proc_hold(s->env, s->obj);
    Signal(s->parent, SIGF_SINGLE);
    if (s->printing) odt_print_run(s->env, s->obj, (struct odt_dtPrint *)&s->pr, &dispose);
    else odt_layout_run(s->env, s->obj, (struct odt_gpLayout *)&s->gpl, &dispose);
    /* DisposeDTObject from inside a completion notification: done now
     * the object's last process has unwound (odt_dispose_later); after an
     * OM_DISPOSE as well, only the record is left */
    if (dispose) dispose_dt(the_base, s->obj, dispose == ODT_DISPOSE_ALL);
    FreeVec(s);
}

static const char layout_name[] = "datatypes.library layout";
static const char print_name[] = "datatypes.library print";

static void *ce_start_process(const struct odt_cenv *e, int printing, void *obj, void *win, void *req, void *msg)
{
    struct startup *s = AllocVec(sizeof *s, MEMF_PUBLIC | MEMF_CLEAR);
    struct Process *p;
    struct TagItem t[5];
    if (!s) return NULL;
    s->msg.mn_Node.ln_Type = NT_MESSAGE;
    s->msg.mn_Length = sizeof *s;
    s->env = e;
    s->obj = obj;
    s->printing = printing;
    if (printing) {
        /* the process has no GadgetInfo of its own (ABI test: OS 3.2's
         * DTM_PRINT on its print process) */
        CopyMem(msg, &s->pr, sizeof s->pr);
        s->pr.dtp_GInfo = NULL;
    } else {
        CopyMem(msg, &s->gpl, sizeof s->gpl);
        if (s->gpl.gpl_GInfo) {
            CopyMem(s->gpl.gpl_GInfo, &s->gi, sizeof s->gi);   /* the caller's lasts only for its call */
            s->gpl.gpl_GInfo = &s->gi;
        }
    }
    t[0].ti_Tag = NP_Entry;     t[0].ti_Data = (ULONG)proc_entry;
    t[1].ti_Tag = NP_Name;      t[1].ti_Data = (ULONG)(printing ? print_name : layout_name);
    t[2].ti_Tag = NP_StackSize; t[2].ti_Data = 16384;
    t[3].ti_Tag = NP_Priority;  t[3].ti_Data = 0;
    t[4].ti_Tag = TAG_DONE;     t[4].ti_Data = 0;
    /* the process holds proc_lock before we answer (OM_DISPOSE waits on
     * it): it says so with SIGF_SINGLE, which needs no allocation and
     * which no semaphore of ours is waiting on now */
    s->parent = FindTask(NULL);
    SetSignal(0, SIGF_SINGLE);
    p = CreateNewProc(t);
    if (!p) { FreeVec(s); return NULL; }
    PutMsg(&p->pr_MsgPort, &s->msg);
    Wait(SIGF_SINGLE);
    return p;
}

static void ce_signal_stop(const struct odt_cenv *e, void *proc) { Signal(proc, SIGBREAKF_CTRL_C); }
static int ce_check_stop(const struct odt_cenv *e) { return (SetSignal(0, 0) & SIGBREAKF_CTRL_C) != 0; }

static const char *ce_dt_string(const struct odt_cenv *e, odt_u32 id);
static void ce_unlock(const struct odt_cenv *e, odt_u32 lock) { (void)e; UnLock((BPTR)lock); }
static void *ce_this_task(const struct odt_cenv *e) { (void)e; return FindTask(NULL); }

static struct odt_cenv cenv = {
    NULL, NULL,
    ce_do_method, ce_do_super, ce_coerce, ce_new_object, ce_dispose,
    ce_alloc, ce_free,
    ce_sem_init, ce_sem_obtain, ce_sem_shared, ce_sem_attempt, ce_sem_release,
    ce_io_err, ce_set_io_err, ce_open, ce_close, ce_read, ce_write, ce_seek,
    ce_iff_dos, ce_iff_clip, ce_iff_mem, ce_iff_close, ce_iff_parse, ce_iff_prop, ce_iff_stop,
    ce_iff_stop_exit, ce_iff_coll, ce_iff_find_prop, ce_iff_current, ce_iff_read, ce_iff_push,
    ce_iff_write, ce_iff_pop,
    ce_obtain_rp, ce_release_rp, ce_complement_box,
    ce_start_process, ce_signal_stop, ce_check_stop,
    ce_dt_string,
    NULL,
    ce_unlock,
    ce_this_task
};

/* datatypesclass's dispatcher, as Intuition calls a class's hook */
static ULONG dt_dispatch(REG(a0, struct IClass *cl), REG(a2, Object *o), REG(a1, Msg msg))
{
    return odt_dtclass_dispatch((const struct odt_cenv *)cl->cl_UserData, (struct odt_IClass *)cl, o, msg);
}

/* Is o one of datatypesclass's (ours, not OS 3.2's when this is the
 * side-by-side twin)? */
static int ours(struct ODBase *base, Object *o)
{
    struct IClass *cl;
    if (!o || !base->dtclass) return 0;
    for (cl = OCLASS(o); cl; cl = cl->cl_Super)
        if (cl == base->dtclass) return 1;
    return 0;
}

/* ---- strings: sys/datatypes.catalog ------------------------------------------------- */

static const char catalog_name[] = "sys/datatypes.catalog";
static const char builtin_language[] = "english";

static const char *dt_string(struct ODBase *base, ULONG id)
{
    const char *s = odt_GetDTString(id);
    if (!s) return NULL;
    /* the catalog, the first time a string is wanted (and after a locale
     * change, when the program reopens us: ABI test) */
    if (!base->catalog_tried) {
        ObtainSemaphore(&base->lock);
        if (!base->catalog_tried) {
            if (!LocaleBase) LocaleBase = (struct LocaleBase *)OpenLibrary((CONST_STRPTR)"locale.library", 38);
            if (LocaleBase) {
                struct TagItem t[2];
                t[0].ti_Tag = OC_BuiltInLanguage; t[0].ti_Data = (ULONG)builtin_language;
                t[1].ti_Tag = TAG_DONE; t[1].ti_Data = 0;
                base->catalog = OpenCatalogA(NULL, (CONST_STRPTR)catalog_name, t);
            }
            base->catalog_tried = 1;
        }
        ReleaseSemaphore(&base->lock);
    }
    if (base->catalog && *s) s = (const char *)GetCatalogStr(base->catalog, id, (CONST_STRPTR)s);
    return s;
}

static const char *ce_dt_string(const struct odt_cenv *e, odt_u32 id) { return dt_string(the_base, id); }

/* ---- the object calls ----------------------------------------------------------------- */

/* What NewDTObjectA keeps with each object it made: the class library and
 * the DataType, both let go by DisposeDTObject */
struct objrec {
    struct MinNode node;
    Object *obj;
    struct Library *classlib;
    struct DataType *dt;
};

static struct objrec *find_obj(struct ODBase *base, Object *o, int unlink)
{
    struct MinNode *n;
    struct objrec *r = NULL;
    ObtainSemaphore(&base->lock);
    for (n = base->objects.mlh_Head; n->mln_Succ; n = n->mln_Succ)
        if (((struct objrec *)n)->obj == o) { r = (struct objrec *)n; break; }
    if (r && unlink) Remove((struct Node *)&r->node);
    ReleaseSemaphore(&base->lock);
    return r;
}

/* A class library's one call, -30 ObtainEngine: its Class */
static struct IClass *obtain_engine(struct Library *lib)
{
    register struct IClass *r __asm("d0");
    register struct Library *a0 __asm("a0") = lib;
    /* A6 saved by hand: it may be the frame pointer */
    __asm volatile ("move.l %%a6,-(%%sp)\n\tmove.l %1,%%a6\n\tjsr -30(%%a6)\n\tmove.l (%%sp)+,%%a6"
                    : "=r"(r), "+r"(a0) : : "d1", "a1", "cc", "memory");
    return r;
}

/* The type of what is on the clipboard: the IFF FORM's type, as a FORM
 * header recognition can look at (with the IFFHandle for its code) */
static struct odt_entry *recognize_iff(struct ODBase *base, struct IFFHandle *iff)
{
    struct DTHookContext dthc;
    struct ContextNode *cn;
    UBYTE buf[12];
    ULONG type;

    if (!(cn = CurrentChunk(iff))) {
        if (ParseIFF(iff, IFFPARSE_STEP)) return NULL;
        if (!(cn = CurrentChunk(iff))) return NULL;
    }
    type = cn->cn_Type;
    buf[0] = 'F'; buf[1] = 'O'; buf[2] = 'R'; buf[3] = 'M';
    buf[4] = cn->cn_Size >> 24; buf[5] = cn->cn_Size >> 16; buf[6] = cn->cn_Size >> 8; buf[7] = cn->cn_Size;
    buf[8] = type >> 24; buf[9] = type >> 16; buf[10] = type >> 8; buf[11] = type;
    memset_(&dthc, sizeof dthc);
    dthc.dthc_SysBase = (struct Library *)SysBase;
    dthc.dthc_DOSBase = (struct Library *)DOSBase;
    dthc.dthc_UtilityBase = UtilityBase;
    dthc.dthc_IFFParseBase = IFFParseBase;
    dthc.dthc_IFF = iff;
    dthc.dthc_Buffer = (STRPTR)buf;
    dthc.dthc_BufferLength = sizeof buf;
    return odt_recognize(&env, &base->db, buf, sizeof buf, NULL, (struct odt_DTHookContext *)&dthc);
}

/* ObtainDataTypeA's DTST_CLIPBOARD: handle is an IFFHandle open on the
 * clipboard (ABI test: how far OS 3.2 parses it) */
static struct odt_entry *recognize_clip(struct ODBase *base, ULONG unit)
{
    struct odt_entry *e = NULL;
    struct IFFHandle *iff = ce_iff_clip(&cenv, unit, 0);
    if (!iff) { SetIoErr(DTERROR_COULDNT_OPEN_CLIPBOARD); return NULL; }
    e = recognize_iff(base, iff);
    ce_iff_close(&cenv, iff);
    return e;
}

static struct DataType *lock_type(struct ODBase *base, struct DataType *dt)
{
    ObtainSemaphore(&base->lock);
    ((struct odt_entry *)dt)->uses++;
    ReleaseSemaphore(&base->lock);
    return dt;
}

static void L_ReleaseDataType(REG(a0, struct DataType *dt), REG(a6, struct ODBase *base));
static struct DataType *L_ObtainDataTypeA(REG(d0, ULONG type), REG(a0, APTR handle),
                                          REG(a1, struct TagItem *attrs), REG(a6, struct ODBase *base));

static const char class_dir[] = "datatypes/";
static const char class_ext[] = ".datatype";

static Object *L_NewDTObjectA(REG(d0, APTR name), REG(a0, struct TagItem *attrs), REG(a6, struct ODBase *base))
{
    ULONG st = GetTagData(DTA_SourceType, DTST_FILE, attrs);
    ULONG gid = GetTagData(DTA_GroupID, 0, attrs);
    struct DataType *dt = (struct DataType *)GetTagData(DTA_DataType, 0, attrs);
    const char *basename = NULL;
    struct objrec *rec;
    struct Library *lib;
    struct IClass *cl;
    Object *o;
    char path[64];
    ULONG n, i;
    struct TagItem t[6];

    if (dt) {
        lock_type(base, dt);
    } else {
        switch (st) {
        case DTST_FILE: {
            BPTR lock;
            if (!name) { SetIoErr(ERROR_REQUIRED_ARG_MISSING); return NULL; }
            if (!(lock = Lock((CONST_STRPTR)name, SHARED_LOCK))) return NULL;
            dt = L_ObtainDataTypeA(DTST_FILE, (APTR)lock, NULL, base);
            UnLock(lock);
            break;
        }
        case DTST_CLIPBOARD: {
            /* DTA_Handle: the program's IFFHandle on the clipboard
             * instead of the unit in name (the autodoc) */
            struct IFFHandle *h = (struct IFFHandle *)GetTagData(DTA_Handle, 0, attrs);
            struct odt_entry *e;
            ObtainSemaphore(&base->lock);
            load(base);
            if ((e = h ? recognize_iff(base, h) : recognize_clip(base, (ULONG)name)) != NULL) e->uses++;
            ReleaseSemaphore(&base->lock);
            dt = e ? (struct DataType *)&e->dt : NULL;
            break;
        }
        case DTST_MEMORY:
            dt = L_ObtainDataTypeA(DTST_MEMORY, NULL, attrs, base);
            break;
        case DTST_RAM:
            /* a new, empty object of the group's class (ABI test: the
             * class OS 3.2 picks for each group, and DTA_BaseName) */
            basename = (const char *)GetTagData(DTA_BaseName, 0, attrs);
            if (!basename) basename = odt_group_base(gid);
            if (!basename) { SetIoErr(ERROR_REQUIRED_ARG_MISSING); return NULL; }
            break;
        default:
            SetIoErr(ERROR_NOT_IMPLEMENTED);
            return NULL;
        }
        if (!dt && !basename) {
            if (!IoErr()) SetIoErr(DTERROR_UNKNOWN_DATATYPE);
            return NULL;
        }
    }
    if (dt) {
        /* DTA_GroupID: only an object of that group (documented) */
        if (gid && dt->dtn_Header->dth_GroupID != gid) {
            L_ReleaseDataType(dt, base);
            SetIoErr(ERROR_OBJECT_WRONG_TYPE);
            return NULL;
        }
        basename = (const char *)dt->dtn_Header->dth_BaseName;
    }

    /* the class: LIBS:datatypes/<base name>.datatype */
    for (n = 0, i = 0; class_dir[i]; i++) path[n++] = class_dir[i];
    for (i = 0; basename[i] && n < sizeof path - sizeof class_ext; i++) path[n++] = basename[i];
    for (i = 0; class_ext[i]; i++) path[n++] = class_ext[i];
    path[n] = 0;
    if (!(lib = OpenLibrary((CONST_STRPTR)path, 0))) {
        if (dt) L_ReleaseDataType(dt, base);
        SetIoErr(DTERROR_COULDNT_OPEN);     /* ABI test: OS 3.2's IoErr here */
        return NULL;
    }
    if (!(rec = AllocVec(sizeof *rec, MEMF_ANY | MEMF_CLEAR)) || !(cl = obtain_engine(lib))) {
        FreeVec(rec);
        CloseLibrary(lib);
        if (dt) L_ReleaseDataType(dt, base);
        SetIoErr(rec ? DTERROR_COULDNT_OPEN : ERROR_NO_FREE_STORE);
        return NULL;
    }
    t[0].ti_Tag = DTA_Name;       t[0].ti_Data = (ULONG)name;
    t[1].ti_Tag = DTA_SourceType; t[1].ti_Data = st;
    t[2].ti_Tag = DTA_DataType;   t[2].ti_Data = (ULONG)dt;
    t[3].ti_Tag = TAG_MORE;       t[3].ti_Data = (ULONG)attrs;
    SetIoErr(0);
    if (!(o = NewObjectA(cl, NULL, t))) {
        LONG err = IoErr();
        FreeVec(rec);
        CloseLibrary(lib);
        if (dt) L_ReleaseDataType(dt, base);
        SetIoErr(err ? err : ERROR_NO_FREE_STORE);
        return NULL;
    }
    rec->obj = o;
    rec->classlib = lib;
    rec->dt = dt;
    ObtainSemaphore(&base->lock);
    AddTail((struct List *)&base->objects, (struct Node *)&rec->node);
    ReleaseSemaphore(&base->lock);
    return o;
}

/* The object (when object) and the library's record of it */
static void dispose_dt(struct ODBase *base, Object *o, int object)
{
    struct objrec *r = find_obj(base, o, 1);
    if (object) DisposeObject(o);
    if (r) {
        CloseLibrary(r->classlib);
        if (r->dt) L_ReleaseDataType(r->dt, base);
        FreeVec(r);
    }
}

/* From a layout or print process inside its completion notification, the
 * object and its class library stay until the notification has unwound:
 * the process disposes it then (proc_entry). The object has one disposer:
 * a second DisposeDTObject joins the first (odt_dispose_later). */
static void L_DisposeDTObject(REG(a0, Object *o), REG(a6, struct ODBase *base))
{
    int later;
    if (!o) return;
    later = ours(base, o) ? odt_dispose_later(&cenv, o) : 0;
    if (!later) dispose_dt(base, o, 1);
    else if (later == ODT_DISPOSE_RECORD) dispose_dt(base, o, 0);
}

static ULONG L_SetDTAttrsA(REG(a0, Object *o), REG(a1, struct Window *win), REG(a2, struct Requester *req),
                           REG(a3, struct TagItem *attrs))
{
    struct opSet m;
    ULONG r;
    if (!o) return 0;
    if (win) {
        /* attached: what changed is drawn (the class says so with a
         * nonzero answer) */
        r = SetGadgetAttrsA((struct Gadget *)o, win, req, attrs);
        if (r) RefreshGList((struct Gadget *)o, win, req, 1);
        return r;
    }
    m.MethodID = OM_SET;
    m.ops_AttrList = attrs;
    m.ops_GInfo = NULL;
    return do_method(o, &m);
}

static ULONG L_GetDTAttrsA(REG(a0, Object *o), REG(a2, struct TagItem *attrs))
{
    return odt_get_dt_attrs(&cenv, o, (struct odt_TagItem *)attrs);
}

static LONG L_AddDTObject(REG(a0, struct Window *win), REG(a1, struct Requester *req), REG(a2, Object *o),
                          REG(d0, LONG pos), REG(a6, struct ODBase *base))
{
    LONG r;
    struct gpLayout gpl;
    if (!win || !o) return -1;
    if (ours(base, o)) ODT_DTINST(&cenv, o)->flags |= ODT_DTF_ADDED;
    r = (LONG)AddGList(win, (struct Gadget *)o, pos, 1, req);
    /* the autodoc: the object gets GM_LAYOUT with gpl_Initial 1. Intuition
     * sends that itself to a gadget with relative size or GFLG_RELSPECIAL;
     * an absolute one gets it here. */
    if (!(((struct Gadget *)o)->Flags & (GFLG_RELBOTTOM | GFLG_RELRIGHT | GFLG_RELWIDTH | GFLG_RELHEIGHT |
                                          GFLG_RELSPECIAL))) {
        gpl.MethodID = GM_LAYOUT;
        gpl.gpl_GInfo = NULL;
        gpl.gpl_Initial = 1;
        DoGadgetMethodA((struct Gadget *)o, win, req, (Msg)&gpl);
    }
    return r;
}

static void L_RefreshDTObjectA(REG(a0, Object *o), REG(a1, struct Window *win), REG(a2, struct Requester *req),
                               REG(a3, struct TagItem *attrs))
{
    if (o && win) RefreshGList((struct Gadget *)o, win, req, 1);
}

static ULONG L_DoAsyncLayout(REG(a0, Object *o), REG(a1, struct gpLayout *gpl), REG(a6, struct ODBase *base))
{
    struct gpLayout m;
    if (!o || !gpl) return 0;
    if (ours(base, o)) return odt_do_async_layout(&cenv, o, (struct odt_gpLayout *)gpl);
    /* OS 3.2's datatypesclass under us (the twin): laid out here */
    CopyMem(gpl, &m, sizeof m);
    m.MethodID = DTM_ASYNCLAYOUT;
    return do_method(o, &m);
}

static ULONG L_DoDTMethodA(REG(a0, Object *o), REG(a1, struct Window *win), REG(a2, struct Requester *req),
                           REG(a3, Msg msg))
{
    if (!o || !msg) return 0;
    if (win) return DoGadgetMethodA((struct Gadget *)o, win, req, msg);
    return do_method(o, msg);
}

static LONG L_RemoveDTObject(REG(a0, struct Window *win), REG(a1, Object *o), REG(a6, struct ODBase *base))
{
    LONG pos;
    if (!o) return -1;
    odt_remove_object(&cenv, o);
    pos = win ? (LONG)RemoveGList(win, (struct Gadget *)o, 1) : -1;
    return pos;
}

static ULONG *L_GetDTMethods(REG(a0, Object *o))
{
    ULONG v = 0;
    if (o) GetAttr(DTA_Methods, o, &v);
    return (ULONG *)v;
}

static struct DTMethod *L_GetDTTriggerMethods(REG(a0, Object *o))
{
    ULONG v = 0;
    if (o) GetAttr(DTA_TriggerMethods, o, &v);
    return (struct DTMethod *)v;
}

static ULONG L_PrintDTObjectA(REG(a0, Object *o), REG(a1, struct Window *win), REG(a2, struct Requester *req),
                              REG(a3, struct dtPrint *msg), REG(a6, struct ODBase *base))
{
    if (!o || !msg) return 0;
    if (ours(base, o)) return odt_print_object(&cenv, o, win, req, (struct odt_dtPrint *)msg);
    return 0;
}

/* ObtainDTDrawInfoA, DrawDTObjectA and ReleaseDTDrawInfo (V44; private in
 * NDK 3.2's fd, at -120 to -132 as the V44 headers have them: ABI test) */
static APTR L_ObtainDTDrawInfoA(REG(a0, Object *o), REG(a1, struct TagItem *attrs))
{
    struct opSet m;
    if (!o) return NULL;
    m.MethodID = DTM_OBTAINDRAWINFO;
    m.ops_AttrList = attrs;
    m.ops_GInfo = NULL;
    return (APTR)do_method(o, &m);
}

static LONG L_DrawDTObjectA(REG(a0, struct RastPort *rp), REG(a1, Object *o), REG(d0, LONG x), REG(d1, LONG y),
                            REG(d2, LONG w), REG(d3, LONG h), REG(d4, LONG th), REG(d5, LONG tv),
                            REG(a2, struct TagItem *attrs))
{
    struct dtDraw m;
    if (!rp || !o) return 0;
    m.MethodID = DTM_DRAW;
    m.dtd_RPort = rp;
    m.dtd_Left = x;
    m.dtd_Top = y;
    m.dtd_Width = w;
    m.dtd_Height = h;
    m.dtd_TopHoriz = th;
    m.dtd_TopVert = tv;
    m.dtd_AttrList = attrs;
    return (LONG)do_method(o, &m);
}

static void L_ReleaseDTDrawInfo(REG(a0, Object *o), REG(a1, APTR handle))
{
    struct odt_dtReleaseDrawInfo m;     /* NDK 3.2 has no struct dtReleaseDrawInfo */
    if (!o) return;
    m.MethodID = DTM_RELEASEDRAWINFO;
    m.dtr_Handle = handle;
    do_method(o, &m);
}

static ULONG L_SaveDTObjectA(REG(a0, Object *o), REG(a1, struct Window *win), REG(a2, struct Requester *req),
                             REG(a3, STRPTR file), REG(d0, ULONG mode), REG(d1, LONG saveicon),
                             REG(a4, struct TagItem *attrs))
{
    struct dtWrite m;
    BPTR fh;
    ULONG r;
    LONG err;
    if (!o || !file) return 0;
    if (!(fh = Open(file, MODE_NEWFILE))) return 0;
    m.MethodID = DTM_WRITE;
    m.dtw_GInfo = NULL;
    m.dtw_FileHandle = fh;
    m.dtw_Mode = mode;
    m.dtw_AttrList = attrs;
    r = win ? DoGadgetMethodA((struct Gadget *)o, win, req, (Msg)&m) : do_method(o, &m);
    err = IoErr();
    Close(fh);
    if (!r) {
        /* nothing half written stays behind (ABI test: OS 3.2 deletes it) */
        DeleteFile(file);
        SetIoErr(err ? err : DTERROR_COULDNT_SAVE);
        return 0;
    }
    if (saveicon) {
        struct Library *IconBase = OpenLibrary((CONST_STRPTR)"icon.library", 37);
        if (IconBase) {
            /* the default project icon (ABI test: OS 3.2 asks icon.library
             * for the type's own, ENV:Sys/def_<type>) */
            struct DiskObject *dob = GetDiskObject((CONST_STRPTR)"ENV:Sys/def_project");
            if (!dob) dob = GetDefDiskObject(WBPROJECT);
            if (dob) {
                PutDiskObject(file, dob);
                FreeDiskObject(dob);
            }
            CloseLibrary(IconBase);
        }
    }
    return r;
}

static ULONG L_StartDragSelect(REG(a0, Object *o), REG(a6, struct ODBase *base))
{
    if (!ours(base, o)) return 0;
    return odt_start_drag_select(&cenv, o);
}

/* ---- D3: the V45 tool calls ---------------------------------------------------------- */

static ULONG L_LockDataType(REG(a0, struct DataType *dt), REG(a6, struct ODBase *base))
{
    if (!dt) return 0;
    lock_type(base, dt);
    return 1;
}

static struct ToolNode *L_FindToolNodeA(REG(a0, struct List *tools), REG(a1, struct TagItem *attrs))
{
    return (struct ToolNode *)odt_FindToolNodeA((struct odt_List *)tools, (const struct odt_TagItem *)attrs);
}

static const char nil_name[] = "NIL:";

/* A command, asynchronously, with NIL: for its input and output */
static LONG run_async(const char *cmd)
{
    BPTR in = Open((CONST_STRPTR)nil_name, MODE_OLDFILE), out = Open((CONST_STRPTR)nil_name, MODE_NEWFILE);
    LONG r;
    struct TagItem t[5];
    if (!in || !out) {
        if (in) Close(in);
        if (out) Close(out);
        return -1;
    }
    t[0].ti_Tag = SYS_Input;    t[0].ti_Data = (ULONG)in;
    t[1].ti_Tag = SYS_Output;   t[1].ti_Data = (ULONG)out;
    t[2].ti_Tag = SYS_Asynch;   t[2].ti_Data = TRUE;
    t[3].ti_Tag = NP_StackSize; t[3].ti_Data = 16384;
    t[4].ti_Tag = TAG_DONE;     t[4].ti_Data = 0;
    r = SystemTagList((CONST_STRPTR)cmd, t);
    if (r == -1) { Close(in); Close(out); }     /* SYS_Asynch closes them only when it ran */
    return r;
}

/* LaunchToolA: the tool on the project, as its TF_ says: TF_SHELL as a
 * Shell command, TF_WORKBENCH through workbench.library, TF_RX as an
 * ARexx program with RX. Nonzero when it was started (ABI test: what
 * OS 3.2 does with attrs, and its answer). */
static ULONG L_LaunchToolA(REG(a0, struct Tool *tool), REG(a1, STRPTR project), REG(a2, struct TagItem *attrs))
{
    char *cmd;
    ULONG n, r = 0;
    if (!tool || !tool->tn_Program) return 0;
    n = strlen_((const char *)tool->tn_Program) + (project ? 2 * strlen_((const char *)project) : 0) + 16;
    if (!(cmd = AllocVec(n, MEMF_ANY))) return 0;
    switch (tool->tn_Flags & TF_LAUNCH_MASK) {
    case TF_SHELL:
        if (odt_tool_command(cmd, n, (const char *)tool->tn_Program, (const char *)project))
            r = run_async(cmd) != -1;
        break;
    case TF_RX:
        cmd[0] = 'R'; cmd[1] = 'X'; cmd[2] = ' ';
        if (odt_tool_command(cmd + 3, n - 3, (const char *)tool->tn_Program, (const char *)project))
            r = run_async(cmd) != -1;
        break;
    case TF_WORKBENCH: {
        struct Library *WorkbenchBase = OpenLibrary((CONST_STRPTR)"workbench.library", 44);
        if (WorkbenchBase) {
            struct TagItem t[3];
            BPTR dir = 0, lock;
            t[0].ti_Tag = TAG_IGNORE;
            t[1].ti_Tag = TAG_IGNORE;
            t[2].ti_Tag = TAG_DONE;
            if (project && (lock = Lock(project, SHARED_LOCK))) {
                dir = ParentDir(lock);
                UnLock(lock);
                t[0].ti_Tag = WBOPENA_ArgLock; t[0].ti_Data = (ULONG)dir;
                t[1].ti_Tag = WBOPENA_ArgName; t[1].ti_Data = (ULONG)FilePart(project);
            }
            r = OpenWorkbenchObjectA(tool->tn_Program, t) != 0;
            if (dir) UnLock(dir);
            CloseLibrary(WorkbenchBase);
        }
        break;
    }
    }
    FreeVec(cmd);
    return r;
}

/* ---- D3: AddDataTypes ----------------------------------------------------------------- */

/* The private call our AddDataTypes command makes (it checks the
 * library's id string names GFOpenDatatypes first): each descriptor file
 * in files added (a type of the same name replaced), DEVS:DataTypes read
 * again with ODT_ADT_REFRESH, and each type given to the hook with
 * ODT_ADT_LIST (CallHookPkt(hook, DataType, NULL)). It answers how many
 * files it added. */
#define ODT_ADT_REFRESH 1
#define ODT_ADT_LIST    2

static ULONG add_file(struct ODBase *base, CONST_STRPTR name)
{
    UBYTE *buf = AllocVec(MAX_DESCRIPTOR, MEMF_ANY);
    BPTR fh;
    LONG n;
    struct odt_entry *e;
    ULONG r = 0;
    if (!buf) return 0;
    if ((fh = Open(name, MODE_OLDFILE)) != 0) {
        n = Read(fh, buf, MAX_DESCRIPTOR);
        Close(fh);
        if (n > 0 && odt_parse(&env, buf, (ULONG)n, &e) == ODT_OK) {
            add_entry(base, e);
            r = 1;
        }
    }
    FreeVec(buf);
    return r;
}

static ULONG L_AddDataTypesA(REG(a0, STRPTR *files), REG(d0, ULONG flags), REG(a1, struct Hook *hook),
                             REG(a6, struct ODBase *base))
{
    ULONG n = 0;
    ObtainSemaphore(&base->lock);
    load(base);
    if (flags & ODT_ADT_REFRESH) load_dir(base);
    if (files)
        for (; *files; files++) n += add_file(base, *files);
    if ((flags & ODT_ADT_LIST) && hook) {
        struct odt_Node *nd;
        for (nd = base->db.all.lh_Head; nd->ln_Succ; nd = nd->ln_Succ) {
            struct odt_entry *e = (struct odt_entry *)((UBYTE *)nd - offsetof(struct odt_DataType, dtn_Node2));
            CallHookPkt(hook, &e->dt, NULL);
        }
    }
    ReleaseSemaphore(&base->lock);
    return n;
}

/* The private slots nobody fills answer 0 */
static ULONG L_none(void)
{
    return 0;
}

/* datatypes.library's LVO order as NDK 3.2's fd has it; datatypes.lvo says
 * the same, line for line. NDK 3.2 keeps -120 to -132 and -144 to -252
 * private: ours hold the V44 draw calls and the V45 tool calls where the
 * V44/V45 headers had them (ABI test: OS 3.2's own there), and at -252
 * the call our AddDataTypes makes; the rest answer 0. */
static const APTR lib_vectors[] = {
    (APTR)lib_open, (APTR)lib_close, (APTR)lib_expunge, (APTR)lib_null,
    (APTR)lib_null,                      /* -30 private */
    (APTR)L_ObtainDataTypeA,             /* -36 */
    (APTR)L_ReleaseDataType,             /* -42 */
    (APTR)L_NewDTObjectA,                /* -48 NewDTObjectA */
    (APTR)L_DisposeDTObject,             /* -54 DisposeDTObject */
    (APTR)L_SetDTAttrsA,                 /* -60 SetDTAttrsA */
    (APTR)L_GetDTAttrsA,                 /* -66 GetDTAttrsA */
    (APTR)L_AddDTObject,                 /* -72 AddDTObject */
    (APTR)L_RefreshDTObjectA,            /* -78 RefreshDTObjectA */
    (APTR)L_DoAsyncLayout,               /* -84 DoAsyncLayout */
    (APTR)L_DoDTMethodA,                 /* -90 DoDTMethodA */
    (APTR)L_RemoveDTObject,              /* -96 RemoveDTObject */
    (APTR)L_GetDTMethods,                /* -102 GetDTMethods */
    (APTR)L_GetDTTriggerMethods,         /* -108 GetDTTriggerMethods */
    (APTR)L_PrintDTObjectA,              /* -114 PrintDTObjectA */
    (APTR)L_ObtainDTDrawInfoA,           /* -120 private: ObtainDTDrawInfoA (V44) */
    (APTR)L_DrawDTObjectA,               /* -126 private: DrawDTObjectA (V44) */
    (APTR)L_ReleaseDTDrawInfo,           /* -132 private: ReleaseDTDrawInfo (V44) */
    (APTR)L_GetDTString,                 /* -138 */
    (APTR)L_LockDataType,                /* -144 private: LockDataType (V45) */
    (APTR)L_FindToolNodeA,               /* -150 private: FindToolNodeA (V45) */
    (APTR)L_LaunchToolA,                 /* -156 private: LaunchToolA (V45) */
    (APTR)L_none,                        /* -162 private */
    (APTR)L_none,                        /* -168 private */
    (APTR)L_none,                        /* -174 private */
    (APTR)L_none,                        /* -180 private */
    (APTR)L_none,                        /* -186 private */
    (APTR)L_none,                        /* -192 private */
    (APTR)L_none,                        /* -198 private */
    (APTR)L_none,                        /* -204 private */
    (APTR)L_none,                        /* -210 private */
    (APTR)L_none,                        /* -216 private */
    (APTR)L_none,                        /* -222 private */
    (APTR)L_none,                        /* -228 private */
    (APTR)L_none,                        /* -234 private */
    (APTR)L_none,                        /* -240 private */
    (APTR)L_none,                        /* -246 private */
    (APTR)L_AddDataTypesA,               /* -252 private: GFOpenDatatypes: AddDataTypes */
    (APTR)L_FindMethod,                  /* -258 */
    (APTR)L_FindTriggerMethod,           /* -264 */
    (APTR)L_CopyDTMethods,               /* -270 */
    (APTR)L_CopyDTTriggerMethods,        /* -276 */
    (APTR)L_FreeDTMethods,               /* -282 */
    (APTR)L_GetDTTriggerMethodDataFlags, /* -288 */
    (APTR)L_SaveDTObjectA,               /* -294 SaveDTObjectA */
    (APTR)L_StartDragSelect,             /* -300 StartDragSelect */
    (APTR)-1
};

static const struct {
    ULONG size;
    const APTR *vectors;
    APTR data;
    APTR init;
} lib_inittable = {
    sizeof(struct ODBase), lib_vectors, NULL, (APTR)lib_init
};

const struct Resident lib_romtag = {
    RTC_MATCHWORD, (struct Resident *)&lib_romtag, (APTR)(&lib_romtag + 1),
    RTF_AUTOINIT, LIB_VERSION, NT_LIBRARY, 0,
    (char *)lib_name, (char *)lib_id, (APTR)&lib_inittable
};
