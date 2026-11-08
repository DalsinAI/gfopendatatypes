/*
 * gfopentga.datatype: Targa (TGA) pictures for AmigaOS 3.x, a picture.datatype
 * (V43 mode) subclass that decodes on the Amiga itself (tgadecode.c; no
 * other library). Uncompressed and run-length encoded; colour-mapped,
 * 15/16, 24 and 32-bit true-colour with alpha, and greyscale; rows from the
 * bottom or the top. The pixels go to picture.datatype as 32-bit ARGB
 * (bmh_Depth 32), as gfopenpng.datatype's do, so a program reading them back
 * as ARGB gets them as they were decoded, alpha included. They go in strips
 * of about 32 rows as they are decoded (47.3), so the picture is not held
 * whole a second time.
 *
 * DTM_WRITE in raw mode writes the picture as a TGA (tgaencode.c): 32 bits
 * with alpha when it has any, else 24, run-length encoded unless
 * OIA_Compression is 0 in dtw_AttrList (include/datatypes/gfopenimage.h).
 * IFF mode is picture.datatype's ILBM. An object made with no source
 * (DTST_RAM) is a picture to write into and save: a converter's target.
 *
 * A read or write of the pixels with no pixel array (pbpa_PixelData NULL)
 * is refused here rather than passed on, as in gfopenpng.datatype: that is
 * what PDTM_READPIXELARRAY sent with DoDTMethodA() instead of DoMethodA()
 * arrives as, and picture.datatype would write the picture from address 0.
 *
 * A TGA file has no signature at its start, so DEVS:DataTypes/GFOpenTGA
 * (build.sh) goes by the name: #?.tga, and the other names Truevision gave
 * (.targa, .icb, .vda, .vst). A descriptor with no mask is tried after every
 * one with a mask (Datatypes/openmodule/DESIGN.md), so a file of another
 * kind that happens to be called .tga still opens as what it is when its own
 * datatype knows it, and one that is not a TGA fails as invalid data.
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
#include <clib/alib_protos.h>

#include "dtlib.h"
#include "tgadecode.h"
#include "tgaencode.h"
#include <datatypes/gfopenimage.h>
#include <proto/utility.h>

const char LibName[] = "gfopentga.datatype";
const char LibIdString[] = "gfopentga.datatype 47.3 (7.11.2026) AmiCradle Team";
const UWORD LibVersion = 47;
const UWORD LibRevision = 3;
static const char version[] __attribute__((used)) = "$VER: gfopentga.datatype 47.3 (7.11.2026)";

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

/* Fast RAM when there is any: the decode never needs Chip RAM. */
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

/* Every row through tga_strip() to picture.datatype, a strip at a time
 * (opaque: the second time, the alpha having come out empty). */
static BOOL hand_over(Class *cl, Object *o, const struct tga_head *h, const UBYTE *file, ULONG size, ULONG *pal,
                      ULONG *argb, struct tga_rows *r, UBYTE opaque)
{
    ULONG y, n, most = strip_rows(h->width);
    if (tga_start(h, file, size, pal, r, opaque))
        return FALSE;
    for (y = 0; y < h->height; y += n) {
        n = h->height - y < most ? h->height - y : most;
        tga_strip(h, file, size, pal, r, argb, n);
        if (!DoSuperMethod(cl, o, PDTM_WRITEPIXELARRAY, (ULONG)argb, PBPAFMT_ARGB, h->width * 4,
                0, h->top_down ? y : h->height - y - n, h->width, n))
            return FALSE;
    }
    return TRUE;
}

