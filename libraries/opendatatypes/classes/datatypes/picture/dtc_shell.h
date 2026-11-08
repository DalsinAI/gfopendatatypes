/* GFOpenDatatypes class libraries: the C around the picture, ilbm, sound and
 * 8svx cores. Built only on the os32-gcc16 stove, with the NDK 3.2
 * headers; the cores never see them.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * Clean-room, from the autodocs of exec, dos, intuition, graphics,
 * iffparse, datatypes and audio.device and the NDK 3.2 headers' published
 * layouts; never from Kickstart's, Hyperion's or AROS's code.
 *
 * A shell (open<name>_dtclass.c) defines, then includes this file:
 *   LIBNAME        "picture.datatype", or for the ABI test's side-by-side
 *                  build "gfopenpicture.datatype"; the public class has the
 *                  same name
 *   DTC_SUPER      the superclass: "datatypesclass", or a class library's
 *                  name ("picture.datatype")
 *   DTC_SUPERLIB   optional: the library to open for it
 *                  ("datatypes/picture.datatype")
 *   DTC_DISPATCH   the core's dispatcher, DTC_INST_SIZE its instance size call
 *   DTC_IDSTRING   the library's id string
 *   DTC_NEED_GFX   fill struct dtc_ext's graphics (picture, ilbm)
 *   DTC_NEED_AUDIO fill its audio player (sound, 8svx)
 * then DTC_ROMTAG.
 *
 * The library has one call of its own, at -30: the class (the NDK's
 * datatype class libraries' ObtainEngine).
 *
 * Multicore (OPEN_LIBRARIES.md section 5): anchored. The bases below are
 * set once at init and only read after; each object's state is its own.
 */
#ifndef OPENDATATYPES_CLASSES_DTC_SHELL_H
#define OPENDATATYPES_CLASSES_DTC_SHELL_H

#include <exec/types.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <intuition/intuition.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <intuition/gadgetclass.h>
#include <intuition/cghooks.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <libraries/iffparse.h>
#include <devices/audio.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/iffparse.h>

#include "dtc_env.h"

#ifndef REG
#define REG(r, decl) register decl __asm(#r)
#endif

/* The core's copies of the NDK's layouts are the NDK's own */
#define DTC_SAME(a, b) _Static_assert(sizeof(struct a) == sizeof(struct b), #a)
DTC_SAME(IClass, odt_IClass); DTC_SAME(Gadget, odt_Gadget); DTC_SAME(GadgetInfo, odt_GadgetInfo);
DTC_SAME(BitMap, odt_BitMap); DTC_SAME(TagItem, odt_TagItem); DTC_SAME(Hook, odt_Hook);
DTC_SAME(gpLayout, odt_gpLayout); DTC_SAME(gpInput, odt_gpInput); DTC_SAME(gpRender, odt_gpRender);
_Static_assert(__builtin_offsetof(struct DrawInfo, dri_Pens) == __builtin_offsetof(struct odt_DrawInfo, dri_Pens),
               "DrawInfo.dri_Pens");

#define LIB_VERSION  47
#define LIB_REVISION 100                /* 47.100 and up: GFOpenDatatypes */

struct DTCBase {
    struct Library lib;
    BPTR seglist;
    struct IClass *cl;
    struct Library *superlib;
    struct odt_cenv env;
    struct dtc_ext ext;
};

/* Set once by lib_init, read-only after */
struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *UtilityBase;
struct Library *IFFParseBase;
struct Library *DataTypesBase;

/* Run as a program, the library does nothing. The first code in the file
 * (-fno-toplevel-reorder). */
int start(void) { return -1; }

static const char lib_name[] = LIBNAME;
static const char lib_id[] = DTC_IDSTRING;

/* ---- BOOPSI, memory, DOS ------------------------------------------------ */

