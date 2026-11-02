/*
 * gfopenpicture.datatype: modern pictures for AmigaOS 3.x, a picture.datatype
 * (V43 mode) subclass: AVIF, HEIC/HEIF, JPEG XL, camera RAW, PSD, XCF,
 * OpenEXR, Radiance HDR, QOI, DDS and JPEG 2000. The pictures are decoded
 * by the media.decode/1 service (DalsinAI/gfopenservice
 * docs/MEDIA_DECODE.md) on the services card or a paired Cradle, which
 * sends back 32-bit ARGB, scaled down to fit ENV:GFOpenImage/MaxSide (default
 * 4096). The file's extension goes with it as a hint (camera RAW is TIFF
 * inside). A sequence shows its first picture.
 *
 * With neither a card nor a Cradle the picture does not open: AVIF needs
 * AV1, HEIC needs HEVC and RAW needs demosaicing, too slow for a 68k at
 * these sizes.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <string.h>
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
#include "dtservice.h"

const char LibName[] = "gfopenpicture.datatype";
const char LibIdString[] = "gfopenpicture.datatype 47.1 (5.10.2026) AmiCradle Team, decoded by media.decode/1";
const UWORD LibVersion = 47;
const UWORD LibRevision = 1;
static const char version[] __attribute__((used)) = "$VER: gfopenpicture.datatype 47.1 (5.10.2026)";

const char dt_superclass[] = "picture.datatype";
const UWORD dt_superversion = 43;
ULONG dt_instsize = 0;

/* media.decode/1 (docs/MEDIA_DECODE.md) */
#define MD_PROBE  1
#define MD_DECODE 2
#define MD_FLAG_ALPHA 1

BOOL dt_init(void)
{
    return TRUE;
}

void dt_cleanup(void)
{
}

static ULONG get32(const UBYTE *p)
{
    return (ULONG)p[0] << 24 | (ULONG)p[1] << 16 | (ULONG)p[2] << 8 | p[3];
}

static BOOL loadPicture(Class *cl, Object *o)
{
    struct BitMapHeader *bmh = NULL;
    struct dt_service svc;
    struct OSBuffer buf[4];
    ULONG extra[4], size = 0, width, height, w, h, flags;
    UBYTE info[24], *data, *argb;
    STRPTR name = NULL;
    BOOL ok = FALSE;
    LONG err = DTERROR_INVALID_DATA;

    if (GetDTAttrs(o, PDTA_BitMapHeader, (ULONG)&bmh, DTA_Name, (ULONG)&name, TAG_DONE) < 1 || !bmh) {
        SetIoErr(ERROR_OBJECT_WRONG_TYPE);
        return FALSE;
    }
    if (!(data = dt_read_source(o, &size)))
        return FALSE;
    if (!dt_service_open(&svc, "media.decode/1")) {
        FreeVec(data);
        SetIoErr(ERROR_NOT_IMPLEMENTED);       /* no card and no paired Cradle */
        return FALSE;
    }

    memset(buf, 0, sizeof buf);
    extra[0] = extra[1] = dt_max_side();
    extra[2] = dt_name_hint(name);
    extra[3] = 0;
    buf[0].ob_Data = data;
    buf[0].ob_Length = size;
    buf[1].ob_Data = info;
    buf[1].ob_Length = sizeof info;
    if (dt_service_call(&svc, MD_PROBE, 0, 2, buf, extra, &width, &height) != OSERR_OK)
        goto out;
    flags = get32(info + 8);
    w = get32(info + 16);
    h = get32(info + 20);
    if (!w || !h || w > 32767 || h > 32767)
        goto out;

    bmh->bmh_Width = bmh->bmh_PageWidth = w;
    bmh->bmh_Height = bmh->bmh_PageHeight = h;
    bmh->bmh_Depth = (flags & MD_FLAG_ALPHA) ? 32 : 24;
    bmh->bmh_Masking = (flags & MD_FLAG_ALPHA) ? mskHasAlpha : mskNone;
    SetDTAttrs(o, NULL, NULL,
        DTA_ObjName, (ULONG)(name ? FilePart(name) : (STRPTR)"Picture"),
        DTA_NominalHoriz, w,
        DTA_NominalVert, h,
        PDTA_SourceMode, PMODE_V43,
        PDTA_AlphaChannel, (flags & MD_FLAG_ALPHA) ? TRUE : FALSE,
        TAG_DONE);

    if (!(argb = AllocVec(w * h * 4, MEMF_ANY))) {
        err = ERROR_NO_FREE_STORE;
        goto out;
    }
    buf[1].ob_Data = argb;
    buf[1].ob_Length = w * h * 4;
    if (dt_service_call(&svc, MD_DECODE, 0, 2, buf, extra, &width, &height) == OSERR_OK && width == w && height == h)
        ok = DoSuperMethod(cl, o, PDTM_WRITEPIXELARRAY, (ULONG)argb, PBPAFMT_ARGB, w * 4, 0, 0, w, h) ? TRUE : FALSE;
    FreeVec(argb);
out:
    dt_service_close(&svc);
    FreeVec(data);
    if (!ok)
        SetIoErr(err);
    return ok;
}

ULONG dt_dispatch(Class *cl, Object *o, Msg msg)
{
    switch (msg->MethodID) {
    case OM_NEW: {
        Object *obj = (Object *)DoSuperMethodA(cl, o, msg);
        if (obj && !loadPicture(cl, obj)) {
            LONG err = IoErr();
            CoerceMethod(cl, obj, OM_DISPOSE);
            SetIoErr(err);
            obj = NULL;
        }
        return (ULONG)obj;
    }
    default:
        return DoSuperMethodA(cl, o, msg);
    }
}
