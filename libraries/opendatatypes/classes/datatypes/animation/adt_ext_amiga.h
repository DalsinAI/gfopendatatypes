/* GFOpenDatatypes: the animation family's services (struct adt_ext) on
 * AmigaOS 3.2, for the animation.datatype and anim.datatype shells.
 * Included after ../text/dtc_shell.h. Built only on the os32-gcc16 stove.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * Clean-room, from the graphics, audio.device, timer.device and dos
 * autodocs and the NDK 3.2 headers' layouts; never from Kickstart's,
 * Hyperion's or AROS's code.
 *
 * ADT_EXT_BITMAPS_ONLY (anim.datatype) leaves out the sound and the
 * player process: a subclass only makes and frees frames.
 */
#ifndef OPENDATATYPES_ADT_EXT_AMIGA_H
#define OPENDATATYPES_ADT_EXT_AMIGA_H

#include <graphics/gfx.h>
#include <graphics/view.h>
#include <devices/audio.h>
#include <devices/timer.h>
#include <dos/dostags.h>
#include <exec/ports.h>
#include <exec/io.h>
#include <datatypes/animationclass.h>
#include "animation_core.h"

_Static_assert(sizeof(struct BitMap) == sizeof(struct adt_BitMap), "BitMap");
_Static_assert(sizeof(struct adtFrame) == 40, "adtFrame");
_Static_assert(sizeof(struct adtNewFormatFrame) == sizeof(struct adt_Frame), "adtNewFormatFrame");

static struct adt_BitMap *ax_alloc_bitmap(const struct odt_cenv *e, odt_u32 w, odt_u32 h, odt_u32 depth, void *fr)
{
    (void)e;
    /* planar, as the cores read and write it: no friend (ABI test: OS
     * 3.2's frames on an RTG screen); not cleared, as every frame and
     * every remap is drawn whole */
    (void)fr;
    return (struct adt_BitMap *)AllocBitMap(w, h, depth, 0, NULL);
}

/* A frame bitmap in any memory (Fast RAM when there is some): the BitMap
 * and its planes in one block, its pad marked, which is how
 * ax_free_bitmap knows it from AllocBitMap's */
#define AX_ANY_MARK 0x4746

struct ax_any {
    struct BitMap bm;
    ULONG size;                         /* the block's */
};

static struct adt_BitMap *ax_alloc_any(const struct odt_cenv *e, odt_u32 w, odt_u32 h, odt_u32 depth)
{
    ULONG bpr = ((w + 15) >> 4) << 1, plane = bpr * h, size, p;
    struct ax_any *a;
    (void)e;
    if (!w || !h || !depth || depth > 8 || plane / bpr != h) return NULL;
    size = sizeof *a + depth * plane;
    if (!(a = AllocVec(size, MEMF_ANY))) return NULL;
    InitBitMap(&a->bm, depth, w, h);
    for (p = 0; p < depth; p++) a->bm.Planes[p] = (PLANEPTR)((UBYTE *)(a + 1) + p * plane);
    a->bm.pad = AX_ANY_MARK;
    a->size = size;
    return (struct adt_BitMap *)&a->bm;
}

static int ax_is_any(const struct BitMap *bm)
{
    return bm->pad == AX_ANY_MARK && bm->Planes[0] == (PLANEPTR)((UBYTE *)bm + sizeof(struct ax_any));
}

static void ax_free_bitmap(const struct odt_cenv *e, struct adt_BitMap *bm)
{
    (void)e;
    if (!bm) return;
    if (ax_is_any((struct BitMap *)bm)) {
        FreeVec(bm);                    /* the CPU's alone: no blit reads it */
        return;
    }
    WaitBlit();
    FreeBitMap((struct BitMap *)bm);
}

static int ax_in_chip(const struct odt_cenv *e, const struct adt_BitMap *bm)
{
    ULONG p;
    (void)e;
    for (p = 0; p < bm->Depth && p < 8; p++)
        if (bm->Planes[p] && bm->Planes[p] != (odt_u8 *)~0UL && !(TypeOfMem(bm->Planes[p]) & MEMF_CHIP)) return 0;
    return 1;
}

#ifndef ADT_EXT_BITMAPS_ONLY

static void ax_wait_blit(const struct odt_cenv *e)
{
    (void)e;
    WaitBlit();
}