static odt_u32 e_do_method(const struct odt_cenv *e, void *obj, void *msg)
{ (void)e; return obj ? CallHookPkt(&OCLASS(obj)->cl_Dispatcher, obj, msg) : 0; }
static odt_u32 e_do_super(const struct odt_cenv *e, struct odt_IClass *cl, void *obj, void *msg)
{ (void)e; return CallHookPkt(&((struct IClass *)cl)->cl_Super->cl_Dispatcher, obj, msg); }
static odt_u32 e_coerce(const struct odt_cenv *e, struct odt_IClass *cl, void *obj, void *msg)
{ (void)e; return CallHookPkt(&((struct IClass *)cl)->cl_Dispatcher, obj, msg); }
static void *e_alloc(const struct odt_cenv *e, odt_u32 n) { (void)e; return AllocVec(n ? n : 1, MEMF_ANY | MEMF_CLEAR); }
static void e_free(const struct odt_cenv *e, void *p) { (void)e; if (p) FreeVec(p); }
static void e_set_io_err(const struct odt_cenv *e, odt_s32 c) { (void)e; SetIoErr(c); }
static void e_sem_obtain(const struct odt_cenv *e, struct odt_SignalSemaphore *s)
{ (void)e; ObtainSemaphore((struct SignalSemaphore *)s); }
static void e_sem_release(const struct odt_cenv *e, struct odt_SignalSemaphore *s)
{ (void)e; ReleaseSemaphore((struct SignalSemaphore *)s); }
static odt_s32 e_io_err(const struct odt_cenv *e) { (void)e; return IoErr(); }
static odt_s32 e_write(const struct odt_cenv *e, odt_u32 fh, const void *buf, odt_s32 n)
{ (void)e; return Write((BPTR)fh, (APTR)buf, n); }
static void *e_obtain_rp(const struct odt_cenv *e, struct odt_GadgetInfo *gi)
{ (void)e; return ObtainGIRPort((struct GadgetInfo *)gi); }
static void e_release_rp(const struct odt_cenv *e, void *rp) { (void)e; if (rp) ReleaseGIRPort((struct RastPort *)rp); }

/* ---- iffparse on the IFFHandle datatypesclass opened ---------------------- */

static odt_s32 e_iff_parse(const struct odt_cenv *e, void *iff, odt_s32 mode) { (void)e; return ParseIFF(iff, mode); }
static odt_s32 e_iff_prop(const struct odt_cenv *e, void *iff, odt_u32 type, odt_u32 id)
{ (void)e; return PropChunk(iff, type, id); }
static odt_s32 e_iff_stop(const struct odt_cenv *e, void *iff, odt_u32 type, odt_u32 id)
{ (void)e; return StopChunk(iff, type, id); }
static const void *e_iff_find_prop(const struct odt_cenv *e, void *iff, odt_u32 type, odt_u32 id, odt_s32 *size)
{
    struct StoredProperty *sp = FindProp(iff, type, id);
    (void)e;
    if (!sp) return NULL;
    *size = sp->sp_Size;
    return sp->sp_Data;
}
static odt_u32 e_iff_current(const struct odt_cenv *e, void *iff, odt_u32 *type, odt_s32 *size)
{
    struct ContextNode *cn = CurrentChunk(iff);
    (void)e;
    if (!cn) return 0;
    *type = cn->cn_Type;
    *size = cn->cn_Size;
    return cn->cn_ID;
}
static odt_s32 e_iff_read(const struct odt_cenv *e, void *iff, void *buf, odt_s32 n)
{ (void)e; return ReadChunkBytes(iff, buf, n); }

static void *x_alloc_chip(const struct odt_cenv *e, odt_u32 n) { (void)e; return AllocVec(n ? n : 2, MEMF_CHIP | MEMF_CLEAR); }

/* ---- graphics ------------------------------------------------------------ */

