/*
 * gfopenpng.datatype: PNG pictures for AmigaOS 3.x, a picture.datatype (V43
 * mode) subclass that decodes on the Amiga itself (pngdecode.c, our own
 * inflate; no zlib or libpng). Every PNG colour type, bit depth,
 * transparency and Adam7 interlacing. The pixels go to picture.datatype as
 * 32-bit ARGB (bmh_Depth 32), so a program reading them back as ARGB gets
 * them as they were decoded, alpha included. They go in strips of about 32
 * rows (47.7), converted from the inflated rows as they go, so the picture
 * is not held whole a second time.
 *
 * A read or write of the pixels with no pixel array (pbpa_PixelData NULL)
 * is refused here rather than passed on. That is what a program gets when it
 * sends PDTM_READPIXELARRAY with DoDTMethodA() instead of DoMethodA():
 * DoDTMethodA puts a GadgetInfo (NULL with no window) in the message's
 * second longword, which for this method is the pixel array, and
 * picture.datatype then writes the picture from address 0 up through Chip
 * RAM. GFOpenBench 0.1 and a test tool did that on AmiCradle's Instance-32
 * (10 and 11 October 2026); this class answers FALSE instead.
 *
 * PNG out (47.4): OIM_ENCODEPNG (datatypes/gfopenimage.h) encodes ARGB pixels,
 * the program's or the object's own, to a PNG file in memory, and DTM_WRITE
 * with DTWM_RAW writes the picture as PNG to a file (pngencode.c, deflate
 * by z.library, opened for each call); OIA_Compression in its dtw_AttrList
 * (47.5) sets the deflate level, 0 to store. An object made with no source
 * (DTST_RAM) loads nothing, so a program can make one just to encode with.
 *
 * DEVS:DataTypes/GFOpenPNG (build.sh) matches the PNG signature and the IHDR
 * that must follow it: a longer mask than the system's PNG descriptor, which
 * AmigaOS 3.2.3's datatypes.library tries first, so this class takes PNGs
 * while the system's png.datatype stays installed and untouched. Removing
 * the descriptor gives PNGs back to it.
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
#include "pngdecode.h"
#include "pngencode.h"
#include "zlibrary.h"

const char LibName[] = "gfopenpng.datatype";
const char LibIdString[] = "gfopenpng.datatype 47.7 (7.11.2026) AmiCradle Team";
const UWORD LibVersion = 47;
const UWORD LibRevision = 7;
static const char version[] __attribute__((used)) = "$VER: gfopenpng.datatype 47.7 (7.11.2026)";

const char dt_superclass[] = "picture.datatype";
const UWORD dt_superversion = 43;
ULONG dt_instsize = 0;

BOOL dt_init(void)
{
    return TRUE;
}

void dt_cleanup(void)
{
}

/* Fast RAM when there is any: the decode never needs Chip RAM (and a job on
 * another core may not touch it). */
static APTR big_alloc(ULONG n)
{
    APTR p = AllocVec(n, MEMF_FAST);
    return p ? p : AllocVec(n, MEMF_ANY);
}

/* Rows in a strip handed to picture.datatype: about 32, so the picture is
 * never held whole twice, in at most 256 KB. */
static ULONG strip_rows(ULONG width)
{
    ULONG rows = (256UL * 1024) / (width * 4);
    return rows > 32 ? 32 : rows ? rows : 1;
}