static odt_s32 ax_obtain_pen(const struct odt_cenv *e, void *scr, odt_u32 r, odt_u32 g, odt_u32 b)
{
    struct TagItem t[2];
    (void)e;
    t[0].ti_Tag = OBP_Precision;
    t[0].ti_Data = PRECISION_IMAGE;
    t[1].ti_Tag = TAG_DONE;
    return ObtainBestPenA(((struct Screen *)scr)->ViewPort.ColorMap, r, g, b, t);
}

static void ax_release_pen(const struct odt_cenv *e, void *scr, odt_u32 pen)
{
    (void)e;
    ReleasePen(((struct Screen *)scr)->ViewPort.ColorMap, pen);
}

/* A frame's ColorMap, 16 colours a GetRGB32 (little stack: the player's) */
static odt_u32 ax_cmap_colors(const struct odt_cenv *e, void *cmap, odt_u32 n, odt_u8 *rgb)
{
    struct ColorMap *cm = cmap;
    ULONG t[3 * 16], i, k, got;
    (void)e;
    got = cm->Count < n ? cm->Count : n;
    for (i = 0; i < got; i += k) {
        k = got - i > 16 ? 16 : got - i;
        GetRGB32(cm, i, k, t);
        for (n = 0; n < 3 * k; n++) rgb[3 * i + n] = (UBYTE)(t[n] >> 24);
    }
    return got;
}

/* Colours into a screen's palette, 16 a LoadRGB32; never the Workbench's */
static void ax_load_colors(const struct odt_cenv *e, void *scr, odt_u32 first, odt_u32 n, const odt_u8 *rgb)
{
    struct Screen *s = scr;
    ULONG t[1 + 3 * 16 + 1], i, k, j;
    (void)e;
    if ((s->Flags & SCREENTYPE) == WBENCHSCREEN) return;
    for (i = 0; i < n; i += k) {
        k = n - i > 16 ? 16 : n - i;
        t[0] = k << 16 | (first + i);
        for (j = 0; j < 3 * k; j++) t[1 + j] = rgb[3 * i + j] * 0x01010101UL;
        t[1 + 3 * k] = 0;
        LoadRGB32(&s->ViewPort, t);
    }
}

static odt_u32 ax_screen_depth(const struct odt_cenv *e, void *scr)
{
    (void)e;
    return GetBitMapAttr(((struct Screen *)scr)->RastPort.BitMap, BMA_DEPTH);
}

static void ax_blit(const struct odt_cenv *e, struct adt_BitMap *bm, odt_s32 sx, odt_s32 sy, void *rp, odt_s32 dx,
                    odt_s32 dy, odt_s32 w, odt_s32 h)
{
    (void)e;
    BltBitMapRastPort((struct BitMap *)bm, sx, sy, (struct RastPort *)rp, dx, dy, w, h, 0xc0);
}

static void ax_fill(const struct odt_cenv *e, void *rp, odt_s32 pen, odt_s32 x0, odt_s32 y0, odt_s32 x1, odt_s32 y1)
{
    (void)e;
    if (x1 < x0 || y1 < y0) return;
    SetAPen((struct RastPort *)rp, pen);
    SetDrMd((struct RastPort *)rp, JAM1);
    RectFill((struct RastPort *)rp, x0, y0, x1, y1);
}

/* SHINEPEN and SHADOWPEN as the default DrawInfo has them (ABI test:
 * the screen's own) */
static void ax_bevel(const struct odt_cenv *e, void *rport, odt_s32 x, odt_s32 y, odt_s32 w, odt_s32 h, int rec)
{
    struct RastPort *rp = rport;
    (void)e;
    if (w < 2 || h < 2) return;
    SetDrMd(rp, JAM1);
    SetAPen(rp, rec ? 1 : 2);
    Move(rp, x, y + h - 1);
    Draw(rp, x, y);
    Draw(rp, x + w - 1, y);
    SetAPen(rp, rec ? 2 : 1);
    Draw(rp, x + w - 1, y + h - 1);
    Draw(rp, x + 1, y + h - 1);
}