#ifdef DTC_NEED_GFX
static struct odt_BitMap *x_alloc_bitmap(const struct odt_cenv *e, odt_u32 w, odt_u32 h, odt_u32 d,
                                         struct odt_BitMap *fr)
{
    (void)e;
    return (struct odt_BitMap *)AllocBitMap(w, h, d, BMF_CLEAR, (struct BitMap *)fr);
}
static void x_free_bitmap(const struct odt_cenv *e, struct odt_BitMap *bm)
{ (void)e; if (bm) { WaitBlit(); FreeBitMap((struct BitMap *)bm); } }
static void x_write_lut8(const struct odt_cenv *e, struct odt_BitMap *bm, const odt_u8 *px, odt_u32 w, odt_u32 h,
                         odt_u32 mod)
{
    struct RastPort rp;
    (void)e;
    InitRastPort(&rp);
    rp.BitMap = (struct BitMap *)bm;
    WriteChunkyPixels(&rp, 0, 0, w - 1, h - 1, (UBYTE *)px, mod);
}
/* A true colour bitmap is RTG's: cybergraphics.library's WritePixelArray,
 * when the NDK in the stove has its header (else nothing is written: ABI
 * test, what OS 3.2 draws on an RTG screen) */
#if __has_include(<proto/cybergraphics.h>)
#include <proto/cybergraphics.h>
#include <cybergraphx/cybergraphics.h>
struct Library *CyberGfxBase;
static void x_write_argb(const struct odt_cenv *e, struct odt_BitMap *bm, const odt_u8 *px, odt_u32 w, odt_u32 h,
                         odt_u32 mod)
{
    struct RastPort rp;
    (void)e;
    if (!CyberGfxBase) return;
    InitRastPort(&rp);
    rp.BitMap = (struct BitMap *)bm;
    WritePixelArray((APTR)px, 0, 0, mod, &rp, 0, 0, w, h, RECTFMT_ARGB);
}
#define DTC_HAVE_CGX 1
#else
#define x_write_argb NULL
#endif
static odt_u32 x_screen_depth(const struct odt_cenv *e, void *s)
{ (void)e; return GetBitMapAttr(((struct Screen *)s)->RastPort.BitMap, BMA_DEPTH); }
static void *x_screen_cm(const struct odt_cenv *e, void *s) { (void)e; return ((struct Screen *)s)->ViewPort.ColorMap; }
static struct odt_BitMap *x_screen_bm(const struct odt_cenv *e, void *s)
{ (void)e; return (struct odt_BitMap *)((struct Screen *)s)->RastPort.BitMap; }
static odt_s32 x_obtain(const struct odt_cenv *e, void *cm, odt_u32 r, odt_u32 g, odt_u32 b, odt_s32 prec)
{
    struct TagItem t[3];
    (void)e;
    if (!cm) return -1;
    t[0].ti_Tag = OBP_Precision; t[0].ti_Data = (ULONG)prec;
    t[1].ti_Tag = OBP_FailIfBad; t[1].ti_Data = FALSE;
    t[2].ti_Tag = TAG_DONE;
    return ObtainBestPenA((struct ColorMap *)cm, r, g, b, t);
}
static void x_release(const struct odt_cenv *e, void *cm, odt_u32 pen) { (void)e; if (cm) ReleasePen(cm, pen); }
static void x_get_rgb(const struct odt_cenv *e, void *cm, odt_u32 pen, odt_u32 rgb[3])
{ (void)e; if (cm) GetRGB32(cm, pen, 1, (ULONG *)rgb); }
static void x_blit(const struct odt_cenv *e, void *rp, struct odt_BitMap *bm, const odt_u8 *mask, odt_s32 sx,
                   odt_s32 sy, odt_s32 x, odt_s32 y, odt_s32 w, odt_s32 h)
{
    (void)e;
    if (mask)
        BltMaskBitMapRastPort((struct BitMap *)bm, sx, sy, rp, x, y, w, h, 0xE0, (PLANEPTR)mask);
    else
        BltBitMapRastPort((struct BitMap *)bm, sx, sy, rp, x, y, w, h, 0xC0);
}
#endif

static void x_fill(const struct odt_cenv *e, void *rp, odt_u32 pen, odt_s32 x0, odt_s32 y0, odt_s32 x1, odt_s32 y1)
{
    (void)e;
    SetAPen(rp, pen);
    SetDrMd(rp, JAM1);
    RectFill(rp, x0, y0, x1, y1);
}
static void x_line(const struct odt_cenv *e, void *rp, odt_u32 pen, odt_s32 x0, odt_s32 y0, odt_s32 x1, odt_s32 y1)
{
    (void)e;
    SetAPen(rp, pen);
    SetDrMd(rp, JAM1);
    Move(rp, x0, y0);
    Draw(rp, x1, y1);
}

