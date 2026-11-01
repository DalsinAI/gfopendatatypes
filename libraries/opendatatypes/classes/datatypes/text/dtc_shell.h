/* GFOpenDatatypes classes: the C around the text, ascii, animation and anim
 * class cores, shared by their shells (open<name>_class.c). Built only on
 * the os32-gcc16 stove, with the NDK 3.2 headers; the cores never see them.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * Clean-room, from the autodocs of exec, dos, intuition, graphics,
 * iffparse and datatypes and the NDK 3.2 headers' published layouts;
 * never from Kickstart's, Hyperion's or AROS's code.
 *
 * A shell defines, then includes this file:
 *   LIBNAME          the library's name, "text.datatype", or for the ABI
 *                    test's side-by-side build "gfopentext.datatype"; the
 *                    public class is made under the same name
 *   DTC_SUPERCLASS   the superclass's public name ("datatypesclass",
 *                    "text.datatype", "animation.datatype")
 *   DTC_SUPERLIB     the library that makes it ("datatypes.library",
 *                    "datatypes/text.datatype"...), kept open
 *   DTC_DISPATCH     the core's dispatcher (odt_dispatch_fn)
 *   DTC_INST_SIZE    the core's instance size call
 *   DTC_IDSTRING     the library's id string
 *   DTC_EXT_INIT     optional: a function (struct DTCBase *) that fills
 *                    base->ext and sets env.class_ext to it
 *   DTC_EXT_FREE     optional: its undoing, at expunge
 * then makes its vector table with DTC_STD_VECTORS and DTC_ROMTAG(vectors).
 *
 * A datatype class library has one call of its own, at -30: it answers
 * the class (ObtainEngine in our .lvo; the NDK has no fd for the class
 * libraries, so build-amiga.sh checks it only when one is there).
 *
 * Multicore (OPEN_LIBRARIES.md section 5): every class call is anchored
 * (it draws and talks to Intuition). The bases below are set once at init
 * and only read after; per-object state is in the object.
 */
#ifndef OPENDATATYPES_DTC_SHELL_H
#define OPENDATATYPES_DTC_SHELL_H

#include <exec/types.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <intuition/gadgetclass.h>
#include <intuition/cghooks.h>
#include <intuition/screens.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <graphics/regions.h>
#include <libraries/iffparse.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <utility/hooks.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/iffparse.h>

#include "dtc_util.h"

#ifndef REG
#define REG(r, decl) register decl __asm(#r)
#endif

/* The cores' copies of the NDK's layouts are the NDK's own */
#define DTC_SAME(a, b) _Static_assert(sizeof(struct a) == sizeof(struct b), #a)
DTC_SAME(IClass, odt_IClass); DTC_SAME(Gadget, odt_Gadget); DTC_SAME(ExtGadget, odt_ExtGadget);
DTC_SAME(GadgetInfo, odt_GadgetInfo); DTC_SAME(InputEvent, odt_InputEvent); DTC_SAME(TagItem, odt_TagItem);
DTC_SAME(DTSpecialInfo, odt_DTSpecialInfo); DTC_SAME(gpInput, odt_gpInput); DTC_SAME(gpLayout, odt_gpLayout);
DTC_SAME(gpRender, odt_gpRender); DTC_SAME(dtSelect, odt_dtSelect); DTC_SAME(dtWrite, odt_dtWrite);
DTC_SAME(dtDraw, odt_dtDraw); DTC_SAME(SignalSemaphore, odt_SignalSemaphore); DTC_SAME(IBox, odt_IBox);
_Static_assert(__builtin_offsetof(struct Gadget, SpecialInfo) == __builtin_offsetof(struct odt_Gadget, SpecialInfo),
               "Gadget.SpecialInfo");
_Static_assert(__builtin_offsetof(struct IFFHandle, iff_Stream) == 0, "IFFHandle.iff_Stream");

#define LIB_VERSION  47
#define LIB_REVISION 100                /* 47.100 and up: GFOpenDatatypes */

struct DTCBase {
    struct Library lib;
    BPTR seglist;
    struct IClass *cl;
    struct Library *super_lib;          /* DTC_SUPERLIB, open while we are */
    struct odt_cenv env;
    void *ext;                          /* the family's class_ext */
};

/* Set once by lib_init, read-only after */
struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *UtilityBase;
struct Library *IFFParseBase;
struct Library *LayersBase;
struct Library *DiskfontBase;

