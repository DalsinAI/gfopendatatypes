/*
 * gfopentga.datatype: Targa (TGA) pictures for AmigaOS 3.x, a picture.datatype
 * (V43 mode) subclass that decodes on the Amiga itself (tgadecode.c; no
 * other library). Uncompressed and run-length encoded; colour-mapped,
 * 15/16, 24 and 32-bit true-colour with alpha, and greyscale; rows from the
 * bottom or the top. The pixels go to picture.datatype as 32-bit ARGB
 * (bmh_Depth 32), as gfopenpng.datatype's do, so a program reading them back
 * as ARGB gets them as they were decoded, alpha included.
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

const char LibName[] = "gfopentga.datatype";
const char LibIdString[] = "gfopentga.datatype 47.1 (12.10.2026) AmiCradle Team";
const UWORD LibVersion = 47;
const UWORD LibRevision = 1;
static const char version[] __attribute__((used)) = "$VER: gfopentga.datatype 47.1 (12.10.2026)";

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

static BOOL loadTGA(Class *cl, Object *o)
{
    struct BitMapHeader *bmh = NULL;
    struct tga_head h;
    STRPTR name = NULL;
    UBYTE *file;
    ULONG *argb = NULL, *pal = NULL, size = 0;
    UBYTE alpha = 0;
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
    if ((argb = big_alloc(h.width * h.height * 4)) && (h.type != 1 || (pal = AllocVec((ULONG)h.map_len * 4, MEMF_ANY)))) {
        if (tga_decode(&h, file, size, argb, pal, &alpha) == 0)
            ok = TRUE;
        else
            err = DTERROR_INVALID_DATA;
    }
    if (pal) FreeVec(pal);
    FreeVec(file);

    if (ok) {
        bmh->bmh_Width = bmh->bmh_PageWidth = (UWORD)h.width;
        bmh->bmh_Height = bmh->bmh_PageHeight = (UWORD)h.height;
        bmh->bmh_Depth = 32;             /* ARGB, alpha or not */
        bmh->bmh_Masking = alpha ? mskHasAlpha : mskNone;
        SetDTAttrs(o, NULL, NULL,
            DTA_ObjName, (ULONG)(name ? FilePart(name) : (STRPTR)"TGA"),
            DTA_NominalHoriz, h.width,
            DTA_NominalVert, h.height,
            PDTA_SourceMode, PMODE_V43,
            PDTA_AlphaChannel, alpha ? TRUE : FALSE,
            TAG_DONE);
        ok = DoSuperMethod(cl, o, PDTM_WRITEPIXELARRAY, (ULONG)argb, PBPAFMT_ARGB, h.width * 4,
            0, 0, h.width, h.height) ? TRUE : FALSE;
        if (!ok)
            err = ERROR_NO_FREE_STORE;
    }
    if (argb) FreeVec(argb);
    if (!ok)
        SetIoErr(err);
    return ok;
}

ULONG dt_dispatch(Class *cl, Object *o, Msg msg)
{
    switch (msg->MethodID) {
    case OM_NEW: {
        Object *obj = (Object *)DoSuperMethodA(cl, o, msg);
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
