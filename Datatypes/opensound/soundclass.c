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
 * older ones get 8-bit mono. With neither a card nor a Cradle, FLAC, Ogg
 * Vorbis and MP3 are decoded on this CPU (47.2; snddec.c: dr_flac,
 * stb_vorbis, dr_mp3), 44.1 and 48 kHz halved for Paula; the others don't
 * open without one.
 *
 * For programs (datatypes/gfopenimage.h, 47.2): opened with OIA_Reader,
 * FLAC, Ogg Vorbis and MP3 are decoded on this CPU a piece at a time by
 * OIM_READSOUND, at their own rate, never whole; nothing is played.
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
#include <proto/utility.h>
#include <clib/alib_protos.h>

#include <datatypes/gfopenimage.h>

#include "dtlib.h"
#include "dtservice.h"
#include "snddec.h"

const char LibName[] = "gfopensound.datatype";
const char LibIdString[] = "gfopensound.datatype 47.2 (8.11.2026) AmiCradle Team, decoded by media.decode/1 or this CPU";
const UWORD LibVersion = 47;
const UWORD LibRevision = 2;
static const char version[] __attribute__((used)) = "$VER: gfopensound.datatype 47.2 (8.11.2026)";

const char dt_superclass[] = "sound.datatype";
const UWORD dt_superversion = 39;

struct SndInst {
    snddec *dec;               /* OIA_Reader: the decoder, reading data */
    UBYTE *data;               /* the file */
    ULONG size;
    UWORD channels;            /* of OIM_READSOUND's frames: 1 or 2 */
};
ULONG dt_instsize = sizeof(struct SndInst);

/* libnix's soft floating point (stb_vorbis and dr_mp3 use floats) calls
 * the ROM math libraries through these; opened while the library is. */
struct Library *MathIeeeDoubBasBase, *MathIeeeDoubTransBase, *MathIeeeSingBasBase;

/* dr_mp3 keeps some 16 KB of its own on the stack while it decodes a
 * frame: every decoder call runs on a stack of this size. */
#define DEC_STACK 65536

/* media.decode/1 (docs/MEDIA_DECODE.md) */
#define MD_PROBE  1
#define MD_DECODE 2
#define MD_KIND_SOUND 3

/* The PAL colour clock, for periods. */
#define PAL_CLOCK 3546895UL

static BOOL wide;    /* sound.datatype V44+: 16-bit and stereo */

BOOL dt_init(void)
{
    MathIeeeDoubBasBase = OpenLibrary((CONST_STRPTR)"mathieeedoubbas.library", 34);
    MathIeeeDoubTransBase = OpenLibrary((CONST_STRPTR)"mathieeedoubtrans.library", 34);
    MathIeeeSingBasBase = OpenLibrary((CONST_STRPTR)"mathieeesingbas.library", 34);
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
    if (MathIeeeSingBasBase) CloseLibrary(MathIeeeSingBasBase);
    if (MathIeeeDoubTransBase) CloseLibrary(MathIeeeDoubTransBase);
    if (MathIeeeDoubBasBase) CloseLibrary(MathIeeeDoubBasBase);
}

static BOOL mathOpen(void)
{
    return MathIeeeDoubBasBase && MathIeeeDoubTransBase && MathIeeeSingBasBase;
}

/* --- the decoders on this CPU, each call on a big enough stack --------------- */

struct DecCall {
    snddec *dec;
    const UBYTE *data;
    ULONG size;
    WORD *out;
    ULONG n;
    int channels;
    unsigned long rate, frames;
};

static ULONG callOpen(APTR a)
{
    struct DecCall *c = a;
    c->dec = snddec_open(c->data, c->size, &c->channels, &c->rate, &c->frames);
    return c->dec != NULL;
}

static ULONG callRead(APTR a)
{
    struct DecCall *c = a;
    return snddec_read(c->dec, (short *)c->out, c->n);
}

/* Decodes the whole sound on this CPU (no card, no Cradle): 16-bit,
 * interleaved, halved while over the rate Paula plays. FALSE with IoErr(). */
