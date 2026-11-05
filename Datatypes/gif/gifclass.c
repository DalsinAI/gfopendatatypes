/*
 * gfopengif.datatype: GIF pictures and animations for AmigaOS 3.x, a
 * picture.datatype (V43 mode) subclass that decodes on the Amiga itself
 * (gifdecode.c, our own LZW; no other library).
 *
 * Every frame of an animation is there, composited as web browsers show
 * them (gifdecode.h):
 *
 * - The object's picture is frame 0, or the frame PDTA_WhichPicture names
 *   at OM_NEW (the V44 way to choose a picture in a file of several).
 *   Frame 0 goes to picture.datatype as the file's own colour indexes and
 *   table (8 bits or fewer, bmh_Transparent for its transparent index)
 *   whenever every pixel has a colour or the frame has a transparent index;
 *   other frames, and the rare first frame that can't, as 32-bit ARGB;
 *   PDTA_GetNumPictures and OIA_Frames give the number of frames.
 * - OIM_READFRAME (datatypes/gfopenimage.h) writes any frame into a
 *   program's pixels, with its delay; OIA_LoopCount gives the loop count.
 *   GFOpenBrowser plays animated GIFs this way.
 *
 * A one-frame GIF frees the file once it is decoded; an animation keeps it,
 * with the screen, while the object lives (OIM_READFRAME decodes from it).
 *
 * 47.3: an animated GIF opened with no DTA_GroupID (MultiView, and other
 * programs that take whatever the file is) comes back as an animation, as
 * an IFF ANIM does: an object of a class of this library's own whose
 * superclass is animation.datatype, which plays it with its control
 * panel. Its frames are 8-bit bitmaps in one colour table for the whole
 * file: the GIF's own colours when all its tables together have 256 or
 * fewer (nearly every GIF), else a 6x6x6 cube. Pixels no frame gives a
 * colour show the GIF's background colour. A program that asks for
 * GID_PICTURE (GFOpenBrowser does), or names PDTA_WhichPicture, gets the
 * picture as before, with OIM_READFRAME. The descriptor stays a picture
 * one, so a program that asks for GID_ANIMATION is still refused.
 *
 * DEVS:DataTypes/GFOpenGIF (build.sh) matches "GIF87a" or "GIF89a" and the
 * screen size after it, a longer mask than the system's GIF descriptor,
 * which AmigaOS 3.2.3's datatypes.library tries first: this class takes GIFs
 * while the system's gif.datatype stays installed and untouched. Removing
 * the descriptor gives GIFs back to it.
 *
 * A read or write of the pixels with no pixel array is refused, as in
 * gfopenpng.datatype (see there).
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <string.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <dos/dos.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <datatypes/animationclass.h>
#include <intuition/classes.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/datatypes.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>

#include "dtlib.h"
#include <datatypes/gfopenimage.h>
#include "gifdecode.h"

/* V44's tags for files of several pictures (the NDK's numbers). */
#ifndef PDTA_WhichPicture
#define PDTA_WhichPicture   (DTA_Dummy + 219)
#endif
#ifndef PDTA_GetNumPictures
#define PDTA_GetNumPictures (DTA_Dummy + 220)
#endif

const char LibName[] = "gfopengif.datatype";
const char LibIdString[] = "gfopengif.datatype 47.3 (5.11.2026) AmiCradle Team";
const UWORD LibVersion = 47;
const UWORD LibRevision = 3;
static const char version[] __attribute__((used)) = "$VER: gfopengif.datatype 47.3 (5.11.2026)";

const char dt_superclass[] = "picture.datatype";
const UWORD dt_superversion = 43;

/* Kept while the object lives (animations only; zero for a still). */
struct gifdata {
    UBYTE *file;
    ULONG size;
    struct gif_info info;
    struct gif_frame *frame;
    struct gif_canvas canvas;
    ULONG first;                      /* the frame the object's picture is */
};

ULONG dt_instsize = sizeof(struct gifdata);