/* ---- audio: a player process per sound that plays ---------------------------- */

#ifdef DTC_NEED_AUDIO
#include "dtc_audio.h"

struct player {
    struct SignalSemaphore sem;         /* held by the player while it runs */
    struct Process *proc;               /* NULL once it has ended (set under Forbid) */
    struct Task *creator;
    const struct dtc_audio_plan *plan;
};

/* exec's BeginIO through the device's vector (-30), as amiga.lib's does:
 * SendIO would clear ADIOF_PERVOL */
static void begin_io(struct IORequest *io)
{
    register struct IORequest *a1 __asm("a1") = io;
    register struct Device *a0 __asm("a0") = io->io_Device;
    __asm volatile("move.l a6,-(sp)\n\tmove.l %1,a6\n\tjsr -30(a6)\n\tmove.l (sp)+,a6"
                   : "+r"(a1), "+r"(a0) : : "d0", "d1", "memory", "cc");
}

/* The player's requests: a0 the one that opened the device, io[] the four
 * slots dtc_audio_run sends (two steps of up to two channels) */
struct player_io {
    struct MsgPort *port;
    struct IOAudio *a0, *io[4];
    ULONG role_ch[3];                   /* the channels for roles 1, 2 and 3 */
};

static void pio_begin(void *ctx, odt_u32 slot, const struct dtc_audio_seg *g)
{
    struct player_io *p = ctx;
    struct IOAudio *io = p->io[slot];
    *io = *p->a0;
    io->ioa_Request.io_Command = CMD_WRITE;
    io->ioa_Request.io_Flags = ADIOF_PERVOL;
    io->ioa_Request.io_Unit = (struct Unit *)p->role_ch[(g->roles & 3) - 1];
    io->ioa_Data = (UBYTE *)g->data;
    io->ioa_Length = g->length;
    io->ioa_Period = g->period;
    io->ioa_Volume = g->volume;
    io->ioa_Cycles = g->cycles;
    begin_io((struct IORequest *)io);
}

static int pio_wait(void *ctx, odt_u32 slot)
{
    struct player_io *p = ctx;
    struct IORequest *io = (struct IORequest *)p->io[slot];
    while (!CheckIO(io))
        if (Wait((1UL << p->port->mp_SigBit) | SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) return 0;
    WaitIO(io);
    return 1;
}

static void pio_abort(void *ctx, odt_u32 slot)
{
    struct player_io *p = ctx;
    AbortIO((struct IORequest *)p->io[slot]);
    WaitIO((struct IORequest *)p->io[slot]);
}

static void player_main(void)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    struct player *pl = me->pr_Task.tc_UserData;
    const struct dtc_audio_plan *plan = pl->plan;
    struct player_io p;
    struct dtc_audio_io aio;
    int stopped = 0, k, ok;
    ObtainSemaphore(&pl->sem);
    Signal(pl->creator, SIGF_SINGLE);
    p.a0 = NULL;
    p.port = CreateMsgPort();
    if (p.port) p.a0 = (struct IOAudio *)CreateIORequest(p.port, sizeof(struct IOAudio));
    for (ok = 1, k = 0; k < 4; k++)
        if (!(p.io[k] = AllocVec(sizeof(struct IOAudio), MEMF_PUBLIC | MEMF_CLEAR))) ok = 0;
    if (p.a0 && ok) {
        p.a0->ioa_Request.io_Message.mn_Node.ln_Pri = 0;    /* ABI test: OS 3.2's allocation precedence */
        p.a0->ioa_Data = (UBYTE *)plan->alloc;
        p.a0->ioa_Length = plan->nalloc;
        if (!OpenDevice((CONST_STRPTR)AUDIONAME, 0, (struct IORequest *)p.a0, 0)) {
            ULONG units = (ULONG)p.a0->ioa_Request.io_Unit, l = units & 9, r = units & 6;
            p.role_ch[0] = l ? l : units;
            p.role_ch[1] = r ? r : units;
            p.role_ch[2] = units;
            aio.ctx = &p;
            aio.begin = pio_begin;
            aio.wait = pio_wait;
            aio.abort = pio_abort;
            /* double buffered: each step queued behind the one playing */
            stopped = !dtc_audio_run(plan, &aio);
            CloseDevice((struct IORequest *)p.a0);
        }
    }
    for (k = 0; k < 4; k++)
        if (p.io[k]) FreeVec(p.io[k]);
    if (p.a0) DeleteIORequest((struct IORequest *)p.a0);
    if (p.port) DeleteMsgPort(p.port);
    /* ABI test: OS 3.2 signals at the end only, not when stopped */
    if (!stopped && plan->signal_task && plan->signal_mask) Signal(plan->signal_task, plan->signal_mask);
    Forbid();                           /* ends forbidden: the library's code stays until it is gone */
    pl->proc = NULL;
    ReleaseSemaphore(&pl->sem);
}

