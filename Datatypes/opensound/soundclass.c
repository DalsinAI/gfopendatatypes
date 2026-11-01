/*
 * gfopensound.datatype: FLAC, Ogg Vorbis, Opus, AAC/M4A, ALAC, WMA and MP3
 * sounds, MIDI, C64 SID tunes and XM/IT/S3M modules for AmigaOS 3.x, a
 * sound.datatype subclass. MIDI and SID are played on the host into PCM. The sound is decoded
 * by the media.decode/1 service (DalsinAI/openamigaservice
 * docs/MEDIA_DECODE.md) on the services card or a paired Cradle, which
 * sends back 16-bit PCM at a rate Paula plays (at most
 * ENV:GFOpenImage/SoundRate, default 28000 Hz; 44.1 kHz comes as 22.05 kHz).
 *
 * With sound.datatype V44 or newer the sound is kept in 16-bit stereo;
 * older ones get 8-bit mono. With neither a card nor a Cradle the sound
 * does not open yet (FLAC, Vorbis and Opus decoders on the 68k come next).
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
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

#include "dtlib.h"
#include "dtservice.h"

const char LibName[] = "gfopensound.datatype";
const char LibIdString[] = "gfopensound.datatype 47.1 (5.10.2026) AmiCradle Team, decoded by media.decode/1";
const UWORD LibVersion = 47;
const UWORD LibRevision = 1;
static const char version[] __attribute__((used)) = "$VER: gfopensound.datatype 47.1 (5.10.2026)";

const char dt_superclass[] = "sound.datatype";
const UWORD dt_superversion = 39;
ULONG dt_instsize = 0;

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

ULONG dt_dispatch(Class *cl, Object *o, Msg msg)
{
    switch (msg->MethodID) {
    case OM_NEW: {
        Object *obj = (Object *)DoSuperMethodA(cl, o, msg);
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