/* Run as a program, the library does nothing. The first code in the file
 * (-fno-toplevel-reorder). */
int start(void) { return -1; }

static const char lib_name[] = LIBNAME;
static const char lib_id[] = DTC_IDSTRING;

/* ---- BOOPSI ------------------------------------------------------------ */

static odt_u32 env_do_method(const struct odt_cenv *e, void *obj, void *msg)
{
    (void)e;
    if (!obj) return 0;
    return CallHookPkt(&OCLASS(obj)->cl_Dispatcher, obj, msg);
}

static odt_u32 env_do_super(const struct odt_cenv *e, struct odt_IClass *cl, void *obj, void *msg)
{
    (void)e;
    if (!cl || !obj) return 0;
    return CallHookPkt(&((struct IClass *)cl)->cl_Super->cl_Dispatcher, obj, msg);
}

static odt_u32 env_coerce(const struct odt_cenv *e, struct odt_IClass *cl, void *obj, void *msg)
{
    (void)e;
    if (!cl || !obj) return 0;
    return CallHookPkt(&((struct IClass *)cl)->cl_Dispatcher, obj, msg);
}

/* ---- exec and dos ------------------------------------------------------ */

static void *env_alloc(const struct odt_cenv *e, odt_u32 n) { (void)e; return AllocVec(n, MEMF_ANY | MEMF_CLEAR); }
static void env_free(const struct odt_cenv *e, void *p) { (void)e; if (p) FreeVec(p); }
static void env_sem_init(const struct odt_cenv *e, struct odt_SignalSemaphore *s)
{ (void)e; InitSemaphore((struct SignalSemaphore *)s); }
static void env_sem_obtain(const struct odt_cenv *e, struct odt_SignalSemaphore *s)
{ (void)e; ObtainSemaphore((struct SignalSemaphore *)s); }
static void env_sem_shared(const struct odt_cenv *e, struct odt_SignalSemaphore *s)
{ (void)e; ObtainSemaphoreShared((struct SignalSemaphore *)s); }
static int env_sem_attempt(const struct odt_cenv *e, struct odt_SignalSemaphore *s)
{ (void)e; return AttemptSemaphore((struct SignalSemaphore *)s) != 0; }
static void env_sem_release(const struct odt_cenv *e, struct odt_SignalSemaphore *s)
{ (void)e; ReleaseSemaphore((struct SignalSemaphore *)s); }

/* dos calls only from a process: the input.device task (GM_GOACTIVE)
 * gets no IoErr */
static int is_process(void) { return FindTask(NULL)->tc_Node.ln_Type == NT_PROCESS; }
static odt_s32 env_io_err(const struct odt_cenv *e) { (void)e; return is_process() ? IoErr() : 0; }
static void env_set_io_err(const struct odt_cenv *e, odt_s32 c) { (void)e; if (is_process()) SetIoErr(c); }
static odt_s32 env_read(const struct odt_cenv *e, odt_u32 fh, void *buf, odt_s32 n)
{ (void)e; return Read((BPTR)fh, buf, n); }
static odt_s32 env_write(const struct odt_cenv *e, odt_u32 fh, const void *buf, odt_s32 n)
{ (void)e; return Write((BPTR)fh, (APTR)buf, n); }
static odt_s32 env_seek(const struct odt_cenv *e, odt_u32 fh, odt_s32 off, odt_s32 mode)
{ (void)e; return Seek((BPTR)fh, off, mode); }

/* ---- iffparse ---------------------------------------------------------- */

static void *env_iff_open_clip(const struct odt_cenv *e, odt_u32 unit, int write)
{
    struct IFFHandle *iff;
    (void)e;
    if (!IFFParseBase || !(iff = AllocIFF())) return NULL;
    if ((iff->iff_Stream = (ULONG)OpenClipboard(unit)) != 0) {
        InitIFFasClip(iff);
        if (OpenIFF(iff, write ? IFFF_WRITE : IFFF_READ) == 0) return iff;
        CloseClipboard((struct ClipboardHandle *)iff->iff_Stream);
    }
    FreeIFF(iff);
    return NULL;
}

static void env_iff_close(const struct odt_cenv *e, void *p)
{
    struct IFFHandle *iff = p;
    (void)e;
    CloseIFF(iff);
    CloseClipboard((struct ClipboardHandle *)iff->iff_Stream);
    FreeIFF(iff);
}

