/*
 * webm.datatype: WebM video for AmigaOS 3.x, an animation.datatype
 * subclass. Our own reader (webm_demux.c) finds the VP8 or VP9 track and
 * libvpx decodes it; each frame is shown in 256 colours (a 6x6x6 colour
 * cube and a grey ramp, with ordered dithering), so it plays on any screen.
 * Frames are decoded on demand; going back, or skipping ahead past a key
 * frame, starts again from the nearest key frame. The sound (Vorbis or
 * Opus) comes from the media.decode/1 service on the services card or a
 * paired Cradle, as gfopenvideo.datatype's does: the file goes up in parts
 * and is kept open there, and each frame's 8-bit mono is asked for as the
 * frame loads; without the service the video plays silent.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team. libvpx is the WebM Project's,
 * under its BSD licence (upstream/libvpx/LICENSE).
 */
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <dos/dos.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
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
#include <clib/alib_protos.h>
#include <string.h>

#include "dtlib.h"
#include "dtservice.h"
#include "webm_demux.h"
#include "webm_dither.h"
#include "webm_readcache.h"
#include "vpx/vpx_decoder.h"
#include "vpx/vp8dx.h"

const char LibName[] = "webm.datatype";
const char LibIdString[] = "webm.datatype 47.1 (4.10.2026) AmiCradle Team, libvpx 1.17.0";
const UWORD LibVersion = 47;
const UWORD LibRevision = 1;
static const char version[] __attribute__((used)) = "$VER: webm.datatype 47.1 (4.10.2026)";

const char dt_superclass[] = "animation.datatype";
const UWORD dt_superversion = 40;

/* media.decode/1 (gfopenservice docs/MEDIA_DECODE.md) */
#define MD_VOPEN   3
#define MD_VCLOSE  5
#define MD_VSOUND  7
#define MD_VUPLOAD 8
#define MD_KIND_ANIMATION 2
#define MD_FLAG_SOUND 2
/* The file goes to the service in parts this big (as gfopenvideo.datatype's),
 * so it is never all in memory here. */
#define UPLOAD_PART (256UL * 1024)
/* The service keeps an upload of up to 1 GB. */
#define SOUND_FILE_MAX (1024UL * 1024 * 1024)
/* Each frame's sound is asked for at about this rate: exactly the rate
 * its Paula period plays. */
#define SOUND_RATE 22050
/* Frames whose sound is kept at once in chip RAM: animation.datatype loads
 * a few ahead, never this many. */
#define SOUND_RING 64
/* VSOUND's steady frames for one of ours, at most. */
#define SOUND_SPAN_MAX 64
/* The PAL colour clock, for periods. */
#define PAL_CLOCK 3546895UL

/* libvpx decodes on a stack of its own; callers may have only 4 KB. */
#define DECODE_STACK (96 * 1024)

typedef struct {
    struct SignalSemaphore lock;   /* the file and the decoder are shared by the loader */
    BPTR file;
    WebMInfo info;
    vpx_codec_ctx_t codec;
    int codecReady;
    LONG lastDecoded;              /* frame the decoder last produced, -1 if none */
    UBYTE *chunky;                 /* width x height palette indices */
    ULONG chunkySize;              /* bytes allocated at chunky */
    UBYTE *frameData;
    ULONG frameDataSize;
    struct BitMap *keyFrame;
    ULONG fps;                     /* frames a second, as played */
    ULONG soundHandle;             /* the service's open video, for VSOUND; 0 when silent */
    ULONG hostFps1000;             /* the steady frames a second VSOUND counts in, x 1000 */
    ULONG soundRate;               /* asked of VSOUND: the rate period plays */
    BYTE *ring;                    /* SOUND_RING slots of soundSlot bytes in chip RAM, by frame */
    ULONG soundSlot;
    ULONG period;                  /* Paula period for the sound, handed out with every frame */
} WebMData;

ULONG dt_instsize = sizeof(WebMData);

BOOL dt_init(void)
{
    return TRUE;                   /* the dither's tables are const (webm_dither_tables.h) */
}

void dt_cleanup(void)
{
}

static long readAt(void *handle, uint32_t offset, void *buffer, uint32_t length)
{
    BPTR file = (BPTR)handle;
    if (Seek(file, offset, OFFSET_BEGINNING) < 0)
        return -1;
    return Read(file, buffer, length);
}

