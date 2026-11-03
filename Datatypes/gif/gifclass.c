/*
 * gfopengif.datatype: GIF pictures and animations for AmigaOS 3.x, a
 * picture.datatype (V43 mode) subclass that decodes on the Amiga itself
 * (gifdecode.c, our own LZW; no other library). The picture goes to
 * picture.datatype as 32-bit ARGB (bmh_Depth 32) with alpha where the GIF is
 * transparent, so a program reading it back as ARGB gets it as decoded.
 *
 * Every frame of an animation is there, composited as web browsers show
 * them (gifdecode.h):
 *
 * - The object's picture is frame 0, or the frame PDTA_WhichPicture names
 *   at OM_NEW (the V44 way to choose a picture in a file of several);
 *   PDTA_GetNumPictures and OIA_Frames give the number of frames.
 * - OIM_READFRAME (datatypes/gfopenimage.h) writes any frame into a
 *   program's pixels, with its delay; OIA_LoopCount gives the loop count.
 *   GFOpenBrowser plays animated GIFs this way.
 *
 * A one-frame GIF frees the file once it is decoded; an animation keeps it,
 * with the screen, while the object lives (OIM_READFRAME decodes from it).
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
#include <exec/memory.h>
#include <dos/dos.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <intuition/classes.h>
#include <proto/exec.h>
#include <proto/dos.h>
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
const char LibIdString[] = "gfopengif.datatype 47.1 (3.11.2026) AmiCradle Team";
const UWORD LibVersion = 47;
const UWORD LibRevision = 1;
static const char version[] __attribute__((used)) = "$VER: gfopengif.datatype 47.1 (3.11.2026)";

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

BOOL dt_init(void)
{
    return TRUE;
}

void dt_cleanup(void)
{
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

static BOOL loadGIF(Class *cl, Object *o, struct TagItem *attrs)
{
    struct gifdata *d = INST_DATA(cl, o);
    struct BitMapHeader *bmh = NULL;
    STRPTR name = NULL;
    ULONG pixels, which;
    LONG n, err = ERROR_NO_FREE_STORE;
    BOOL ok = FALSE, alpha;

    if (GetDTAttrs(o, PDTA_BitMapHeader, (ULONG)&bmh, DTA_Name, (ULONG)&name, TAG_DONE) < 1 || !bmh) {
        SetIoErr(ERROR_OBJECT_WRONG_TYPE);
        return FALSE;
    }
    if (!(d->file = dt_read_source(o, &d->size)))
        return FALSE;                 /* IoErr() says why */
    if ((n = gif_scan(d->file, d->size, &d->info, NULL, 0)) < 0) {
        freeData(d);
        SetIoErr(DTERROR_INVALID_DATA);
        return FALSE;
    }
    pixels = d->info.width * d->info.height;
    d->canvas.shown = -1;
    if ((d->frame = AllocVec(sizeof(struct gif_frame) * (ULONG)n, MEMF_ANY))
        && (d->canvas.argb = big_alloc(pixels * 4))
        && (!d->info.restores || (d->canvas.saved = big_alloc(pixels * 4)))
        && (d->canvas.work = AllocVec(GIF_WORKSIZE, MEMF_ANY))) {
        gif_scan(d->file, d->size, &d->info, d->frame, (ULONG)n);
        which = GetTagData(PDTA_WhichPicture, 0, attrs);
        if (which >= d->info.frames)
            which = d->info.frames - 1;
        d->first = which;
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
    if (!ok || d->info.frames == 1)
        freeData(d);                  /* a still: nothing more to read */
    if (!ok)
        SetIoErr(err);
    return ok;
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
        Object *obj = (Object *)DoSuperMethodA(cl, o, msg);
        if (obj && GetTagData(DTA_SourceType, DTST_FILE, ((struct opSet *)msg)->ops_AttrList) == DTST_RAM)
            return (ULONG)obj;        /* no source: a picture to write into */
        if (obj && !loadGIF(cl, obj, ((struct opSet *)msg)->ops_AttrList)) {
            LONG err = IoErr();
            CoerceMethod(cl, obj, OM_DISPOSE);
            SetIoErr(err);
            obj = NULL;
        }
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