static BOOL loadPNG(Class *cl, Object *o)
{
    struct BitMapHeader *bmh = NULL;
    struct png_head *h;
    struct png_rows rows;
    STRPTR name = NULL;
    UBYTE *file, *idat = NULL, *raw = NULL;
    ULONG *argb = NULL, size = 0;
    APTR work = NULL;
    LONG err = ERROR_NO_FREE_STORE;
    BOOL ok = FALSE;

    if (GetDTAttrs(o, PDTA_BitMapHeader, (ULONG)&bmh, DTA_Name, (ULONG)&name, TAG_DONE) < 1 || !bmh) {
        SetIoErr(ERROR_OBJECT_WRONG_TYPE);
        return FALSE;
    }
    if (!(h = AllocVec(sizeof(*h), MEMF_ANY))) {
        SetIoErr(ERROR_NO_FREE_STORE);
        return FALSE;
    }
    if (!(file = dt_read_source(o, &size))) {
        err = IoErr();
        FreeVec(h);
        SetIoErr(err);
        return FALSE;
    }
    if (png_read_head(file, size, h)) {
        FreeVec(file);
        FreeVec(h);
        SetIoErr(DTERROR_INVALID_DATA);
        return FALSE;
    }
    if ((idat = big_alloc(h->idat_len)))
        png_gather(file, size, idat);
    FreeVec(file);
    if (idat && (raw = big_alloc(h->raw_len)) && (work = AllocVec(PNG_WORKSIZE, MEMF_ANY))) {
        if (png_unpack(h, idat, raw, work, &rows) == 0)
            ok = TRUE;
        else
            err = DTERROR_INVALID_DATA;
    }
    if (work) FreeVec(work);
    if (idat) FreeVec(idat);

    /* The rows go to picture.datatype a strip at a time as they are
     * converted: the picture is held once, in picture.datatype. */
    if (ok && !(argb = big_alloc(h->width * 4 * strip_rows(h->width))))
        ok = FALSE;
    if (ok) {
        ULONG y, n, most = strip_rows(h->width);
        bmh->bmh_Width = bmh->bmh_PageWidth = (UWORD)h->width;
        bmh->bmh_Height = bmh->bmh_PageHeight = (UWORD)h->height;
        bmh->bmh_Depth = 32;           /* ARGB, alpha or not */
        bmh->bmh_Masking = h->alpha ? mskHasAlpha : mskNone;
        SetDTAttrs(o, NULL, NULL,
            DTA_ObjName, (ULONG)(name ? FilePart(name) : (STRPTR)"PNG"),
            DTA_NominalHoriz, h->width,
            DTA_NominalVert, h->height,
            PDTA_SourceMode, PMODE_V43,
            PDTA_AlphaChannel, h->alpha ? TRUE : FALSE,
            TAG_DONE);
        for (y = 0; y < h->height && ok; y += n) {
            n = h->height - y < most ? h->height - y : most;
            png_strip(h, raw, &rows, argb, n);
            ok = DoSuperMethod(cl, o, PDTM_WRITEPIXELARRAY, (ULONG)argb, PBPAFMT_ARGB, h->width * 4,
                0, y, h->width, n) ? TRUE : FALSE;
        }
        if (!ok)
            err = ERROR_NO_FREE_STORE;
    }
    if (argb) FreeVec(argb);
    if (raw) FreeVec(raw);
    FreeVec(h);
    if (!ok)
        SetIoErr(err);
    return ok;
}

static void *e_alloc(struct pnge_env *e, ULONG n)
{
    (void)e;
    return big_alloc(n);
}

static void e_free(struct pnge_env *e, void *p)
{
    (void)e;
    FreeVec(p);
}

/* OIM_ENCODEPNG's work: the pixels given, or the object's own read back as
 * ARGB. 0, or an IoErr() code. */