static void ax_glyph(const struct odt_cenv *e, void *rport, odt_u32 kind, odt_s32 x, odt_s32 y, odt_s32 w, odt_s32 h,
                     odt_s32 pen)
{
    struct RastPort *rp = rport;
    odt_s32 s = w < h ? w : h, cx = x + (w - s) / 2, i;
    (void)e;
    if (s < 2) return;
    SetAPen(rp, pen);
    SetDrMd(rp, JAM1);
    if (kind == ADT_GLYPH_PLAY) {
        /* a triangle pointing right, one column at a time */
        for (i = 0; i < s; i++) {
            odt_s32 half = (s - 1) / 2 - i / 2;
            if (half < 0) break;
            Move(rp, cx + i, y + (h - 1) / 2 - half);
            Draw(rp, cx + i, y + (h - 1) / 2 + half);
        }
    } else if (kind == ADT_GLYPH_PAUSE) {
        RectFill(rp, cx, y + (h - s) / 2, cx + s / 3 - 1 + (s < 3), y + (h - s) / 2 + s - 1);
        RectFill(rp, cx + s - s / 3, y + (h - s) / 2, cx + s - 1, y + (h - s) / 2 + s - 1);
    } else
        RectFill(rp, cx, y + (h - s) / 2, cx + s - 1, y + (h - s) / 2 + s - 1);
}

static odt_s32 ax_pen(const struct odt_cenv *e, struct odt_GadgetInfo *gi, odt_u32 which)
{
    static const UBYTE defaults[] = { 0, 1, 1, 2, 1, 3, 1, 0, 2 };
    struct GadgetInfo *g = (struct GadgetInfo *)gi;
    (void)e;
    if (g && g->gi_DrInfo && which < g->gi_DrInfo->dri_NumPens) return g->gi_DrInfo->dri_Pens[which];
    return which < sizeof defaults ? defaults[which] : 1;
}

static odt_u32 ax_font_ysize(const struct odt_cenv *e, struct odt_GadgetInfo *gi)
{
    struct GadgetInfo *g = (struct GadgetInfo *)gi;
    (void)e;
    if (g && g->gi_DrInfo && g->gi_DrInfo->dri_Font) return g->gi_DrInfo->dri_Font->tf_YSize;
    return GfxBase->DefaultFont ? GfxBase->DefaultFont->tf_YSize : 8;
}

static odt_u32 ax_clock(const struct odt_cenv *e)
{
    (void)e;
    return (GfxBase->DisplayFlags & PAL) ? 3546895UL : 3579545UL;
}

/* ---- sound: audio.device, one channel, from any task ------------------ */

/* The reply port signals nobody (PA_IGNORE), so the task that stops the
 * sound needn't be the one that started it: completion is polled. */
struct ax_sound {
    struct SignalSemaphore sem;         /* the player and the application both play */
    struct MsgPort port;
    struct IOAudio io;
    BYTE *chip;                         /* the data, copied to chip memory */
    ULONG chip_len;
    UBYTE open, busy;
};

static void ax_sound_stop_(struct ax_sound *s)
{
    if (!s->busy) return;
    AbortIO((struct IORequest *)&s->io);
    while (!CheckIO((struct IORequest *)&s->io)) {}   /* audio.device ends an aborted write at once */
    Forbid();
    Remove(&s->io.ioa_Request.io_Message.mn_Node);
    Permit();
    s->busy = 0;
}

static void ax_begin_io(struct IORequest *io)
{
    /* the device's BeginIO (LVO -30), which keeps ADIOF_PERVOL (SendIO
     * clears io_Flags) */
    register struct IORequest *a1 __asm("a1") = io;
    register struct Device *a6 __asm("a6") = io->io_Device;
    __asm volatile ("jsr -30(%%a6)" : "+a"(a1), "+a"(a6) : : "d0", "d1", "a0", "cc", "memory");
}

static struct ax_sound *ax_sound(const struct odt_cenv *e);

static int ax_sound_play_(struct ax_sound *s, const odt_s8 *data, odt_u32 len, odt_u32 period, odt_u32 volume,
                          odt_u32 cycles, const UBYTE *channels);

static int ax_sound_play(const struct odt_cenv *e, const odt_s8 *data, odt_u32 len, odt_u32 period, odt_u32 volume,
                         odt_u32 cycles)
{
    static const UBYTE channels[] = { 1, 8, 2, 4 };
    struct ax_sound *s = ax_sound(e);
    int ok;
    if (!s || !len) return 0;
    ObtainSemaphore(&s->sem);
    ok = ax_sound_play_(s, data, len, period, volume, cycles, channels);
    ReleaseSemaphore(&s->sem);
    return ok;
}