static void x_stop(const struct odt_cenv *e, void **slot);

static int x_play(const struct odt_cenv *e, void **slot, const struct dtc_audio_plan *plan)
{
    struct player *pl;
    struct TagItem t[4];
    x_stop(e, slot);
    pl = AllocVec(sizeof *pl, MEMF_PUBLIC | MEMF_CLEAR);
    if (!pl) return 0;
    InitSemaphore(&pl->sem);
    pl->creator = FindTask(NULL);
    pl->plan = plan;
    t[0].ti_Tag = NP_Entry; t[0].ti_Data = (ULONG)player_main;
    t[1].ti_Tag = NP_Name; t[1].ti_Data = (ULONG) "GFOpenSound player";
    t[2].ti_Tag = NP_Priority; t[2].ti_Data = 21;   /* ABI test: OS 3.2's player priority */
    t[3].ti_Tag = TAG_DONE;
    SetSignal(0, SIGF_SINGLE);
    Forbid();
    pl->proc = CreateNewProc(t);
    if (pl->proc) pl->proc->pr_Task.tc_UserData = pl;
    Permit();
    if (!pl->proc) {
        FreeVec(pl);
        return 0;
    }
    Wait(SIGF_SINGLE);                  /* it holds the semaphore now */
    *slot = pl;
    return 1;
}

static void x_stop(const struct odt_cenv *e, void **slot)
{
    struct player *pl = *slot;
    (void)e;
    if (!pl) return;
    Forbid();
    if (pl->proc) Signal(&pl->proc->pr_Task, SIGBREAKF_CTRL_C);
    Permit();
    ObtainSemaphore(&pl->sem);          /* until the player is done with the plan */
    ReleaseSemaphore(&pl->sem);
    FreeVec(pl);
    *slot = NULL;
}
#endif

/* ---- the class's dispatcher: A0 the class, A2 the object, A1 the message */

static ULONG dispatch_entry(REG(a0, struct IClass *cl), REG(a2, Object *obj), REG(a1, Msg msg))
{
    struct DTCBase *base = (struct DTCBase *)cl->cl_UserData;
    return DTC_DISPATCH(&base->env, (struct odt_IClass *)cl, obj, msg);
}