static LONG encodePNG(Class *cl, Object *o, struct oiEncodePNG *m)
{
    struct pnge_env env;
    struct Library *zbase;
    const UBYTE *px = m->oiep_Pixels;
    ULONG w = m->oiep_Width, h = m->oiep_Height, stride = m->oiep_Stride;
    UBYTE *own = NULL;
    LONG err;

    m->oiep_Data = NULL;
    m->oiep_Size = 0;
    if (!px) {
        struct BitMapHeader *bmh = NULL;
        if (GetDTAttrs(o, PDTA_BitMapHeader, (ULONG)&bmh, TAG_DONE) < 1 || !bmh
            || !bmh->bmh_Width || !bmh->bmh_Height)
            return ERROR_OBJECT_WRONG_TYPE;
        w = bmh->bmh_Width;
        h = bmh->bmh_Height;
        stride = w * 4;
        if (!(own = big_alloc(stride * h)))
            return ERROR_NO_FREE_STORE;
        if (!DoSuperMethod(cl, o, PDTM_READPIXELARRAY, (ULONG)own, PBPAFMT_ARGB, stride, 0, 0, w, h)) {
            FreeVec(own);
            return ERROR_OBJECT_WRONG_TYPE;
        }
        px = own;
    }
    if (!(zbase = zl_Open())) {
        if (own)
            FreeVec(own);
        return ERROR_OBJECT_NOT_FOUND;
    }
    env.alloc = e_alloc;
    env.free = e_free;
    err = png_encode(zbase, &env, px, w, h, stride, m->oiep_Flags, m->oiep_Level, &m->oiep_Data, &m->oiep_Size);
    CloseLibrary(zbase);
    if (own)
        FreeVec(own);
    return err;
}

ULONG dt_dispatch(Class *cl, Object *o, Msg msg)
{
    switch (msg->MethodID) {
    case OIM_ENCODEPNG: {
        struct oiEncodePNG *m = (struct oiEncodePNG *)msg;
        if ((m->oiep_Error = encodePNG(cl, o, m))) {
            SetIoErr(m->oiep_Error);
            return FALSE;
        }
        return TRUE;
    }
    case DTM_WRITE: {
        struct dtWrite *w = (struct dtWrite *)msg;
        struct oiEncodePNG m;
        LONG err;
        if (w->dtw_Mode != DTWM_RAW)
            return DoSuperMethodA(cl, o, msg);   /* IFF: picture.datatype's ILBM */
        m.MethodID = OIM_ENCODEPNG;
        m.oiep_GInfo = NULL;
        m.oiep_Pixels = NULL;
        m.oiep_Width = m.oiep_Height = m.oiep_Stride = 0;
        m.oiep_Flags = 0;
        m.oiep_Level = (LONG)GetTagData(OIA_Compression, (ULONG)-1, w->dtw_AttrList);
        {
            ULONG alpha = TRUE;        /* a picture without alpha is written as RGB */
            if (GetDTAttrs(o, PDTA_AlphaChannel, (ULONG)&alpha, TAG_DONE) == 1 && !alpha)
                m.oiep_Flags = OIEF_OPAQUE;
        }
        if (!w->dtw_FileHandle)
            err = ERROR_REQUIRED_ARG_MISSING;
        else if (!(err = encodePNG(cl, o, &m))) {
            if (Write(w->dtw_FileHandle, m.oiep_Data, m.oiep_Size) != (LONG)m.oiep_Size)
                err = IoErr();
            FreeVec(m.oiep_Data);
        }
        if (err) {
            SetIoErr(err);
            return FALSE;
        }
        return TRUE;
    }
    case OM_NEW: {
        Object *obj = (Object *)DoSuperMethodA(cl, o, msg);
        if (obj && GetTagData(DTA_SourceType, DTST_FILE, ((struct opSet *)msg)->ops_AttrList) == DTST_RAM)
            return (ULONG)obj;         /* no source: a picture to write into, or to encode with */
        if (obj && !loadPNG(cl, obj)) {
            LONG err = IoErr();
            CoerceMethod(cl, obj, OM_DISPOSE);
            SetIoErr(err);
            obj = NULL;
        }
        return (ULONG)obj;
    }
    case PDTM_READPIXELARRAY:
    case PDTM_WRITEPIXELARRAY:
        if (!((struct pdtBlitPixelArray *)msg)->pbpa_PixelData)
            return FALSE;              /* no pixel array (sent with DoDTMethodA?): see the top */
        return DoSuperMethodA(cl, o, msg);
    default:
        return DoSuperMethodA(cl, o, msg);
    }
}