/* The file read by OM_NEW, handed to the object that keeps it (a private
 * tag, never seen outside this library): whoever takes it sets file NULL. */
#define GIFA_Hand (TAG_USER + 0x0DA159F0)
struct gifhand {
    UBYTE *file;
    ULONG size;
};

/* The animation class, made the first time an animation is opened (a
 * picture never loads animation.datatype). */
static struct SignalSemaphore animLock;
static struct Library *animBase;
static Class *animClass;

BOOL dt_init(void)
{
    InitSemaphore(&animLock);
    return TRUE;
}

void dt_cleanup(void)
{
    /* Expunge comes only when no object of the library is left. */
    if (animClass)
        FreeClass(animClass);
    animClass = NULL;
    if (animBase)
        CloseLibrary(animBase);
    animBase = NULL;
}

/* Fast RAM when there is any: the decode never needs Chip RAM. */
static APTR big_alloc(ULONG n)
{
    APTR p = AllocVec(n, MEMF_FAST);
    return p ? p : AllocVec(n, MEMF_ANY);
}

static void freeData(struct gifdata *d)
{
    if (d->file) FreeVec(d->file);
    if (d->frame) FreeVec(d->frame);
    if (d->canvas.argb) FreeVec(d->canvas.argb);
    if (d->canvas.saved) FreeVec(d->canvas.saved);
    if (d->canvas.work) FreeVec(d->canvas.work);
    d->file = NULL;
    d->frame = NULL;
    d->canvas.argb = d->canvas.saved = NULL;
    d->canvas.work = NULL;
}

/* The first frame as colour indexes with its own table (gif_frame_lut8()):
 * a GIF has at most 256 colours a frame, so programs on a palette screen
 * get the file's colours, not 32 bits picture.datatype has to bring down
 * again, in a quarter of the memory. It can when no pixel is left without
 * a colour, or the frame has a transparent index to give those (it then
 * becomes bmh_Transparent). FALSE, with nothing changed, when it can't or
 * runs out of memory: the 32-bit route is taken. */
static BOOL writeLUT8(Class *cl, Object *o, struct gifdata *d, struct BitMapHeader *bmh, STRPTR name)
{
    const struct gif_frame *f = &d->frame[0];
    ULONG pixels = d->info.width * d->info.height, holes, colours, i, *cregs = NULL;
    struct ColorRegister *cmap = NULL;
    UBYTE *lut8, depth;
    APTR work;
    BOOL ok;

    if (!f->colours)
        return FALSE;
    if (!(lut8 = big_alloc(pixels)))
        return FALSE;
    if (!(work = d->canvas.work ? d->canvas.work : AllocVec(GIF_WORKSIZE, MEMF_ANY))) {
        FreeVec(lut8);
        return FALSE;
    }
    gif_frame_lut8(d->file, d->size, &d->info, f, lut8, work, f->transparent ? f->trans : 0, &holes);
    if (work != d->canvas.work)
        FreeVec(work);
    if (holes && !f->transparent) {
        FreeVec(lut8);
        return FALSE;
    }
    colours = f->colours;
    if (holes && f->trans >= colours)
        colours = (ULONG)f->trans + 1;   /* the transparent index past the table: black there */
    for (depth = 1; (1UL << depth) < colours; depth++)
        ;
    bmh->bmh_Width = bmh->bmh_PageWidth = (UWORD)d->info.width;
    bmh->bmh_Height = bmh->bmh_PageHeight = (UWORD)d->info.height;
    bmh->bmh_Depth = depth;
    bmh->bmh_Masking = holes ? mskHasTransparentColor : mskNone;
    bmh->bmh_Transparent = holes ? f->trans : 0;
    SetDTAttrs(o, NULL, NULL,
        DTA_ObjName, (ULONG)(name ? FilePart(name) : (STRPTR)"GIF"),
        DTA_NominalHoriz, d->info.width,
        DTA_NominalVert, d->info.height,
        PDTA_SourceMode, PMODE_V43,
        PDTA_NumColors, colours,
        TAG_DONE);
    if (GetDTAttrs(o, PDTA_ColorRegisters, (ULONG)&cmap, PDTA_CRegs, (ULONG)&cregs, TAG_DONE) < 2 || !cmap || !cregs) {
        FreeVec(lut8);
        return FALSE;
    }
    for (i = 0; i < colours; i++) {
        const UBYTE *c = i < f->colours ? d->file + f->palette + 3 * i : (const UBYTE *)"\0\0\0";
        cmap[i].red = c[0];
        cmap[i].green = c[1];
        cmap[i].blue = c[2];
        cregs[i * 3] = c[0] * 0x01010101UL;
        cregs[i * 3 + 1] = c[1] * 0x01010101UL;
        cregs[i * 3 + 2] = c[2] * 0x01010101UL;
    }
    ok = DoSuperMethod(cl, o, PDTM_WRITEPIXELARRAY, (ULONG)lut8, PBPAFMT_LUT8,
        d->info.width, 0, 0, d->info.width, d->info.height) ? TRUE : FALSE;
    FreeVec(lut8);
    return ok;
}

