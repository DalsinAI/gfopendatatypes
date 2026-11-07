/*
 * gfopensound.datatype: FLAC, Ogg Vorbis, Opus, AAC/M4A, ALAC, WMA and MP3
 * sounds, MIDI, C64 SID tunes and XM/IT/S3M modules for AmigaOS 3.x, a
 * sound.datatype subclass. MIDI and SID are played on the host into PCM. The sound is decoded
 * by the media.decode/1 service (DalsinAI/gfopenservice
 * docs/MEDIA_DECODE.md) on the services card or a paired Cradle, which
 * sends back 16-bit PCM at a rate Paula plays (at most
 * ENV:GFOpenImage/SoundRate, default 28000 Hz; 44.1 kHz comes as 22.05 kHz).
 *
 * With sound.datatype V44 or newer the sound is kept in 16-bit stereo;
 * older ones get 8-bit mono. With neither a card nor a Cradle the sound
 * does not open yet (FLAC, Vorbis and Opus decoders on the 68k come next).
 *
 * DTM_WRITE in raw mode (47.2) writes AAC (as M4A, or ADTS for a .aac) or
 * WMA through media.encode/1 (common/dtencode.c): the object's samples go
 * to the service as 16-bit PCM, its FFmpeg encodes them, and the file comes
 * back. The Team's ruling (6 November 2026): AAC and WMA are written by the
 * service's FFmpeg, never by code in the pack. OIA_WriteFormat ('M4A ',
 * 'AAC ', 'WMA ') and OIA_Quality in dtw_AttrList choose
 * (include/datatypes/gfopenimage.h). No card or Cradle, or one whose
 * FFmpeg can't: the write fails with ERROR_NOT_IMPLEMENTED. IFF mode is
 * sound.datatype's 8SVX. An object made with no source (DTST_RAM) is a
 * sound to set and save: a converter's target.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <string.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/soundclass.h>
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

const char LibName[] = "gfopensound.datatype";
const char LibIdString[] = "gfopensound.datatype 47.2 (7.11.2026) AmiCradle Team, decoded by media.decode/1, AAC and WMA written by media.encode/1";
const UWORD LibVersion = 47;
const UWORD LibRevision = 2;
static const char version[] __attribute__((used)) = "$VER: gfopensound.datatype 47.2 (7.11.2026)";

const char dt_superclass[] = "sound.datatype";
const UWORD dt_superversion = 39;

typedef struct {
    ULONG format;                  /* as media.decode/1 named it ('AAC ', 'WMA '); 0 with no source */
} SoundData;

ULONG dt_instsize = sizeof(SoundData);

/* sound.datatype V44's sample type, asked for when writing (the Team's
 * sound.datatype answers it; the 3.2 NDK has no name for it, and its own
 * name here would switch loadSound() to 16 bits). */
#define WRITE_SampleType (SDTA_Dummy + 30)
/* Sample frames sent to media.encode/1 at a time. */
#define STRIP_FRAMES 16384UL

/* media.decode/1 (docs/MEDIA_DECODE.md) */
#define MD_PROBE  1
#define MD_DECODE 2
#define MD_KIND_SOUND 3

/* The PAL colour clock, for periods. */
#define PAL_CLOCK 3546895UL

static BOOL wide;    /* sound.datatype V44+: 16-bit and stereo */

BOOL dt_init(void)
{
#ifdef SDTA_SampleType
    struct Library *sound = OpenLibrary((CONST_STRPTR)"datatypes/sound.datatype", 44);
    if (sound) {
        wide = TRUE;
        CloseLibrary(sound);
    }
#endif
    return TRUE;
}

void dt_cleanup(void)
{
}

static ULONG get32(const UBYTE *p)
{
    return (ULONG)p[0] << 24 | (ULONG)p[1] << 16 | (ULONG)p[2] << 8 | p[3];
}

static ULONG soundRate(void)
{
    char buf[16];
    LONG n = GetVar((CONST_STRPTR)"GFOpenImage/SoundRate", (STRPTR)buf, sizeof buf, GVF_GLOBAL_ONLY);
    ULONG v = 0;
    LONG i;

    for (i = 0; i < n && buf[i] >= '0' && buf[i] <= '9'; i++)
        v = v * 10 + (buf[i] - '0');
    return v >= 4000 ? v : 28000;
}