static BOOL decodeHere(const UBYTE *data, ULONG size, WORD **pcmOut, ULONG *framesOut,
                       ULONG *rateOut, ULONG *chOut, ULONG maxRate)
{
    struct DecCall c;
    WORD *pcm, *grow;
    ULONG have = 0, cap, ch, i;

    memset(&c, 0, sizeof c);
    c.data = data;
    c.size = size;
    if (!mathOpen() || !snddec_kind(data, size) || !dt_call_with_stack(DEC_STACK, callOpen, &c)) {
        SetIoErr(mathOpen() ? DTERROR_INVALID_DATA : ERROR_INVALID_RESIDENT_LIBRARY);
        return FALSE;
    }
    if (c.channels < 1 || c.channels > 2 || !c.rate) {
        snddec_close(c.dec);
        SetIoErr(DTERROR_INVALID_DATA);
        return FALSE;
    }
    ch = c.channels;
    cap = c.frames ? c.frames + 1 : c.rate * 8;
    if (!(pcm = AllocVec(cap * ch * 2, MEMF_ANY))) {
        snddec_close(c.dec);
        SetIoErr(ERROR_NO_FREE_STORE);
        return FALSE;
    }
    for (;;) {
        ULONG got;
        if (have + 4096 > cap) {               /* the length wasn't known, or was short */
            ULONG ncap = cap * 2;
            if (!(grow = AllocVec(ncap * ch * 2, MEMF_ANY))) {
                FreeVec(pcm);
                snddec_close(c.dec);
                SetIoErr(ERROR_NO_FREE_STORE);
                return FALSE;
            }
            CopyMem(pcm, grow, have * ch * 2);
            FreeVec(pcm);
            pcm = grow;
            cap = ncap;
        }
        c.out = pcm + have * ch;
        c.n = 4096;
        if (!(got = dt_call_with_stack(DEC_STACK, callRead, &c)))
            break;
        have += got;
    }
    snddec_close(c.dec);
    /* Paula's rate: halve 44.1 and 48 kHz (each pair of frames averaged) */
    while (c.rate > maxRate && !(c.rate & 1) && have >= 2) {
        for (i = 0; i < have / 2; i++) {
            ULONG k;
            for (k = 0; k < ch; k++)
                pcm[i * ch + k] = (WORD)(((LONG)pcm[2 * i * ch + k] + pcm[(2 * i + 1) * ch + k]) / 2);
        }
        have /= 2;
        c.rate /= 2;
    }
    *pcmOut = pcm;
    *framesOut = have;
    *rateOut = c.rate;
    *chOut = ch;
    return TRUE;
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
        /* no card and no paired Cradle: FLAC, Vorbis and MP3 on this CPU */
        if (!snddec_kind(data, size)) {
            FreeVec(data);
            SetIoErr(ERROR_NOT_IMPLEMENTED);
            return FALSE;
        }
        if (!decodeHere(data, size, (WORD **)&pcm, &frames, &rate, &channels, soundRate())) {
            FreeVec(data);
            return FALSE;
        }
        FreeVec(data);
        data = NULL;
        goto have;
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

have:
    if (!wide && channels == 2) {             /* 8-bit mono below: mix the two first */
        WORD *w = (WORD *)pcm;
        ULONG k;
        for (k = 0; k < frames; k++)
            w[k] = (WORD)(((LONG)w[2 * k] + w[2 * k + 1]) / 2);
        channels = 1;
    }
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
    if (data) {                                /* NULL: decoded here, no service open */
        dt_service_close(&svc);
        FreeVec(data);
    }
    if (!ok)
        SetIoErr(err);
    return ok;
}

/* OIA_Reader: the decoder on this CPU, streaming; the length for programs */
static BOOL openReader(Class *cl, Object *o)
{
    struct SndInst *si = INST_DATA(cl, o);
    struct DecCall c;
    STRPTR name = NULL;

    GetDTAttrs(o, DTA_Name, (ULONG)&name, TAG_DONE);
    if (!(si->data = dt_read_source(o, &si->size)))
        return FALSE;
    if (!snddec_kind(si->data, si->size)) {
        SetIoErr(ERROR_NOT_IMPLEMENTED);       /* a format only the service decodes */
        return FALSE;
    }
    if (!mathOpen()) {
        SetIoErr(ERROR_INVALID_RESIDENT_LIBRARY);
        return FALSE;
    }
    memset(&c, 0, sizeof c);
    c.data = si->data;
    c.size = si->size;
    if (!dt_call_with_stack(DEC_STACK, callOpen, &c) || c.channels < 1 || c.channels > 2 || !c.rate) {
        if (c.dec) snddec_close(c.dec);
        SetIoErr(DTERROR_INVALID_DATA);
        return FALSE;
    }
    si->dec = c.dec;
    si->channels = c.channels;
    SetDTAttrs(o, NULL, NULL,
        DTA_ObjName, (ULONG)(name ? FilePart(name) : (STRPTR)"Sound"),
        SDTA_SampleLength, c.frames,
        SDTA_SamplesPerSec, c.rate,
        SDTA_Period, PAL_CLOCK / c.rate,
        TAG_DONE);
    return TRUE;
}

static ULONG readSound(struct SndInst *si, struct oiReadSound *m)
{
    struct DecCall c;
    if (!si->dec || !m->oirs_Buffer) {
        SetIoErr(si->dec ? ERROR_REQUIRED_ARG_MISSING : ERROR_NOT_IMPLEMENTED);
        return 0;
    }
    memset(&c, 0, sizeof c);
    c.dec = si->dec;
    c.out = m->oirs_Buffer;
    c.n = m->oirs_Frames;
    return dt_call_with_stack(DEC_STACK, callRead, &c);
}

ULONG dt_dispatch(Class *cl, Object *o, Msg msg)
{
    switch (msg->MethodID) {
    case OM_NEW: {
        Object *obj = (Object *)DoSuperMethodA(cl, o, msg);
        BOOL reader = GetTagData(OIA_Reader, FALSE, ((struct opSet *)msg)->ops_AttrList) ? TRUE : FALSE;
        if (obj && !(reader ? openReader(cl, obj) : loadSound(cl, obj))) {
            LONG err = IoErr();
            CoerceMethod(cl, obj, OM_DISPOSE);
            SetIoErr(err);
            obj = NULL;
        }
        return (ULONG)obj;
    }
    case OM_DISPOSE: {
        struct SndInst *si = INST_DATA(cl, o);
        if (si->dec) snddec_close(si->dec);
        if (si->data) FreeVec(si->data);
        si->dec = NULL;
        si->data = NULL;
        return DoSuperMethodA(cl, o, msg);
    }
    case OM_GET: {
        struct opGet *g = (struct opGet *)msg;
        struct SndInst *si = INST_DATA(cl, o);
        if (g->opg_AttrID == OIA_Channels && si->dec) {
            *g->opg_Storage = si->channels;
            return TRUE;
        }
        return DoSuperMethodA(cl, o, msg);
    }
    case OIM_READSOUND:
        return readSound(INST_DATA(cl, o), (struct oiReadSound *)msg);
    default:
        return DoSuperMethodA(cl, o, msg);
    }
}