static BOOL loadGIF(Class *cl, Object *o, struct TagItem *attrs, struct gifhand *hand)
{
    struct gifdata *d = INST_DATA(cl, o);
    struct BitMapHeader *bmh = NULL;
    STRPTR name = NULL;
    ULONG pixels, which;
    LONG n, err = ERROR_NO_FREE_STORE;
    BOOL ok = FALSE, alpha, animated;

    if (GetDTAttrs(o, PDTA_BitMapHeader, (ULONG)&bmh, DTA_Name, (ULONG)&name, TAG_DONE) < 1 || !bmh) {
        SetIoErr(ERROR_OBJECT_WRONG_TYPE);
        return FALSE;
    }
    if (hand && hand->file) {         /* read already: ours now */
        d->file = hand->file;
        d->size = hand->size;
        hand->file = NULL;
    } else if (!(d->file = dt_read_source(o, &d->size)))
        return FALSE;                 /* IoErr() says why */
    if ((n = gif_scan(d->file, d->size, &d->info, NULL, 0)) < 0) {
        freeData(d);
        SetIoErr(DTERROR_INVALID_DATA);
        return FALSE;
    }
    pixels = d->info.width * d->info.height;
    animated = n > 1;
    d->canvas.shown = -1;
    if (!(d->frame = AllocVec(sizeof(struct gif_frame) * (ULONG)n, MEMF_ANY))) {
        freeData(d);
        SetIoErr(err);
        return FALSE;
    }
    gif_scan(d->file, d->size, &d->info, d->frame, (ULONG)n);
    which = GetTagData(PDTA_WhichPicture, 0, attrs);
    if (which >= d->info.frames)
        which = d->info.frames - 1;
    d->first = which;
    /* An animation keeps an ARGB screen for OIM_READFRAME; a still needs
     * one only when it can't go as colour indexes. */
    if (animated && (!(d->canvas.argb = big_alloc(pixels * 4))
            || (d->info.restores && !(d->canvas.saved = big_alloc(pixels * 4)))
            || !(d->canvas.work = AllocVec(GIF_WORKSIZE, MEMF_ANY)))) {
        freeData(d);
        SetIoErr(err);
        return FALSE;
    }
    if (which == 0 && writeLUT8(cl, o, d, bmh, name))
        ok = TRUE;
    else if ((d->canvas.argb || (d->canvas.argb = big_alloc(pixels * 4)))
        && (d->canvas.work || (d->canvas.work = AllocVec(GIF_WORKSIZE, MEMF_ANY)))
        && (!d->info.restores || d->canvas.saved || (d->canvas.saved = big_alloc(pixels * 4)))) {
        gif_show(d->file, d->size, &d->info, d->frame, &d->canvas, which);
        alpha = gif_has_alpha(&d->info, &d->canvas) ? TRUE : FALSE;
        bmh->bmh_Width = bmh->bmh_PageWidth = (UWORD)d->info.width;
        bmh->bmh_Height = bmh->bmh_PageHeight = (UWORD)d->info.height;
        bmh->bmh_Depth = 32;          /* ARGB */
        bmh->bmh_Masking = alpha ? mskHasAlpha : mskNone;
        SetDTAttrs(o, NULL, NULL,
            DTA_ObjName, (ULONG)(name ? FilePart(name) : (STRPTR)"GIF"),
            DTA_NominalHoriz, d->info.width,
            DTA_NominalVert, d->info.height,
            PDTA_SourceMode, PMODE_V43,
            PDTA_AlphaChannel, alpha,
            TAG_DONE);
        ok = DoSuperMethod(cl, o, PDTM_WRITEPIXELARRAY, (ULONG)d->canvas.argb, PBPAFMT_ARGB,
            d->info.width * 4, 0, 0, d->info.width, d->info.height) ? TRUE : FALSE;
    }
    if (!ok || !animated)
        freeData(d);                  /* a still: nothing more to read */
    if (!ok)
        SetIoErr(err);
    return ok;
}