static int ax_sound_play_(struct ax_sound *s, const odt_s8 *data, odt_u32 len, odt_u32 period, odt_u32 volume,
                          odt_u32 cycles, const UBYTE *channels)
{
    ax_sound_stop_(s);
    if (len > 131070UL) len = 131070UL;   /* one write's most: longer, not yet */
    len &= ~1UL;
    if (!len) return 0;
    if (!s->open) {
        s->port.mp_Node.ln_Type = NT_MSGPORT;
        s->port.mp_Flags = PA_IGNORE;
        s->port.mp_MsgList.lh_Head = (struct Node *)&s->port.mp_MsgList.lh_Tail;   /* NewList */
        s->port.mp_MsgList.lh_Tail = NULL;
        s->port.mp_MsgList.lh_TailPred = (struct Node *)&s->port.mp_MsgList.lh_Head;
        s->io.ioa_Request.io_Message.mn_ReplyPort = &s->port;
        s->io.ioa_Request.io_Message.mn_Length = sizeof s->io;
        s->io.ioa_Request.io_Message.mn_Node.ln_Pri = 0;
        s->io.ioa_Data = (UBYTE *)channels;
        s->io.ioa_Length = 4;
        if (OpenDevice((CONST_STRPTR) "audio.device", 0, (struct IORequest *)&s->io, 0)) return 0;
        s->open = 1;
    }
    if (s->chip_len < len) {
        if (s->chip) FreeVec(s->chip);
        s->chip_len = 0;
        if (!(s->chip = AllocVec(len, MEMF_CHIP))) return 0;
        s->chip_len = len;
    }
    CopyMem((APTR)data, s->chip, len);
    s->io.ioa_Request.io_Command = CMD_WRITE;
    s->io.ioa_Request.io_Flags = ADIOF_PERVOL;
    s->io.ioa_Data = (UBYTE *)s->chip;
    s->io.ioa_Length = len;
    s->io.ioa_Period = (UWORD)(period < 124 ? 124 : period);
    s->io.ioa_Volume = (UWORD)(volume > 64 ? 64 : volume);
    s->io.ioa_Cycles = (UWORD)cycles;
    ax_begin_io((struct IORequest *)&s->io);
    s->busy = 1;
    return 1;
}

static void ax_sound_stop(const struct odt_cenv *e)
{
    struct ax_sound *s = ax_sound(e);
    if (!s) return;
    ObtainSemaphore(&s->sem);
    ax_sound_stop_(s);
    ReleaseSemaphore(&s->sem);
}

static void ax_sound_free(struct ax_sound *s)
{
    if (!s) return;
    ax_sound_stop_(s);
    if (s->open) CloseDevice((struct IORequest *)&s->io);
    if (s->chip) FreeVec(s->chip);
    FreeVec(s);
}

/* ---- the player process ------------------------------------------------- */

struct ax_player {
    struct Message msg;                 /* the start message, to the process */
    const struct odt_cenv *env;
    struct odt_IClass *cl;
    void *obj;
    ULONG fps;
    struct SignalSemaphore sem;         /* held while it ticks */
    volatile UBYTE stop;
    struct Process *proc;
};