static BOOL loadSound(Class *cl, Object *o)
{
    struct VoiceHeader *vh = NULL;
    struct dt_service svc;
    struct OSBuffer buf[4];
    ULONG extra[4], size = 0, frames, rate, channels, got, bytes;
    UBYTE info[24], *data, *pcm = NULL;
    STRPTR name = NULL;
    BOOL ok = FALSE;
    LONG err = DTERROR_INVALID_DATA;

    GetDTAttrs(o, SDTA_VoiceHeader, (ULONG)&vh, DTA_Name, (ULONG)&name, TAG_DONE);
    if (!(data = dt_read_source(o, &size)))
        return FALSE;
    if (!dt_service_open(&svc, "media.decode/1")) {
        FreeVec(data);
        SetIoErr(ERROR_NOT_IMPLEMENTED);       /* no card and no paired Cradle */
        return FALSE;
    }

    memset(buf, 0, sizeof buf);
    extra[0] = wide ? 2 : 1;
    extra[1] = soundRate();
    extra[2] = extra[3] = 0;
    buf[0].ob_Data = data;
    buf[0].ob_Length = size;
    buf[1].ob_Data = info;
    buf[1].ob_Length = sizeof info;
    if (dt_service_call(&svc, MD_PROBE, 0, 2, buf, extra, NULL, NULL) != OSERR_OK || get32(info) != MD_KIND_SOUND)
        goto out;
    ((SoundData *)INST_DATA(cl, o))->format = get32(info + 4);
    frames = get32(info + 12);
    rate = get32(info + 16);
    channels = get32(info + 20);
    if (!frames || !rate || !channels || channels > 2 || frames > 0x7fffffffUL / 4)
        goto out;

    bytes = frames * channels * 2;
    if (!(pcm = AllocVec(bytes, MEMF_ANY))) {
        err = ERROR_NO_FREE_STORE;
        goto out;
    }
    buf[1].ob_Data = pcm;
    buf[1].ob_Length = bytes;
    if (dt_service_call(&svc, MD_DECODE, 0, 2, buf, extra, &got, NULL) != OSERR_OK || !got)
        goto out;
    if (got < frames)
        frames = got;

    if (vh) {
        vh->vh_OneShotHiSamples = frames;
        vh->vh_RepeatHiSamples = 0;
        vh->vh_SamplesPerHiCycle = 0;
        vh->vh_SamplesPerSec = rate;
        vh->vh_Octaves = 1;
        vh->vh_Compression = CMP_NONE;
        vh->vh_Volume = 0x10000;
    }
#ifdef SDTA_SampleType
    if (wide) {
        /* 16-bit, interleaved when stereo, as the decoder wrote it. */
        SetDTAttrs(o, NULL, NULL,
            DTA_ObjName, (ULONG)(name ? FilePart(name) : (STRPTR)"Sound"),
            SDTA_SampleType, channels == 2 ? SDTST_S16S : SDTST_M16S,
            SDTA_Sample, (ULONG)pcm,
            SDTA_SampleLength, frames,
            SDTA_Frequency, rate,
            SDTA_Period, PAL_CLOCK / rate,
            SDTA_Volume, 64,
            SDTA_Cycles, 1,
            TAG_DONE);
        pcm = NULL;                            /* sound.datatype frees it */
        ok = TRUE;
    } else
#endif
    {
        /* 8-bit mono: the high byte of each 16-bit sample, in place. */
        ULONG i;
        for (i = 0; i < frames; i++)
            pcm[i] = pcm[i * 2];
        SetDTAttrs(o, NULL, NULL,
            DTA_ObjName, (ULONG)(name ? FilePart(name) : (STRPTR)"Sound"),
            SDTA_Sample, (ULONG)pcm,
            SDTA_SampleLength, frames,
            SDTA_Period, PAL_CLOCK / rate,
            SDTA_Volume, 64,
            SDTA_Cycles, 1,
            TAG_DONE);
        pcm = NULL;
        ok = TRUE;
    }
out:
    if (pcm)
        FreeVec(pcm);
    dt_service_close(&svc);
    FreeVec(data);
    if (!ok)
        SetIoErr(err);
    return ok;
}