/* ---- Animated GIFs as an animation.datatype -------------------------------- */

struct animdata {
    struct SignalSemaphore lock;      /* the loader process and OM_NEW take turns */
    UBYTE *file;
    ULONG size;
    struct gif_info info;
    struct gif_frame *frame;
    struct gif_canvas8 c8;            /* the frames as indexes of one table */
    struct gif_canvas argb;           /* or, past 256 colours, as ARGB for the cube */
    UBYTE *remaps;                    /* 256 bytes a distinct table */
    UWORD *tables;                    /* each frame's remap */
    UBYTE *cube;                      /* the cube's indexes, a frame at a time */
    struct BitMap *keyFrame;
    ULONG tick;                       /* hundredths of a second a player tick is */
    UBYTE depth;
    UBYTE pad[3];
    UWORD colours;
    UBYTE palette[256 * 3];
};

static void freeAnim(struct animdata *a)
{
    if (a->keyFrame) {
        WaitBlit();
        FreeBitMap(a->keyFrame);
    }
    if (a->file) FreeVec(a->file);
    if (a->frame) FreeVec(a->frame);
    if (a->c8.lut8) FreeVec(a->c8.lut8);
    if (a->c8.saved) FreeVec(a->c8.saved);
    if (a->c8.work) FreeVec(a->c8.work);
    if (a->argb.argb) FreeVec(a->argb.argb);
    if (a->argb.saved) FreeVec(a->argb.saved);
    if (a->remaps) FreeVec(a->remaps);
    if (a->tables) FreeVec(a->tables);
    if (a->cube) FreeVec(a->cube);
    a->keyFrame = NULL;
    a->file = NULL;
    a->frame = NULL;
    a->c8.lut8 = a->c8.saved = a->cube = a->remaps = NULL;
    a->c8.work = NULL;
    a->argb.argb = a->argb.saved = NULL;
    a->tables = NULL;
}

/* One table for every frame (gif_one_table()). FALSE past 256 colours,
 * or out of memory (*nomem). */
static BOOL oneTable(struct animdata *a, BOOL *nomem)
{
    ULONG k, distinct;
    UBYTE *smaller;
    LONG n;

    *nomem = FALSE;
    if (!(a->tables = AllocVec(sizeof(UWORD) * a->info.frames, MEMF_ANY))
        || !(a->remaps = AllocVec(256UL * a->info.frames, MEMF_ANY | MEMF_CLEAR))) {
        *nomem = TRUE;
        return FALSE;
    }
    if ((n = gif_one_table(a->file, a->size, &a->info, a->frame, a->palette, a->remaps, a->tables, &a->c8.back)) < 0)
        return FALSE;
    a->colours = (UWORD)n;
    /* Most GIFs have one table: keep only the remaps used. */
    for (k = 0, distinct = 0; k < a->info.frames; k++)
        if (a->tables[k] >= distinct)
            distinct = a->tables[k] + 1UL;
    if (distinct < a->info.frames && (smaller = AllocVec(256 * distinct, MEMF_ANY)) != NULL) {
        CopyMem(a->remaps, smaller, 256 * distinct);
        FreeVec(a->remaps);
        a->remaps = smaller;
    }
    return TRUE;
}

