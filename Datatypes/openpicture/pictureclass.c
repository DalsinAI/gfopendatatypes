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
 * DTM_WRITE in raw mode (47.2) writes HEIC through media.encode/1
 * (common/dtencode.c): the picture's pixels go to the service in strips,
 * its FFmpeg makes the HEVC, and the file comes back. The Team's ruling (6
 * November 2026): HEIC is written by the service's FFmpeg, never by code in
 * the pack. OIA_WriteFormat and OIA_Quality in dtw_AttrList choose
 * (include/datatypes/gfopenimage.h); alpha goes with it when the picture
 * has any. No card or Cradle, or one whose FFmpeg can't: the write fails
 * with ERROR_NOT_IMPLEMENTED. IFF mode is picture.datatype's ILBM. An
 * object made with no source (DTST_RAM) is a picture to write into and
 * save as HEIC: a converter's target.
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

#include <proto/utility.h>

#include "dtlib.h"
#include "dtservice.h"
#include "dtencode.h"
#include "datatypes/gfopenimage.h"

const char LibName[] = "gfopenpicture.datatype";
const char LibIdString[] = "gfopenpicture.datatype 47.2 (7.11.2026) AmiCradle Team, decoded by media.decode/1, HEIC written by media.encode/1";
const UWORD LibVersion = 47;
const UWORD LibRevision = 2;
static const char version[] __attribute__((used)) = "$VER: gfopenpicture.datatype 47.2 (7.11.2026)";

const char dt_superclass[] = "picture.datatype";
const UWORD dt_superversion = 43;

typedef struct {
    ULONG format;                  /* as media.decode/1 named it ('HEIC'); 0 with no source */
} PictureData;

ULONG dt_instsize = sizeof(PictureData);

/* Bytes of pixels sent to media.encode/1 at a time. */
#define STRIP (64UL * 1024)

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
    struct dt_file file = { NULL, 0, 0, { 0 } };
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
    file.data = data;
    file.size = size;
    file.kept = 0;
    buf[1].ob_Data = info;
    buf[1].ob_Length = sizeof info;
    /* The file goes once: kept on the host by PROBE, named by DECODE. */
    if (dt_file_call(&svc, &file, MD_PROBE, 0, 2, buf, extra, &width, &height, FALSE) != OSERR_OK)
        goto out;
    ((PictureData *)INST_DATA(cl, o))->format = get32(info + 4);
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
    if (dt_file_call(&svc, &file, MD_DECODE, 0, 2, buf, extra, &width, &height, TRUE) == OSERR_OK
        && width == w && height == h)
        ok = DoSuperMethod(cl, o, PDTM_WRITEPIXELARRAY, (ULONG)argb, PBPAFMT_ARGB, w * 4, 0, 0, w, h) ? TRUE : FALSE;
    FreeVec(argb);
out:
    dt_file_release(&svc, &file);      /* when DECODE was never reached */
    dt_service_close(&svc);
    FreeVec(data);
    if (!ok)
        SetIoErr(err);
    return ok;
}

/* DTM_WRITE, DTWM_RAW: the picture as HEIC, through media.encode/1. 0, or an IoErr() code. */
static LONG writePicture(Class *cl, Object *o, struct dtWrite *w)
{
    struct BitMapHeader *bmh = NULL;
    struct me_settings s;
    struct dt_encoder enc;
    ULONG alpha = FALSE, sourceType = DTST_FILE, rows, y, n;
    STRPTR name = NULL;
    UBYTE *strip;
    LONG err;

    if (!w->dtw_FileHandle)
        return ERROR_REQUIRED_ARG_MISSING;
    if (GetDTAttrs(o, PDTA_BitMapHeader, (ULONG)&bmh, DTA_Name, (ULONG)&name, TAG_DONE) < 1 || !bmh
        || !bmh->bmh_Width || !bmh->bmh_Height)
        return ERROR_OBJECT_WRONG_TYPE;
    GetDTAttrs(o, DTA_SourceType, (ULONG)&sourceType, TAG_DONE);
    GetDTAttrs(o, PDTA_AlphaChannel, (ULONG)&alpha, TAG_DONE);
    memset(&s, 0, sizeof s);
    s.format = me_pick_format(ME_KIND_PICTURE, GetTagData(OIA_WriteFormat, 0, w->dtw_AttrList),
                              ((PictureData *)INST_DATA(cl, o))->format, dt_name_hint(name), sourceType != DTST_RAM);
    if (!s.format)
        return ERROR_NOT_IMPLEMENTED;          /* AVIF, JPEG XL...: not written by the service (yet) */
    s.width = bmh->bmh_Width;
    s.height = bmh->bmh_Height;
    s.pixels = ME_PIX_ARGB;
    s.quality = me_quality((LONG)GetTagData(OIA_Quality, (ULONG)-1, w->dtw_AttrList));
    s.flags = alpha ? ME_F_ALPHA : 0;
    rows = me_strip_rows(s.width, 4, s.height, STRIP);
    if (!(strip = AllocVec(rows * s.width * 4, MEMF_ANY)))
        return ERROR_NO_FREE_STORE;
    if (!(err = dt_encode_begin(&enc, &s))) {
        for (y = 0; !err && y < s.height; y += n) {
            n = s.height - y < rows ? s.height - y : rows;
            if (!DoSuperMethod(cl, o, PDTM_READPIXELARRAY, (ULONG)strip, PBPAFMT_ARGB, s.width * 4, 0, y, s.width, n))
                err = ERROR_OBJECT_WRONG_TYPE;
            else
                err = dt_encode_put(&enc, ME_STREAM_PICTURE, strip, n * s.width * 4);
        }
        if (!err)
            err = dt_encode_finish(&enc, w->dtw_FileHandle);
    }
    dt_encode_end(&enc);
    FreeVec(strip);
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
        if ((err = writePicture(cl, o, w))) {
            SetIoErr(err);
            return FALSE;
        }
        return TRUE;
    }
    case OM_NEW: {
        Object *obj = (Object *)DoSuperMethodA(cl, o, msg);
        if (obj)
            ((PictureData *)INST_DATA(cl, obj))->format = 0;
        if (obj && GetTagData(DTA_SourceType, DTST_FILE, ((struct opSet *)msg)->ops_AttrList) == DTST_RAM)
            return (ULONG)obj;         /* no source: a picture to write into, and save */
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
