/*
 * webp.datatype: WebP pictures for AmigaOS 3.x, a picture.datatype (V43
 * mode) subclass built on libwebp. Lossy, lossless and alpha pictures load
 * as 32-bit ARGB; an animated WebP shows its first frame.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. libwebp is Google's, under its
 * BSD licence (upstream/libwebp/COPYING).
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
#include "webp/decode.h"
#include "webp/demux.h"

const char LibName[] = "webp.datatype";
const char LibIdString[] = "webp.datatype 47.1 (4.10.2026) AmiCradle Team, libwebp " "1.6.0";
const UWORD LibVersion = 47;
const UWORD LibRevision = 1;
static const char version[] __attribute__((used)) = "$VER: webp.datatype 47.1 (4.10.2026)";

const char dt_superclass[] = "picture.datatype";
const UWORD dt_superversion = 43;
ULONG dt_instsize = 0;

BOOL dt_init(void)
{
    /* libwebp picks its DSP functions on first use; doing it here, while
       the library loads, keeps that out of concurrent decodes. */
    WebPDecoderConfig config;
    return WebPInitDecoderConfig(&config) ? TRUE : FALSE;
}

void dt_cleanup(void)
{
}

/* Writes the first frame of an animated WebP at its place on the canvas. */
static BOOL writeFirstFrame(Class *cl, Object *o, const UBYTE *data, ULONG size)
{
    WebPData webpData = { data, size };
    WebPDemuxer *demux = WebPDemux(&webpData);
    WebPIterator iter;
    BOOL ok = FALSE;

    if (!demux)
        return FALSE;
    if (WebPDemuxGetFrame(demux, 1, &iter)) {
        int fw = 0, fh = 0;
        UBYTE *frame = WebPDecodeARGB(iter.fragment.bytes, iter.fragment.size, &fw, &fh);
        if (frame) {
            ok = DoSuperMethod(cl, o, PDTM_WRITEPIXELARRAY, (ULONG)frame, PBPAFMT_ARGB, fw * 4,
                iter.x_offset, iter.y_offset, fw, fh) ? TRUE : FALSE;
            WebPFree(frame);
        }
        WebPDemuxReleaseIterator(&iter);
    }
    WebPDemuxDelete(demux);
    return ok;
}

static BOOL loadWebP(Class *cl, Object *o)
{
    struct BitMapHeader *bmh = NULL;
    WebPBitstreamFeatures features;
    ULONG size = 0;
    UBYTE *data;
    STRPTR name = NULL;
    BOOL ok = FALSE;

    if (GetDTAttrs(o, PDTA_BitMapHeader, (ULONG)&bmh, DTA_Name, (ULONG)&name, TAG_DONE) < 1 || !bmh) {
        SetIoErr(ERROR_OBJECT_WRONG_TYPE);
        return FALSE;
    }
    if (!(data = dt_read_source(o, &size)))
        return FALSE;

    if (WebPGetFeatures(data, size, &features) != VP8_STATUS_OK || features.width <= 0 || features.height <= 0) {
        FreeVec(data);
        SetIoErr(DTERROR_INVALID_DATA);
        return FALSE;
    }

    bmh->bmh_Width = bmh->bmh_PageWidth = features.width;
    bmh->bmh_Height = bmh->bmh_PageHeight = features.height;
    bmh->bmh_Depth = features.has_alpha ? 32 : 24;
    bmh->bmh_Masking = features.has_alpha ? mskHasAlpha : mskNone;

    SetDTAttrs(o, NULL, NULL,
        DTA_ObjName, (ULONG)(name ? FilePart(name) : (STRPTR)"WebP"),
        DTA_NominalHoriz, features.width,
        DTA_NominalVert, features.height,
        PDTA_SourceMode, PMODE_V43,
        PDTA_AlphaChannel, features.has_alpha ? TRUE : FALSE,
        TAG_DONE);

    if (features.has_animation)
        ok = writeFirstFrame(cl, o, data, size);
    else {
        int width = 0, height = 0;
        UBYTE *argb = WebPDecodeARGB(data, size, &width, &height);
        if (argb) {
            ok = DoSuperMethod(cl, o, PDTM_WRITEPIXELARRAY, (ULONG)argb, PBPAFMT_ARGB, width * 4,
                0, 0, width, height) ? TRUE : FALSE;
            WebPFree(argb);
        }
    }
    FreeVec(data);
    if (!ok)
        SetIoErr(DTERROR_INVALID_DATA);
    return ok;
}

ULONG dt_dispatch(Class *cl, Object *o, Msg msg)
{
    switch (msg->MethodID) {
    case OM_NEW: {
        Object *obj = (Object *)DoSuperMethodA(cl, o, msg);
        if (obj && !loadWebP(cl, obj)) {
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