/* The 6x6x6 cube, for a GIF of more than 256 colours. */
static void cubeTable(struct animdata *a)
{
    ULONG i, gsize = (a->file[10] & 0x80) ? 2UL << (a->file[10] & 7) : 0;
    const UBYTE *back = gsize && a->file[11] < gsize ? a->file + 13 + 3 * a->file[11] : (const UBYTE *)"\0\0\0";

    for (i = 0; i < 216; i++) {
        a->palette[i * 3] = (UBYTE)((i / 36) * 51);
        a->palette[i * 3 + 1] = (UBYTE)(((i / 6) % 6) * 51);
        a->palette[i * 3 + 2] = (UBYTE)((i % 6) * 51);
    }
    a->colours = 216;
    a->c8.back = (UBYTE)(((back[0] + 25) / 51) * 36 + ((back[1] + 25) / 51) * 6 + (back[2] + 25) / 51);
}

/* Frame index composited, as colour indexes width bytes a row. */
static const UBYTE *showFrame(struct animdata *a, ULONG index)
{
    ULONG n, count;

    if (!a->cube) {
        gif_show8(a->file, a->size, &a->info, a->frame, &a->c8, index);
        return a->c8.lut8;
    }
    gif_show(a->file, a->size, &a->info, a->frame, &a->argb, index);
    count = a->info.width * a->info.height;
    for (n = 0; n < count; n++) {
        ULONG c = a->argb.argb[n];
        a->cube[n] = !(c >> 24) ? a->c8.back
            : (UBYTE)((((c >> 16 & 0xff) + 25) / 51) * 36 + (((c >> 8 & 0xff) + 25) / 51) * 6 + ((c & 0xff) + 25) / 51);
    }
    return a->cube;
}

static BOOL drawFrame(struct animdata *a, ULONG index, struct BitMap *bm)
{
    struct RastPort rp;
    const UBYTE *pixels;

    ObtainSemaphore(&a->lock);
    pixels = showFrame(a, index);
    InitRastPort(&rp);
    rp.BitMap = bm;
    WriteChunkyPixels(&rp, 0, 0, a->info.width - 1, a->info.height - 1, (UBYTE *)pixels, a->info.width);
    ReleaseSemaphore(&a->lock);
    return TRUE;
}

/* The player's tick: the longest of 1, 1/2, 1/4, 1/5, 1/10, 1/20, 1/25
 * and 1/50 second that every frame's delay is a whole number of (a delay
 * of 0 or 1 hundredth is 1/10 s, as browsers play it), else 1/50 s, the
 * delays rounded to it. */
static ULONG frameDelay(const struct gif_frame *f)
{
    return f->delay < 2 ? 10 : f->delay;
}

static ULONG chooseTick(const struct animdata *a)
{
    static const UBYTE ticks[] = { 100, 50, 25, 20, 10, 5, 4, 2 };
    ULONG t, k;

    for (t = 0; t < sizeof ticks; t++) {
        for (k = 0; k < a->info.frames; k++)
            if (frameDelay(&a->frame[k]) % ticks[t])
                break;
        if (k == a->info.frames)
            return ticks[t];
    }
    return 2;
}

static ULONG frameTicks(const struct animdata *a, ULONG index)
{
    ULONG d = (frameDelay(&a->frame[index]) + a->tick / 2) / a->tick;
    return d ? d : 1;
}