static BOOL openDecoder(WebMData *d)
{
    if (d->codecReady)
        vpx_codec_destroy(&d->codec);
    d->codecReady = 0;
    d->lastDecoded = -1;
    if (vpx_codec_dec_init(&d->codec, d->info.codec == WEBM_CODEC_VP8 ? vpx_codec_vp8_dx() : vpx_codec_vp9_dx(), NULL, 0))
        return FALSE;
    d->codecReady = 1;
    return TRUE;
}

/* Feeds one frame to the decoder; the picture lands in d->chunky. */
static BOOL decodeOne(WebMData *d, ULONG index)
{
    WebMFrame *frame = &d->info.frames[index];
    vpx_codec_iter_t iter = NULL;
    vpx_image_t *img, *last = NULL;

    if (frame->size > d->frameDataSize) {
        if (d->frameData)
            FreeVec(d->frameData);
        d->frameDataSize = frame->size + 4096;
        d->frameData = AllocVec(d->frameDataSize, MEMF_ANY);
        if (!d->frameData) {
            d->frameDataSize = 0;
            return FALSE;
        }
    }
    if (readAt((void *)d->file, frame->offset, d->frameData, frame->size) != (long)frame->size)
        return FALSE;
    if (vpx_codec_decode(&d->codec, d->frameData, frame->size, NULL, 0))
        return FALSE;
    while ((img = vpx_codec_get_frame(&d->codec, &iter)))
        last = img;
    if (last)
        webm_dither(last, d->chunky, d->chunkySize, d->info.width, d->info.height, d->info.width);
    d->lastDecoded = index;
    return TRUE;
}

typedef struct {
    WebMData *d;
    ULONG want;
} DecodeRequest;

/* Decodes up to the wanted frame, from the nearest key frame when needed. */
static ULONG decodeRequest(APTR arg)
{
    DecodeRequest *rq = arg;
    WebMData *d = rq->d;
    LONG want = rq->want, start, i;

    /* Forward from where the decoder is, unless a key frame lies between. */
    start = want;
    while (start > 0 && !d->info.frames[start].keyFrame)
        start--;
    if (!d->codecReady || d->lastDecoded < 0 || want <= d->lastDecoded || start > d->lastDecoded) {
        if (!openDecoder(d))
            return FALSE;
    } else
        start = d->lastDecoded + 1;
    for (i = start; i <= want; i++)
        if (!decodeOne(d, i))
            return FALSE;
    return TRUE;
}

/* The decoded frame in a bitmap: "into" when animation.datatype hands one
 * back to be used again (see gfopenvideo's ADTM_LOADFRAME), else a new one,
 * not cleared: WriteChunkyPixels fills every pixel of it. */
static struct BitMap *frameBitMap(WebMData *d, struct BitMap *into)
{
    struct BitMap *bm = into ? into : AllocBitMap(d->info.width, d->info.height, 8, 0, NULL);
    struct RastPort rp;
    if (!bm)
        return NULL;
    InitRastPort(&rp);
    rp.BitMap = bm;
    WriteChunkyPixels(&rp, 0, 0, d->info.width - 1, d->info.height - 1, d->chunky, d->info.width);
    return bm;
}

static BOOL loadFrame(WebMData *d, ULONG index)
{
    DecodeRequest rq = { d, index };
    if (index >= d->info.frameCount)
        index = rq.want = d->info.frameCount - 1;
    if (d->codecReady && d->lastDecoded == (LONG)index)
        return TRUE;
    return dt_call_with_stack(DECODE_STACK, decodeRequest, &rq) ? TRUE : FALSE;
}