static void ax_player_main(void)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    struct ax_player *p;
    struct MsgPort *tport;
    struct timerequest *tr = NULL;
    WaitPort(&me->pr_MsgPort);
    p = (struct ax_player *)GetMsg(&me->pr_MsgPort);
    if ((tport = CreateMsgPort()) != NULL &&
        (tr = (struct timerequest *)CreateIORequest(tport, sizeof *tr)) != NULL &&
        OpenDevice((CONST_STRPTR) "timer.device", UNIT_MICROHZ, (struct IORequest *)tr, 0) == 0) {
        ULONG us = 1000000UL / (p->fps ? p->fps : 1);
        for (;;) {
            ULONG got;
            tr->tr_node.io_Command = TR_ADDREQUEST;
            tr->tr_time.tv_secs = us / 1000000UL;
            tr->tr_time.tv_micro = us % 1000000UL;
            SendIO((struct IORequest *)tr);
            got = Wait((1UL << tport->mp_SigBit) | SIGBREAKF_CTRL_C);
            if (got & SIGBREAKF_CTRL_C) {
                AbortIO((struct IORequest *)tr);
            }
            WaitIO((struct IORequest *)tr);
            ObtainSemaphore(&p->sem);
            if (p->stop) {
                ReleaseSemaphore(&p->sem);
                break;
            }
            adt_tick(p->env, p->cl, p->obj);
            ReleaseSemaphore(&p->sem);
        }
        CloseDevice((struct IORequest *)tr);
    } else {
        /* no timer: it waits to be stopped */
        while (!p->stop) Wait(SIGBREAKF_CTRL_C);
    }
    if (tr) DeleteIORequest((struct IORequest *)tr);
    if (tport) DeleteMsgPort(tport);
    /* the last of our code runs under Forbid: whoever stopped us hears it
     * only once this process is gone, so the library can't be unloaded
     * under it (with no one waiting, p is ours to free) */
    Forbid();
    if (p->msg.mn_ReplyPort)
        ReplyMsg(&p->msg);
    else
        FreeVec(p);
}

static void *ax_player_start(const struct odt_cenv *e, struct odt_IClass *cl, void *obj, odt_u32 fps)
{
    struct ax_player *p = AllocVec(sizeof *p, MEMF_PUBLIC | MEMF_CLEAR);
    struct TagItem t[4];
    if (!p) return NULL;
    p->env = e;
    p->cl = cl;
    p->obj = obj;
    p->fps = fps;
    InitSemaphore(&p->sem);
    t[0].ti_Tag = NP_Entry;    t[0].ti_Data = (ULONG)ax_player_main;
    t[1].ti_Tag = NP_Name;     t[1].ti_Data = (ULONG) "animation.datatype player";
    t[2].ti_Tag = NP_Priority; t[2].ti_Data = 1;
    t[3].ti_Tag = TAG_DONE;
    if (!(p->proc = CreateNewProc(t))) {
        FreeVec(p);
        return NULL;
    }
    p->msg.mn_Node.ln_Type = NT_MESSAGE;
    p->msg.mn_Length = sizeof *p;
    p->msg.mn_ReplyPort = NULL;
    PutMsg(&p->proc->pr_MsgPort, &p->msg);
    return p;
}

/* Stops the process and waits until it is gone (its start message comes
 * back as its last act, under Forbid), then frees p: nothing of ours runs
 * after this returns, so the library may be closed and expunged. Safe
 * from any task (no dos calls), and it needs no memory: the answer comes
 * to a port on our stack, on SIGF_SINGLE (no semaphore of ours is waiting
 * on it then). From the process itself it doesn't wait: the process frees
 * p as it goes. */
static void ax_player_stop(const struct odt_cenv *e, void *pp)
{
    struct ax_player *p = pp;
    struct Process *proc;
    struct MsgPort port;
    int wait;
    (void)e;
    if (!p) return;
    proc = p->proc;
    wait = FindTask(NULL) != &proc->pr_Task;
    if (wait) {
        port.mp_Node.ln_Type = NT_MSGPORT;
        port.mp_Node.ln_Name = NULL;
        port.mp_Node.ln_Pri = 0;
        port.mp_Flags = PA_SIGNAL;
        port.mp_SigBit = SIGB_SINGLE;
        port.mp_SigTask = FindTask(NULL);
        port.mp_MsgList.lh_Head = (struct Node *)&port.mp_MsgList.lh_Tail;
        port.mp_MsgList.lh_Tail = NULL;
        port.mp_MsgList.lh_TailPred = (struct Node *)&port.mp_MsgList.lh_Head;
    }
    ObtainSemaphore(&p->sem);
    p->msg.mn_ReplyPort = wait ? &port : NULL;
    p->stop = 1;
    Signal(&proc->pr_Task, SIGBREAKF_CTRL_C);
    ReleaseSemaphore(&p->sem);
    if (!wait) return;
    while (!GetMsg(&port))
        WaitPort(&port);
    SetSignal(0, SIGF_SINGLE);          /* none left over for a semaphore's wait */
    FreeVec(p);
}

#endif /* ADT_EXT_BITMAPS_ONLY */

#endif