static void setColours(Object *o, const struct animdata *a)
{
    struct ColorRegister *cmap = NULL;
    ULONG *cregs = NULL, *gregs = NULL, i;

    SetDTAttrs(o, NULL, NULL, ADTA_NumColors, a->colours, TAG_DONE);
    GetDTAttrs(o, ADTA_ColorRegisters, (ULONG)&cmap, ADTA_CRegs, (ULONG)&cregs, ADTA_GRegs, (ULONG)&gregs, TAG_DONE);
    for (i = 0; i < a->colours; i++) {
        UBYTE r = a->palette[i * 3], g = a->palette[i * 3 + 1], b = a->palette[i * 3 + 2];
        if (cmap) {
            cmap[i].red = r;
            cmap[i].green = g;
            cmap[i].blue = b;
        }
        if (cregs) {
            cregs[i * 3] = r * 0x01010101UL;
            cregs[i * 3 + 1] = g * 0x01010101UL;
            cregs[i * 3 + 2] = b * 0x01010101UL;
        }
        if (gregs) {
            gregs[i * 3] = r * 0x01010101UL;
            gregs[i * 3 + 1] = g * 0x01010101UL;
            gregs[i * 3 + 2] = b * 0x01010101UL;
        }
    }
}

static BOOL loadAnim(Class *cl, Object *o, struct gifhand *hand)
{
    struct animdata *a = INST_DATA(cl, o);
    STRPTR name = NULL;
    ULONG pixels;
    LONG n, err = ERROR_NO_FREE_STORE;
    BOOL nomem;

    memset(a, 0, sizeof *a);
    InitSemaphore(&a->lock);
    a->c8.shown = a->argb.shown = -1;
    a->file = hand->file;
    a->size = hand->size;
    hand->file = NULL;
    GetDTAttrs(o, DTA_Name, (ULONG)&name, TAG_DONE);
    if ((n = gif_scan(a->file, a->size, &a->info, NULL, 0)) < 2) {
        err = DTERROR_INVALID_DATA;
        goto out;
    }
    pixels = a->info.width * a->info.height;
    if (!(a->frame = AllocVec(sizeof(struct gif_frame) * (ULONG)n, MEMF_ANY)))
        goto out;
    gif_scan(a->file, a->size, &a->info, a->frame, (ULONG)n);
    if (!(a->c8.work = AllocVec(GIF_WORKSIZE, MEMF_ANY)))
        goto out;
    if (oneTable(a, &nomem)) {
        a->c8.remap = a->remaps;
        a->c8.table = a->tables;
        if (!(a->c8.lut8 = big_alloc(pixels)) || (a->info.restores && !(a->c8.saved = big_alloc(pixels))))
            goto out;
    } else if (nomem)
        goto out;
    else {
        cubeTable(a);
        a->argb.work = a->c8.work;
        if (!(a->cube = big_alloc(pixels)) || !(a->argb.argb = big_alloc(pixels * 4))
            || (a->info.restores && !(a->argb.saved = big_alloc(pixels * 4))))
            goto out;
    }
    for (a->depth = 1; (1UL << a->depth) < a->colours; a->depth++)
        ;
    a->tick = chooseTick(a);
    if (!(a->keyFrame = AllocBitMap(a->info.width, a->info.height, a->depth, BMF_CLEAR, NULL)))
        goto out;
    drawFrame(a, 0, a->keyFrame);
    setColours(o, a);
    SetDTAttrs(o, NULL, NULL,
        DTA_ObjName, (ULONG)(name ? FilePart(name) : (STRPTR)"GIF"),
        DTA_NominalHoriz, a->info.width,
        DTA_NominalVert, a->info.height,
        ADTA_Width, a->info.width,
        ADTA_Height, a->info.height,
        ADTA_Depth, a->depth,
        ADTA_Frames, a->info.frames,
        ADTA_FramesPerSecond, 100 / a->tick,
        ADTA_KeyFrame, (ULONG)a->keyFrame,
        TAG_DONE);
    return TRUE;
out:
    hand->file = a->file;             /* given back: the GIF opens as a picture */
    a->file = NULL;
    freeAnim(a);
    SetIoErr(err);
    return FALSE;
}