static odt_s32 env_iff_parse(const struct odt_cenv *e, void *iff, odt_s32 mode)
{ (void)e; return ParseIFF((struct IFFHandle *)iff, mode); }
static odt_s32 env_iff_stop(const struct odt_cenv *e, void *iff, odt_u32 type, odt_u32 id)
{ (void)e; return StopChunk((struct IFFHandle *)iff, type, id); }
static odt_u32 env_iff_current(const struct odt_cenv *e, void *iff, odt_u32 *type, odt_s32 *size)
{
    struct ContextNode *cn = CurrentChunk((struct IFFHandle *)iff);
    (void)e;
    if (!cn) { *type = 0; *size = 0; return 0; }
    *type = cn->cn_Type;
    *size = cn->cn_Size;
    return cn->cn_ID;
}
static odt_s32 env_iff_read_chunk(const struct odt_cenv *e, void *iff, void *buf, odt_s32 n)
{ (void)e; return ReadChunkBytes((struct IFFHandle *)iff, buf, n); }
static odt_s32 env_iff_push(const struct odt_cenv *e, void *iff, odt_u32 type, odt_u32 id, odt_s32 size)
{ (void)e; return PushChunk((struct IFFHandle *)iff, type, id, size); }
static odt_s32 env_iff_write_chunk(const struct odt_cenv *e, void *iff, const void *buf, odt_s32 n)
{ (void)e; return WriteChunkBytes((struct IFFHandle *)iff, (APTR)buf, n); }
static odt_s32 env_iff_pop(const struct odt_cenv *e, void *iff) { (void)e; return PopChunk((struct IFFHandle *)iff); }

/* ---- intuition --------------------------------------------------------- */

static void *env_obtain_rp(const struct odt_cenv *e, struct odt_GadgetInfo *gi)
{ (void)e; return ObtainGIRPort((struct GadgetInfo *)gi); }
static void env_release_rp(const struct odt_cenv *e, void *rp) { (void)e; if (rp) ReleaseGIRPort((struct RastPort *)rp); }

/* ---- the class's dispatcher: A0 the class, A2 the object, A1 the message */

static ULONG dispatch_entry(REG(a0, struct IClass *cl), REG(a2, Object *obj), REG(a1, Msg msg))
{
    struct DTCBase *base = (struct DTCBase *)cl->cl_UserData;
    return DTC_DISPATCH(&base->env, (struct odt_IClass *)cl, obj, msg);
}

static void env_init(struct DTCBase *base)
{
    struct odt_cenv *e = &base->env;
    e->ctx = base;
    e->do_method = env_do_method;
    e->do_super = env_do_super;
    e->coerce = env_coerce;
    e->alloc = env_alloc;
    e->free = env_free;
    e->sem_init = env_sem_init;
    e->sem_obtain = env_sem_obtain;
    e->sem_obtain_shared = env_sem_shared;
    e->sem_attempt = env_sem_attempt;
    e->sem_release = env_sem_release;
    e->io_err = env_io_err;
    e->set_io_err = env_set_io_err;
    e->read = env_read;
    e->write = env_write;
    e->seek = env_seek;
    e->iff_open_clip = env_iff_open_clip;
    e->iff_close = env_iff_close;
    e->iff_parse = env_iff_parse;
    e->iff_stop = env_iff_stop;
    e->iff_current = env_iff_current;
    e->iff_read_chunk = env_iff_read_chunk;
    e->iff_push = env_iff_push;
    e->iff_write_chunk = env_iff_write_chunk;
    e->iff_pop = env_iff_pop;
    e->obtain_rp = env_obtain_rp;
    e->release_rp = env_release_rp;
}

/* ---- library housekeeping -------------------------------------------- */

static void shut(struct DTCBase *base)
{
#ifdef DTC_EXT_FREE
    DTC_EXT_FREE(base);
#endif
    if (base->super_lib) CloseLibrary(base->super_lib);
    if (DiskfontBase) CloseLibrary(DiskfontBase);
    if (LayersBase) CloseLibrary(LayersBase);
    if (IFFParseBase) CloseLibrary(IFFParseBase);
    if (UtilityBase) CloseLibrary(UtilityBase);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    if (DOSBase) CloseLibrary((struct Library *)DOSBase);
}