static void setPalette(Object *o)
{
    struct ColorRegister *cmap = NULL;
    ULONG *cregs = NULL, *gregs = NULL;
    int i;

    SetDTAttrs(o, NULL, NULL, ADTA_NumColors, 256, TAG_DONE);
    GetDTAttrs(o, ADTA_ColorRegisters, (ULONG)&cmap, ADTA_CRegs, (ULONG)&cregs, ADTA_GRegs, (ULONG)&gregs, TAG_DONE);
    for (i = 0; i < 256; i++) {
        UBYTE r, g, b;
        if (i < 216) {
            r = (i / 36) * 51;
            g = ((i / 6) % 6) * 51;
            b = (i % 6) * 51;
        } else
            r = g = b = (UBYTE)((i - 216) * 255 / 39);
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

/* Closes the service's video (VCLOSE) and frees the ring of sound. */
static void closeSound(WebMData *d)
{
    struct dt_service svc;
    struct OSBuffer buf[4];
    ULONG extra[4] = { 0, 0, 0, 0 };

    if (d->soundHandle && dt_service_open(&svc, "media.decode/1")) {
        memset(buf, 0, sizeof buf);
        dt_service_call(&svc, MD_VCLOSE, d->soundHandle, 0, buf, extra, NULL, NULL);
        dt_service_close(&svc);
    }
    d->soundHandle = 0;
    if (d->ring)
        FreeVec(d->ring);
    d->ring = NULL;
}

static void freeData(WebMData *d)
{
    if (d->codecReady)
        vpx_codec_destroy(&d->codec);
    d->codecReady = 0;
    webm_free(&d->info);
    if (d->chunky)
        FreeVec(d->chunky);
    if (d->frameData)
        FreeVec(d->frameData);
    if (d->keyFrame) {
        WaitBlit();
        FreeBitMap(d->keyFrame);
    }
    closeSound(d);
    d->chunky = d->frameData = NULL;
    d->keyFrame = NULL;
}

static ULONG get32(const UBYTE *p)
{
    return (ULONG)p[0] << 24 | (ULONG)p[1] << 16 | (ULONG)p[2] << 8 | p[3];
}

/* VSOUND's steady frame at the start of our frame index: the same frame
 * when the clip plays at the rate VSOUND counts in, else index scaled by
 * hostFps1000 / (fps x 1000). */
static ULONG hostFrame(const WebMData *d, ULONG index)
{
    if (d->hostFps1000 == d->fps * 1000)
        return index;
    return (ULONG)((unsigned long long)index * d->hostFps1000 / ((unsigned long long)d->fps * 1000));
}

/* Sends the file to the service in parts of UPLOAD_PART (VUPLOAD); the
 * service's status, with the upload in *upload. */
static LONG uploadFile(WebMData *d, struct dt_service *svc, ULONG size, ULONG *upload)
{
    struct OSBuffer buf[4];
    ULONG part[4] = { 0, 0, 0, 0 }, sent = 0;
    UBYTE *data;
    LONG n, st = OSERR_OK;

    *upload = 0;
    if (!(data = AllocVec(UPLOAD_PART, MEMF_ANY)))
        return OSERR_BADREQUEST;
    memset(buf, 0, sizeof buf);
    while (st == OSERR_OK && sent < size) {
        n = readAt((void *)d->file, sent, data, size - sent < UPLOAD_PART ? size - sent : UPLOAD_PART);
        if (n <= 0) {
            st = OSERR_BADREQUEST;
            break;
        }
        sent += n;
        part[0] = sent >= size ? 1 : 0;   /* the last part */
        buf[0].ob_Data = data;
        buf[0].ob_Length = n;
        st = dt_service_call(svc, MD_VUPLOAD, *upload, 0, buf, part, upload, NULL);
        if (st == OSERR_OK && !*upload)
            st = OSERR_HOST;
    }
    FreeVec(data);
    return st;
}

/* The clip's sound from media.decode/1, as gfopenvideo.datatype gets a
 * video's: the file is uploaded in parts and opened (VOPEN) on the
 * service, which keeps it, and each frame's sound is asked for as it loads
 * (fetchSound). Only for a clip with an audio track; FALSE (silent) when
 * the service, its VUPLOAD or the chip RAM for the ring is missing. */
static BOOL loadSound(WebMData *d, ULONG size)
{
    struct dt_service svc;
    struct OSBuffer buf[4];
    ULONG extra[4] = { 0, 0, 0, 0 }, upload, handle = 0, fps1000 = 0, span, slot;
    UBYTE info[24];

    if (!d->info.audioTracks || size > SOUND_FILE_MAX || !d->fps || d->fps > 1000
        || !dt_service_open(&svc, "media.decode/1"))
        return FALSE;
    if (uploadFile(d, &svc, size, &upload) != OSERR_OK) {
        dt_service_close(&svc);
        return FALSE;
    }
    memset(buf, 0, sizeof buf);
    buf[1].ob_Data = info;
    buf[1].ob_Length = sizeof info;
    extra[0] = extra[1] = 16;              /* only the sound is asked for: the smallest picture will do */
    extra[2] = upload;
    if (dt_service_call(&svc, MD_VOPEN, 0, 2, buf, extra, &handle, &fps1000) != OSERR_OK)
        handle = 0;
    dt_service_close(&svc);
    d->soundHandle = handle;
    /* VSOUND counts in steady frames (a variable-rate clip's shortest, at
     * most 50 a second): up to span of them for each of ours. */
    span = fps1000 ? (fps1000 + d->fps * 1000 - 1) / (d->fps * 1000) : 0;
    if (!handle || get32(info) != MD_KIND_ANIMATION || !(get32(info + 8) & MD_FLAG_SOUND)
        || fps1000 > 1000000 || !span || span > SOUND_SPAN_MAX) {
        closeSound(d);
        return FALSE;
    }
    d->hostFps1000 = fps1000;
    d->period = PAL_CLOCK / SOUND_RATE;
    d->soundRate = PAL_CLOCK / d->period;
    /* A slot holds span steady frames' sound, each at most rate / fps
     * rounded up, and a little more for the service's own rounding. */
    slot = span * (d->soundRate * 1000 / fps1000 + 1) + 2;
    d->soundSlot = (slot + 1) & ~1UL;
    if (!(d->ring = AllocVec(SOUND_RING * d->soundSlot, MEMF_CHIP | MEMF_CLEAR))) {
        closeSound(d);
        return FALSE;
    }
    return TRUE;
}

/* Frame index's sound into its slot in the chip RAM ring (VSOUND): the
 * steady frames from where it starts to where the next one starts; its
 * length, or 0. Opens the service itself: animation.datatype loads frames
 * from its own process. */
static ULONG fetchSound(WebMData *d, ULONG index, BYTE **at)
{
    struct dt_service svc;
    struct OSBuffer buf[4];
    ULONG extra[4] = { 0, 0, 0, 0 }, n, end, got, filled = 0;

    if (!d->soundHandle || !d->ring || !dt_service_open(&svc, "media.decode/1"))
        return 0;
    *at = d->ring + (index % SOUND_RING) * d->soundSlot;
    memset(buf, 0, sizeof buf);
    extra[1] = d->soundRate;
    for (n = hostFrame(d, index), end = hostFrame(d, index + 1); n < end && filled < d->soundSlot; n++) {
        got = 0;
        extra[0] = n;
        buf[1].ob_Data = *at + filled;
        buf[1].ob_Length = d->soundSlot - filled;
        if (dt_service_call(&svc, MD_VSOUND, d->soundHandle, 2, buf, extra, &got, NULL) != OSERR_OK
            || got > d->soundSlot - filled)
            break;
        filled += got;
    }
    dt_service_close(&svc);
    return filled;
}

static BOOL loadWebM(Class *cl, Object *o)
{
    WebMData *d = INST_DATA(cl, o);
    ULONG sourceType = DTST_FILE, fps, size;
    STRPTR name = NULL;
    BPTR file = 0;
    int rc;

    memset(d, 0, sizeof *d);
    InitSemaphore(&d->lock);
    d->lastDecoded = -1;
    GetDTAttrs(o, DTA_SourceType, (ULONG)&sourceType, DTA_Handle, (ULONG)&file, DTA_Name, (ULONG)&name, TAG_DONE);
    if (sourceType != DTST_FILE || !file) {
        SetIoErr(ERROR_OBJECT_WRONG_TYPE);
        return FALSE;
    }
    d->file = file;
    if (Seek(file, 0, OFFSET_END) < 0) {
        SetIoErr(ERROR_SEEK_ERROR);
        return FALSE;
    }
    size = Seek(file, 0, OFFSET_BEGINNING);
    {
        /* The reader's small reads come from a cache of the file (one Read
         * for many elements), or straight from the file without memory */
        WebMReadCache *cache = AllocVec(sizeof *cache, MEMF_ANY);
        if (cache) {
            webm_cache_init(cache, readAt, (void *)file);
            rc = webm_parse(webm_cache_read, cache, size, &d->info);
            FreeVec(cache);
        } else
            rc = webm_parse(readAt, (void *)file, size, &d->info);
    }
    if (rc) {
        SetIoErr(rc == -3 ? ERROR_NO_FREE_STORE : DTERROR_INVALID_DATA);
        return FALSE;
    }
    /* webm_parse takes no side over WEBM_MAX_SIDE; width x height is
     * checked again so the buffer can never be smaller than the picture */
    if (!d->info.width || !d->info.height || d->info.width > WEBM_MAX_SIDE || d->info.height > WEBM_MAX_SIDE
        || d->info.width > 0xffffffffUL / d->info.height) {
        SetIoErr(DTERROR_INVALID_DATA);
        return FALSE;
    }
    d->chunkySize = d->info.width * d->info.height;
    d->chunky = AllocVec(d->chunkySize, MEMF_ANY | MEMF_CLEAR);
    if (!d->chunky) {
        SetIoErr(ERROR_NO_FREE_STORE);
        return FALSE;
    }
    if (!loadFrame(d, 0) || !(d->keyFrame = frameBitMap(d, NULL))) {
        SetIoErr(DTERROR_INVALID_DATA);
        return FALSE;
    }

    if (d->info.frameDurationNs)
        fps = (1000000000UL + d->info.frameDurationNs / 2) / d->info.frameDurationNs;
    else if (d->info.durationMs && d->info.frameCount > 1)
        fps = ((d->info.frameCount - 1) * 1000UL + d->info.durationMs / 2) / d->info.durationMs;
    else
        fps = 25;
    if (!fps)
        fps = 1;

    d->fps = fps;
    if (loadSound(d, size))
        SetDTAttrs(o, NULL, NULL, ADTA_Period, d->period, ADTA_Volume, 64, ADTA_Cycles, 1, TAG_DONE);

    setPalette(o);
    SetDTAttrs(o, NULL, NULL,
        DTA_ObjName, (ULONG)(name ? FilePart(name) : (STRPTR)"WebM"),
        DTA_NominalHoriz, d->info.width,
        DTA_NominalVert, d->info.height,
        ADTA_Width, d->info.width,
        ADTA_Height, d->info.height,
        ADTA_Depth, 8,
        ADTA_Frames, d->info.frameCount,
        ADTA_FramesPerSecond, fps,
        ADTA_KeyFrame, (ULONG)d->keyFrame,
        TAG_DONE);
    return TRUE;
}

ULONG dt_dispatch(Class *cl, Object *o, Msg msg)
{
    switch (msg->MethodID) {
    case OM_NEW: {
        Object *obj = (Object *)DoSuperMethodA(cl, o, msg);
        if (obj && !loadWebM(cl, obj)) {
            LONG err = IoErr();
            CoerceMethod(cl, obj, OM_DISPOSE);
            SetIoErr(err);
            obj = NULL;
        }
        return (ULONG)obj;
    }
    case OM_DISPOSE: {
        /* The superclass stops its loader and player first (see gfopenvideo);
         * the decoder, the sound and the key frame are freed after it. */
        WebMData keep = *(WebMData *)INST_DATA(cl, o);
        ULONG rc = DoSuperMethodA(cl, o, msg);
        freeData(&keep);
        return rc;
    }
    case ADTM_LOADFRAME: {
        WebMData *d = INST_DATA(cl, o);
        struct adtFrame *alf = (struct adtFrame *)msg;
        struct BitMap *bm = NULL, *given = alf->alf_BitMap;
        ULONG index = alf->alf_TimeStamp, n;
        BYTE *sample = NULL;
        if (index >= d->info.frameCount)
            index = d->info.frameCount - 1;
        ObtainSemaphore(&d->lock);
        if (d->info.frames && loadFrame(d, index))
            bm = frameBitMap(d, given);
        ReleaseSemaphore(&d->lock);
        alf->alf_Frame = index;
        alf->alf_Duration = 1;
        alf->alf_BitMap = bm ? bm : given;
        alf->alf_CMap = NULL;
        alf->alf_Sample = NULL;
        alf->alf_SampleLength = 0;
        if ((n = fetchSound(d, index, &sample)) != 0) {
            alf->alf_Sample = sample;          /* ours, in the ring: freed with the object */
            alf->alf_SampleLength = n;
            alf->alf_Period = d->period;   /* animation.datatype 47 plays each frame's sound at this */
        }
        alf->alf_UserData = alf->alf_BitMap;     /* follows the bitmap from frame to frame */
        return bm ? 1 : 0;
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