static ULONG animDispatch(Class *cl, Object *o, Msg msg)
{
    switch (msg->MethodID) {
    case OM_NEW: {
        struct gifhand *hand = (struct gifhand *)GetTagData(GIFA_Hand, 0, ((struct opSet *)msg)->ops_AttrList);
        Object *obj;
        if (!hand || !hand->file)
            return 0;
        if ((obj = (Object *)DoSuperMethodA(cl, o, msg)) && !loadAnim(cl, obj, hand)) {
            LONG err = IoErr();
            CoerceMethod(cl, obj, OM_DISPOSE);
            SetIoErr(err);
            obj = NULL;
        }
        return (ULONG)obj;
    }
    case OM_DISPOSE: {
        /* The superclass stops its loader and player first; what we own
         * is freed after it. */
        struct animdata keep = *(struct animdata *)INST_DATA(cl, o);
        ULONG rc = DoSuperMethodA(cl, o, msg);
        freeAnim(&keep);
        return rc;
    }
    case ADTM_LOADFRAME: {
        /* As cdxl.datatype: animation.datatype 47 hands bitmaps back in
         * alf_BitMap to be drawn again; alf_UserData follows a bitmap and
         * it is freed once, when the frame holding it last unloads. */
        struct animdata *a = INST_DATA(cl, o);
        struct adtFrame *alf = (struct adtFrame *)msg;
        ULONG index = alf->alf_TimeStamp;
        struct BitMap *bm = alf->alf_BitMap;
        if (index >= a->info.frames)
            index = a->info.frames - 1;
        alf->alf_Frame = index;
        alf->alf_Duration = frameTicks(a, index);
        alf->alf_CMap = NULL;
        alf->alf_Sample = NULL;
        alf->alf_SampleLength = 0;
        if (!bm)
            bm = AllocBitMap(a->info.width, a->info.height, a->depth, BMF_CLEAR, NULL);
        if (!bm) {
            SetIoErr(ERROR_NO_FREE_STORE);
            return 0;
        }
        drawFrame(a, index, bm);
        alf->alf_BitMap = bm;
        alf->alf_UserData = bm;
        return 1;
    }
    case ADTM_UNLOADFRAME: {
        struct adtFrame *alf = (struct adtFrame *)msg;
        if (alf->alf_UserData) {
            WaitBlit();
            FreeBitMap((struct BitMap *)alf->alf_UserData);
        }
        alf->alf_UserData = NULL;
        alf->alf_BitMap = NULL;
        return 0;
    }
    default:
        return DoSuperMethodA(cl, o, msg);
    }
}

static ULONG animEntry(REG(a0, Class *cl), REG(a2, Object *o), REG(a1, Msg msg))
{
    return animDispatch(cl, o, msg);
}

/* The animation class, made once; NULL when animation.datatype can't be
 * opened (the GIF then opens as a picture). */
static Class *getAnimClass(void)
{
    Class *c;

    ObtainSemaphore(&animLock);
    if (!animClass) {
        if (!animBase)
            animBase = OpenLibrary((CONST_STRPTR)"datatypes/animation.datatype", 40);
        if (animBase && (animClass = MakeClass(NULL, (ClassID)"animation.datatype", NULL, sizeof(struct animdata), 0)) != NULL)
            animClass->cl_Dispatcher.h_Entry = (typeof(animClass->cl_Dispatcher.h_Entry))animEntry;
    }
    c = animClass;
    ReleaseSemaphore(&animLock);
    return c;
}

/* An animation for the program, when it asked for no group (as MultiView
 * does) and the file has more than one frame: else NULL, and hand keeps
 * the file. */
static Object *newAnimation(struct TagItem *attrs, struct gifhand *hand)
{
    struct gif_info info;
    struct TagItem t[2];
    Class *c;

    if (GetTagData(DTA_GroupID, 0, attrs) || FindTagItem(PDTA_WhichPicture, attrs)
        || gif_scan(hand->file, hand->size, &info, NULL, 0) < 2 || !(c = getAnimClass()))
        return NULL;
    t[0].ti_Tag = GIFA_Hand;
    t[0].ti_Data = (ULONG)hand;
    t[1].ti_Tag = TAG_MORE;
    t[1].ti_Data = (ULONG)attrs;
    return NewObjectA(c, NULL, t);
}