static struct Library *lib_init(REG(d0, struct DTCBase *base), REG(a0, BPTR seglist), REG(a6, struct ExecBase *sys))
{
    SysBase = sys;
    base->seglist = seglist;
    base->lib.lib_Node.ln_Type = NT_LIBRARY;
    base->lib.lib_Node.ln_Name = (char *)lib_name;
    base->lib.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    base->lib.lib_Version = LIB_VERSION;
    base->lib.lib_Revision = LIB_REVISION;
    base->lib.lib_IdString = (APTR)lib_id;
    DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR) "dos.library", 39);
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR) "intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR) "graphics.library", 39);
    UtilityBase = OpenLibrary((CONST_STRPTR) "utility.library", 39);
    IFFParseBase = OpenLibrary((CONST_STRPTR) "iffparse.library", 39);
    LayersBase = OpenLibrary((CONST_STRPTR) "layers.library", 39);
    DiskfontBase = OpenLibrary((CONST_STRPTR) "diskfont.library", 39);    /* not needed: OpenFont then */
    env_init(base);
#ifdef DTC_EXT_INIT
    if (!DTC_EXT_INIT(base)) goto fail;
#endif
    if (DOSBase && IntuitionBase && GfxBase && UtilityBase && IFFParseBase && LayersBase) {
        /* the superclass: datatypes.library makes datatypesclass, a class
         * library its own public class */
        base->super_lib = OpenLibrary((CONST_STRPTR)DTC_SUPERLIB, 0);
        if (base->super_lib) {
            base->cl = MakeClass((CONST_STRPTR)lib_name, (CONST_STRPTR)DTC_SUPERCLASS, NULL, DTC_INST_SIZE(), 0);
            if (base->cl) {
                base->cl->cl_UserData = (ULONG)base;
                base->cl->cl_Dispatcher.h_Entry = (HOOKFUNC)(void (*)(void))dispatch_entry;
                base->cl->cl_Dispatcher.h_SubEntry = NULL;
                base->cl->cl_Dispatcher.h_Data = base;
                AddClass(base->cl);
                return &base->lib;
            }
        }
    }
#ifdef DTC_EXT_INIT
fail:
#endif
    shut(base);
    FreeMem((UBYTE *)base - base->lib.lib_NegSize, base->lib.lib_NegSize + base->lib.lib_PosSize);
    return NULL;
}

static struct Library *lib_open(REG(a6, struct DTCBase *base))
{
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    return &base->lib;
}

static BPTR lib_expunge(REG(a6, struct DTCBase *base));

static BPTR lib_close(REG(a6, struct DTCBase *base))
{
    if (base->lib.lib_OpenCnt) base->lib.lib_OpenCnt--;
    if (base->lib.lib_OpenCnt == 0 && (base->lib.lib_Flags & LIBF_DELEXP)) return lib_expunge(base);
    return 0;
}

/* Objects or subclasses still out keep the class, and the library */
static BPTR lib_expunge(REG(a6, struct DTCBase *base))
{
    BPTR seglist;
    if (base->lib.lib_OpenCnt) {
        base->lib.lib_Flags |= LIBF_DELEXP;
        return 0;
    }
    RemoveClass(base->cl);
    if (!FreeClass(base->cl)) {
        AddClass(base->cl);
        base->lib.lib_Flags |= LIBF_DELEXP;
        return 0;
    }
    seglist = base->seglist;
    Remove(&base->lib.lib_Node);
    shut(base);
    FreeMem((UBYTE *)base - base->lib.lib_NegSize, base->lib.lib_NegSize + base->lib.lib_PosSize);
    return seglist;
}

static ULONG lib_null(void) { return 0; }

/* -30: the class (ObtainEngine) */
static struct IClass *L_ObtainEngine(REG(a6, struct DTCBase *base)) { return base->cl; }

#define DTC_STD_VECTORS (APTR)lib_open, (APTR)lib_close, (APTR)lib_expunge, (APTR)lib_null, (APTR)L_ObtainEngine

#define DTC_ROMTAG(vectors)                                                                        \
    static const struct {                                                                          \
        ULONG size;                                                                                \
        const APTR *vec;                                                                           \
        APTR data;                                                                                 \
        APTR init;                                                                                 \
    } lib_inittable = { sizeof(struct DTCBase), vectors, NULL, (APTR)lib_init };                    \
    const struct Resident lib_romtag = { RTC_MATCHWORD, (struct Resident *)&lib_romtag,             \
                                         (APTR)(&lib_romtag + 1), RTF_AUTOINIT, LIB_VERSION,        \
                                         NT_LIBRARY, 0, (char *)lib_name, (char *)lib_id,           \
                                         (APTR)&lib_inittable }

#endif
