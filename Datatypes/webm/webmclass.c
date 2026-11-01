/*
 * webm.datatype: WebM video for AmigaOS 3.x, an animation.datatype
 * subclass. Our own reader (webm_demux.c) finds the VP8 or VP9 track and
 * libvpx decodes it; each frame is shown in 256 colours (a 6x6x6 colour
 * cube and a grey ramp, with ordered dithering), so it plays on any screen.
 * Frames are decoded on demand; going back, or skipping ahead past a key
 * frame, starts again from the nearest key frame. The sound (Vorbis or
 * Opus) comes from the media.decode/1 service on the services card or a
 * paired Cradle, as 8-bit mono handed out with each frame; without one the
 * video plays silent.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. libvpx is the WebM Project's,
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
#include "vpx/vpx_decoder.h"
#include "vpx/vp8dx.h"

const char LibName[] = "webm.datatype";
const char LibIdString[] = "webm.datatype 47.1 (4.10.2026) AmigaChrome Team, libvpx 1.17.0";
const UWORD LibVersion = 47;
const UWORD LibRevision = 1;
static const char version[] __attribute__((used)) = "$VER: webm.datatype 47.1 (4.10.2026)";

const char dt_superclass[] = "animation.datatype";
const UWORD dt_superversion = 40;

/* media.decode/1 (openamigaservice docs/MEDIA_DECODE.md) */
#define MD_PROBE  1
#define MD_DECODE 2
#define MD_KIND_SOUND 3
/* Files larger than this are not sent for their sound. */
#define SOUND_FILE_MAX (64UL * 1024 * 1024)
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
    BYTE *sound;                   /* 8-bit mono for the whole clip, or NULL */
    ULONG soundLength;             /* samples in sound */
    ULONG soundPerFrame;           /* samples handed out with each frame */
    ULONG period;                  /* Paula period for the sound, handed out with every frame */
} WebMData;

ULONG dt_instsize = sizeof(WebMData);

static WebMDitherTables ditherTables;

BOOL dt_init(void)
{
    webm_dither_init(&ditherTables);
    return TRUE;
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
        webm_dither(&ditherTables, last, d->chunky, d->chunkySize, d->info.width, d->info.height, d->info.width);
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
 * back to be used again (see openvideo's ADTM_LOADFRAME), else a new one. */
static struct BitMap *frameBitMap(WebMData *d, struct BitMap *into)
{
    struct BitMap *bm = into ? into : AllocBitMap(d->info.width, d->info.height, 8, BMF_CLEAR, NULL);
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
    if (d->sound)
        FreeVec(d->sound);
    d->sound = NULL;
    d->chunky = d->frameData = NULL;
    d->keyFrame = NULL;
}

static ULONG get32(const UBYTE *p)
{
    return (ULONG)p[0] << 24 | (ULONG)p[1] << 16 | (ULONG)p[2] << 8 | p[3];
}

/* The clip's sound from media.decode/1, as 8-bit mono at no more than
 * 28 kHz; returns the rate, or 0 (silent) when no service has it. */
static ULONG loadSound(WebMData *d, ULONG size)
{
    struct dt_service svc;
    struct OSBuffer buf[4];
    ULONG extra[4] = { 1, 28000, 0, 0 }, frames, rate = 0, got = 0, i;
    UBYTE info[24], *file, *pcm = NULL;

    if (size > SOUND_FILE_MAX || !(file = AllocVec(size, MEMF_ANY)))
        return 0;
    if (readAt((void *)d->file, 0, file, size) != (long)size || !dt_service_open(&svc, "media.decode/1")) {
        FreeVec(file);
        return 0;
    }
    memset(buf, 0, sizeof buf);
    buf[0].ob_Data = file;
    buf[0].ob_Length = size;
    buf[1].ob_Data = info;
    buf[1].ob_Length = sizeof info;
    if (dt_service_call(&svc, MD_PROBE, 0, 2, buf, extra, NULL, NULL) == OSERR_OK && get32(info) == MD_KIND_SOUND
        && (frames = get32(info + 12)) && frames < 0x40000000UL && get32(info + 20) == 1
        && (pcm = AllocVec(frames * 2, MEMF_ANY))) {
        buf[1].ob_Data = pcm;
        buf[1].ob_Length = frames * 2;
        if (dt_service_call(&svc, MD_DECODE, 0, 2, buf, extra, &got, NULL) == OSERR_OK && got
            && (d->sound = AllocVec(got, MEMF_CHIP)) != NULL) {
            for (i = 0; i < got; i++)                 /* the high byte of each 16-bit sample, in chip RAM */
                d->sound[i] = pcm[i * 2];
            d->soundLength = got;
            rate = get32(info + 16);
        }
    }
    if (pcm)
        FreeVec(pcm);
    dt_service_close(&svc);
    FreeVec(file);
    return rate;
}

static BOOL loadWebM(Class *cl, Object *o)
{
    WebMData *d = INST_DATA(cl, o);
    ULONG sourceType = DTST_FILE, fps, size, rate;
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
    rc = webm_parse(readAt, (void *)file, size, &d->info);
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

    if ((rate = loadSound(d, size)) != 0) {
        d->soundPerFrame = rate / fps;
        d->period = PAL_CLOCK / rate;
        SetDTAttrs(o, NULL, NULL, ADTA_Period, d->period, ADTA_Volume, 64, ADTA_Cycles, 1, TAG_DONE);
    }

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
        /* The superclass stops its loader and player first (see openvideo);
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
        ULONG index = alf->alf_TimeStamp;
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
        if (d->sound && d->soundPerFrame && index * d->soundPerFrame < d->soundLength) {
            ULONG at = index * d->soundPerFrame, n = d->soundPerFrame;
            if (at + n > d->soundLength)
                n = d->soundLength - at;
            alf->alf_Sample = d->sound + at;   /* ours: freed with the object */
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