/* OIM_READFRAME: 0, or an IoErr() code. */
static LONG readFrame(struct gifdata *d, struct oiReadFrame *m)
{
    ULONG y, x, w = d->info.width;
    const ULONG *from;

    if (!m->oirf_Pixels)
        return ERROR_REQUIRED_ARG_MISSING;
    if (!d->file || m->oirf_Frame >= d->info.frames)
        return ERROR_OBJECT_NOT_FOUND;
    gif_show(d->file, d->size, &d->info, d->frame, &d->canvas, m->oirf_Frame);
    from = d->canvas.argb;
    /* GIF alpha is 0 or 255 and transparent pixels are 0: the same words
     * premultiplied or not. */
    for (y = 0; y < d->info.height; y++) {
        ULONG *to = (ULONG *)(m->oirf_Pixels + y * m->oirf_Stride);
        for (x = 0; x < w; x++)
            to[x] = from[x];
        from += w;
    }
    m->oirf_Delay = (ULONG)d->frame[m->oirf_Frame].delay * 10;
    m->oirf_HasAlpha = gif_has_alpha(&d->info, &d->canvas) ? TRUE : FALSE;
    return 0;
}

ULONG dt_dispatch(Class *cl, Object *o, Msg msg)
{
    switch (msg->MethodID) {
    case OM_NEW: {
        struct TagItem *attrs = ((struct opSet *)msg)->ops_AttrList;
        Object *obj = (Object *)DoSuperMethodA(cl, o, msg), *anim;
        struct gifhand hand = { NULL, 0 };
        if (obj && GetTagData(DTA_SourceType, DTST_FILE, attrs) == DTST_RAM)
            return (ULONG)obj;        /* no source: a picture to write into */
        /* No group asked for: an animated GIF becomes an animation. */
        if (obj && !GetTagData(DTA_GroupID, 0, attrs) && !FindTagItem(PDTA_WhichPicture, attrs)
            && (hand.file = dt_read_source(obj, &hand.size)) != NULL) {
            if ((anim = newAnimation(attrs, &hand)) != NULL) {
                CoerceMethod(cl, obj, OM_DISPOSE);
                return (ULONG)anim;
            }
        }
        if (obj && !loadGIF(cl, obj, attrs, &hand)) {
            LONG err = IoErr();
            CoerceMethod(cl, obj, OM_DISPOSE);
            SetIoErr(err);
            obj = NULL;
        }
        if (hand.file)
            FreeVec(hand.file);
        return (ULONG)obj;
    }
    case OM_DISPOSE: {
        struct gifdata keep = *(struct gifdata *)INST_DATA(cl, o);
        ULONG rc = DoSuperMethodA(cl, o, msg);
        freeData(&keep);
        return rc;
    }
    case OM_GET: {
        struct opGet *g = (struct opGet *)msg;
        struct gifdata *d = INST_DATA(cl, o);
        ULONG frames = d->file ? d->info.frames : 1;
        switch (g->opg_AttrID) {
        case OIA_Frames:
        case PDTA_GetNumPictures:
            *g->opg_Storage = frames;
            return TRUE;
        case PDTA_WhichPicture:
            *g->opg_Storage = d->file ? d->first : 0;
            return TRUE;
        case OIA_LoopCount:
            *g->opg_Storage = d->file ? (ULONG)d->info.loops : (ULONG)-1;
            return TRUE;
        }
        return DoSuperMethodA(cl, o, msg);
    }
    case OIM_READFRAME: {
        LONG err = readFrame(INST_DATA(cl, o), (struct oiReadFrame *)msg);
        if (err) {
            SetIoErr(err);
            return FALSE;
        }
        return TRUE;
    }
    case PDTM_READPIXELARRAY:
    case PDTM_WRITEPIXELARRAY:
        if (!((struct pdtBlitPixelArray *)msg)->pbpa_PixelData)
            return FALSE;             /* no pixel array (sent with DoDTMethodA?) */
        return DoSuperMethodA(cl, o, msg);
    default:
        return DoSuperMethodA(cl, o, msg);
    }
}