/* DTM_WRITE, DTWM_RAW: the sound as M4A, AAC or WMA, through media.encode/1. 0, or an IoErr() code. */
static LONG writeSound(Class *cl, Object *o, struct dtWrite *w)
{
    struct me_sound snd;
    struct me_settings s;
    struct dt_encoder enc;
    ULONG left = 0, right = 0, length = 0, type = 0, rate = 0, period = 0, sourceType = DTST_FILE, at, n;
    STRPTR name = NULL;
    UBYTE *strip;
    LONG err;

    if (!w->dtw_FileHandle)
        return ERROR_REQUIRED_ARG_MISSING;
    GetDTAttrs(o, SDTA_Sample, (ULONG)&left, SDTA_SampleLength, (ULONG)&length, DTA_Name, (ULONG)&name, TAG_DONE);
    GetDTAttrs(o, SDTA_LeftSample, (ULONG)&left, TAG_DONE);    /* the same as SDTA_Sample, where it is known */
    GetDTAttrs(o, SDTA_RightSample, (ULONG)&right, TAG_DONE);
    GetDTAttrs(o, WRITE_SampleType, (ULONG)&type, TAG_DONE);   /* 0 (8-bit mono) where it is not known */
    GetDTAttrs(o, SDTA_Period, (ULONG)&period, TAG_DONE);
    GetDTAttrs(o, SDTA_SamplesPerSec, (ULONG)&rate, TAG_DONE);
    GetDTAttrs(o, DTA_SourceType, (ULONG)&sourceType, TAG_DONE);
    if (!left || !length || type > 3)
        return ERROR_OBJECT_WRONG_TYPE;
    if (!rate && period)
        rate = PAL_CLOCK / period;
    if (!rate)
        rate = 8000;                           /* as sound.datatype: no rate and no period */
    memset(&s, 0, sizeof s);
    s.format = me_pick_format(ME_KIND_SOUND, GetTagData(OIA_WriteFormat, 0, w->dtw_AttrList),
                              ((SoundData *)INST_DATA(cl, o))->format, dt_name_hint(name), sourceType != DTST_RAM);
    if (!s.format)
        return ERROR_NOT_IMPLEMENTED;          /* FLAC, MP3...: not written by the service (yet) */
    snd.left = (const signed char *)left;
    snd.right = (const signed char *)right;
    snd.length = length;
    snd.type = type;
    s.rate = rate;
    s.channels = me_sound_channels(&snd);
    s.samples = ME_SAMPLE_S16;
    s.quality = me_quality((LONG)GetTagData(OIA_Quality, (ULONG)-1, w->dtw_AttrList));
    if (!(strip = AllocVec(STRIP_FRAMES * s.channels * 2, MEMF_ANY)))
        return ERROR_NO_FREE_STORE;
    if (!(err = dt_encode_begin(&enc, &s))) {
        for (at = 0; !err && at < length; at += n) {
            n = length - at < STRIP_FRAMES ? length - at : STRIP_FRAMES;
            me_sound_s16(&snd, at, n, strip);
            err = dt_encode_put(&enc, ME_STREAM_SOUND, strip, n * s.channels * 2);
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
            return DoSuperMethodA(cl, o, msg);   /* IFF: sound.datatype's 8SVX */
        if ((err = writeSound(cl, o, w))) {
            SetIoErr(err);
            return FALSE;
        }
        return TRUE;
    }
    case OM_NEW: {
        Object *obj = (Object *)DoSuperMethodA(cl, o, msg);
        if (obj)
            ((SoundData *)INST_DATA(cl, obj))->format = 0;
        if (obj && GetTagData(DTA_SourceType, DTST_FILE, ((struct opSet *)msg)->ops_AttrList) == DTST_RAM)
            return (ULONG)obj;         /* no source: a sound to set, and save */
        if (obj && !loadSound(cl, obj)) {
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