static void env_init(struct DTCBase *base)
{
    struct odt_cenv *e = &base->env;
    struct dtc_ext *x = &base->ext;
    e->ctx = base;
    e->do_method = e_do_method;
    e->do_super = e_do_super;
    e->coerce = e_coerce;
    e->alloc = e_alloc;
    e->free = e_free;
    e->sem_obtain = e_sem_obtain;
    e->sem_release = e_sem_release;
    e->io_err = e_io_err;
    e->set_io_err = e_set_io_err;
    e->write = e_write;
    e->iff_parse = e_iff_parse;
    e->iff_prop = e_iff_prop;
    e->iff_stop = e_iff_stop;
    e->iff_find_prop = e_iff_find_prop;
    e->iff_current = e_iff_current;
    e->iff_read_chunk = e_iff_read;
    e->obtain_rp = e_obtain_rp;
    e->release_rp = e_release_rp;
    e->class_ext = x;
    x->alloc_chip = x_alloc_chip;
    x->fill = x_fill;
    x->line = x_line;
#ifdef DTC_NEED_GFX
    x->alloc_bitmap = x_alloc_bitmap;
    x->free_bitmap = x_free_bitmap;
    x->write_lut8 = x_write_lut8;
#ifdef DTC_HAVE_CGX
    x->write_argb = CyberGfxBase ? x_write_argb : NULL;   /* none: the layout takes pens */
#else
    x->write_argb = x_write_argb;
#endif
    x->screen_depth = x_screen_depth;
    x->screen_colormap = x_screen_cm;
    x->screen_bitmap = x_screen_bm;
    x->obtain_pen = x_obtain;
    x->release_pen = x_release;
    x->get_rgb32 = x_get_rgb;
    x->blit = x_blit;
#endif
#ifdef DTC_NEED_AUDIO
    x->audio_play = x_play;
    x->audio_stop = x_stop;
    x->audio_clock = (GfxBase->DisplayFlags & PAL) ? 3546895UL : 3579545UL;
#endif
}

/* ---- library housekeeping -------------------------------------------- */

static void shut(struct DTCBase *base)
{
    if (base->superlib) CloseLibrary(base->superlib);
#ifdef DTC_HAVE_CGX
    if (CyberGfxBase) CloseLibrary(CyberGfxBase);
#endif
    if (DataTypesBase) CloseLibrary(DataTypesBase);
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
    /* datatypesclass is datatypes.library's: open while our class lives */
    DataTypesBase = OpenLibrary((CONST_STRPTR) "datatypes.library", 39);
#ifdef DTC_HAVE_CGX
    CyberGfxBase = OpenLibrary((CONST_STRPTR) "cybergraphics.library", 40);
#endif
    if (DOSBase && IntuitionBase && GfxBase && UtilityBase && IFFParseBase && DataTypesBase) {
        env_init(base);
#ifdef DTC_SUPERLIB
        base->superlib = OpenLibrary((CONST_STRPTR)DTC_SUPERLIB, 0);
        if (!base->superlib) goto fail;
#endif
        base->cl = MakeClass((CONST_STRPTR)lib_name, (CONST_STRPTR)DTC_SUPER, NULL, DTC_INST_SIZE(), 0);
        if (base->cl) {
            base->cl->cl_UserData = (ULONG)base;
            base->cl->cl_Dispatcher.h_Entry = (__typeof__(base->cl->cl_Dispatcher.h_Entry))(void (*)(void))dispatch_entry;
            base->cl->cl_Dispatcher.h_SubEntry = NULL;
            base->cl->cl_Dispatcher.h_Data = base;
            AddClass(base->cl);
            return &base->lib;
        }
    }
#ifdef DTC_SUPERLIB
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
    if (base->lib.lib_OpenCnt)
        base->lib.lib_OpenCnt--;
    if (base->lib.lib_OpenCnt == 0 && (base->lib.lib_Flags & LIBF_DELEXP))
        return lib_expunge(base);
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

/* ObtainEngine(), -30: the class */
static struct IClass *L_ObtainEngine(REG(a6, struct DTCBase *base)) { return base->cl; }

static const APTR lib_vectors[] = { (APTR)lib_open, (APTR)lib_close, (APTR)lib_expunge, (APTR)lib_null,
                                    (APTR)L_ObtainEngine, (APTR)-1 };

#define DTC_ROMTAG                                                                                 \
    static const struct {                                                                          \
        ULONG size;                                                                                \
        const APTR *vec;                                                                           \
        APTR data;                                                                                 \
        APTR init;                                                                                 \
    } lib_inittable = { sizeof(struct DTCBase), lib_vectors, NULL, (APTR)lib_init };                \
    const struct Resident lib_romtag = { RTC_MATCHWORD, (struct Resident *)&lib_romtag,             \
                                         (APTR)(&lib_romtag + 1), RTF_AUTOINIT, LIB_VERSION,        \
                                         NT_LIBRARY, 0, (char *)lib_name, (char *)lib_id,           \
                                         (APTR)&lib_inittable }

#endif