static BOOL loadTGA(Class *cl, Object *o)
{
    struct BitMapHeader *bmh = NULL;
    struct tga_head h;
    struct tga_rows rows;
    STRPTR name = NULL;
    UBYTE *file;
    ULONG *argb = NULL, *pal = NULL, size = 0;
    LONG err = ERROR_NO_FREE_STORE;
    BOOL ok = FALSE;

    if (GetDTAttrs(o, PDTA_BitMapHeader, (ULONG)&bmh, DTA_Name, (ULONG)&name, TAG_DONE) < 1 || !bmh) {
        SetIoErr(ERROR_OBJECT_WRONG_TYPE);
        return FALSE;
    }
    if (!(file = dt_read_source(o, &size)))
        return FALSE;                    /* IoErr() says why */
    if (tga_read_head(file, size, &h)) {
        FreeVec(file);
        SetIoErr(DTERROR_INVALID_DATA);
        return FALSE;
    }
    /* The rows go to picture.datatype a strip at a time as they are
     * decoded: the picture is held once, in picture.datatype. */
    if ((argb = big_alloc(h.width * 4 * strip_rows(h.width)))
        && (h.type != 1 || (pal = AllocVec((ULONG)h.map_len * 4, MEMF_ANY)))) {
        if (tga_start(&h, file, size, pal, &rows, 0) == 0)
            ok = TRUE;
        else
            err = DTERROR_INVALID_DATA;
    }
    if (ok) {
        bmh->bmh_Width = bmh->bmh_PageWidth = (UWORD)h.width;
        bmh->bmh_Height = bmh->bmh_PageHeight = (UWORD)h.height;
        bmh->bmh_Depth = 32;             /* ARGB, alpha or not */
        bmh->bmh_Masking = h.alpha ? mskHasAlpha : mskNone;
        SetDTAttrs(o, NULL, NULL,
            DTA_ObjName, (ULONG)(name ? FilePart(name) : (STRPTR)"TGA"),
            DTA_NominalHoriz, h.width,
            DTA_NominalVert, h.height,
            PDTA_SourceMode, PMODE_V43,
            PDTA_AlphaChannel, h.alpha ? TRUE : FALSE,
            TAG_DONE);
        ok = hand_over(cl, o, &h, file, size, pal, argb, &rows, 0);
        if (ok && h.alpha && !(rows.seen & 0xFF000000UL)) {
            /* alpha 0 everywhere: an empty channel, not a clear picture.
             * Rare (some writers' 32-bit pictures): the rows again, opaque. */
            bmh->bmh_Masking = mskNone;
            SetDTAttrs(o, NULL, NULL, PDTA_AlphaChannel, FALSE, TAG_DONE);
            ok = hand_over(cl, o, &h, file, size, pal, argb, &rows, 1);
        }
        if (!ok)
            err = ERROR_NO_FREE_STORE;
    }
    if (pal) FreeVec(pal);
    if (argb) FreeVec(argb);
    FreeVec(file);
    if (!ok)
        SetIoErr(err);
    return ok;
}

static void *e_alloc(struct tgae_env *e, ULONG n)
{
    (void)e;
    return big_alloc(n);
}

static void e_free(struct tgae_env *e, void *p)
{
    (void)e;
    FreeVec(p);
}

/* DTM_WRITE, DTWM_RAW: the picture as a TGA. 0, or an IoErr() code. */
static LONG writeTGA(Class *cl, Object *o, struct dtWrite *w)
{
    struct BitMapHeader *bmh = NULL;
    struct tgae_env env;
    ULONG alpha = TRUE, width, height, size = 0, flags = 0;
    UBYTE *px, *tga = NULL;
    LONG err;

    if (!w->dtw_FileHandle)
        return ERROR_REQUIRED_ARG_MISSING;
    if (GetDTAttrs(o, PDTA_BitMapHeader, (ULONG)&bmh, TAG_DONE) < 1 || !bmh
        || !bmh->bmh_Width || !bmh->bmh_Height)
        return ERROR_OBJECT_WRONG_TYPE;
    width = bmh->bmh_Width;
    height = bmh->bmh_Height;
    if (GetDTAttrs(o, PDTA_AlphaChannel, (ULONG)&alpha, TAG_DONE) == 1 && !alpha)
        flags |= TGAE_OPAQUE;             /* a picture without alpha is written as 24 bits */
    if ((LONG)GetTagData(OIA_Compression, (ULONG)-1, w->dtw_AttrList) == 0)
        flags |= TGAE_RAW;
    if (!(px = big_alloc(width * height * 4)))
        return ERROR_NO_FREE_STORE;
    if (!DoSuperMethod(cl, o, PDTM_READPIXELARRAY, (ULONG)px, PBPAFMT_ARGB, width * 4, 0, 0, width, height)) {
        FreeVec(px);
        return ERROR_OBJECT_WRONG_TYPE;
    }
    env.alloc = e_alloc;
    env.free = e_free;
    err = tga_encode(&env, px, width, height, width * 4, flags, &tga, &size);
    FreeVec(px);
    if (!err) {
        if (Write(w->dtw_FileHandle, tga, size) != (LONG)size)
            err = IoErr() ? IoErr() : ERROR_DISK_FULL;
        FreeVec(tga);
    }
    return err;
}

ULONG dt_dispatch(Class *cl, Object *o, Msg msg)
{
    switch (msg->MethodID) {
    case DTM_WRITE: {
        struct dtWrite *w = (struct dtWrite *)msg;
        LONG err;
        if (w->dtw_Mode != DTWM_RAW)
            return DoSuperMethodA(cl, o, msg);   /* IFF: picture.datatype's ILBM */
        if ((err = writeTGA(cl, o, w))) {
            SetIoErr(err);
            return FALSE;
        }
        return TRUE;
    }
    case OM_NEW: {
        Object *obj = (Object *)DoSuperMethodA(cl, o, msg);
        if (obj && GetTagData(DTA_SourceType, DTST_FILE, ((struct opSet *)msg)->ops_AttrList) == DTST_RAM)
            return (ULONG)obj;         /* no source: a picture to write into, and save */
        if (obj && !loadTGA(cl, obj)) {
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
