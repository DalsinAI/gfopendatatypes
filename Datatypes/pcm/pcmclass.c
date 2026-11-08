/*
 * gfopenaiff.datatype and gfopenwave.datatype (this file built twice, with
 * PCM_CLASS_WAVE for the second): AIFF/AIFC and WAV sounds for AmigaOS
 * 3.x, read and written, sound.datatype subclasses.
 *
 * Read: 8, 16, 24 and 32-bit PCM, mono to 8 channels (more than two are
 * taken as their first two), AIFC "sowt" and "fl32" and WAV's float, kept
 * as 16 bits (pcmcore.c). With sound.datatype V44 or newer the sound plays
 * in 16-bit stereo; older ones get 8-bit, stereo through SDTA_LeftSample
 * and SDTA_RightSample.
 *
 * For programs (datatypes/gfopenimage.h): opened with OIA_Reader the file
 * is not read whole: OIM_READSOUND streams it from the file. A DTST_RAM
 * object made with OIA_PCM holds a program's sound. DTM_WRITE with
 * DTWM_RAW writes the sound as AIFF (gfopenaiff) or WAV (gfopenwave), 16
 * or 8 bits (OIA_SampleBits), or only its samples (OIA_Headerless);
 * DTWM_IFF goes to sound.datatype, which writes 8SVX.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <string.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/soundclass.h>
#include <intuition/classes.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/datatypes.h>
#include <clib/alib_protos.h>
#include <datatypes/gfopenimage.h>

#include "dtlib.h"
#include "pcmcore.h"

#ifdef PCM_CLASS_WAVE
#define CONTAINER PCM_WAVE
const char LibName[] = "gfopenwave.datatype";
const char LibIdString[] = "gfopenwave.datatype 47.1 (8.11.2026) AmiCradle Team";
static const char version[] __attribute__((used)) = "$VER: gfopenwave.datatype 47.1 (8.11.2026)";
#else
#define CONTAINER PCM_AIFF
const char LibName[] = "gfopenaiff.datatype";
const char LibIdString[] = "gfopenaiff.datatype 47.1 (8.11.2026) AmiCradle Team";
static const char version[] __attribute__((used)) = "$VER: gfopenaiff.datatype 47.1 (8.11.2026)";
#endif
const UWORD LibVersion = 47;
const UWORD LibRevision = 1;

const char dt_superclass[] = "sound.datatype";
const UWORD dt_superversion = 39;

#define PAL_CLOCK 3546895UL
#define CHUNK 4096                             /* frames a write or a read moves at once */
#define PIECE 512                              /* frames read from the file at once */

struct PcmInst {
    WORD *pcm;                 /* the sound, 16-bit, interleaved; NULL when reading */
    ULONG frames, rate;
    UWORD channels;            /* 1 or 2 */
    UWORD reader;
    BPTR fh;                   /* OIA_Reader: the file datatypes.library opened */
    pcm_info pi;
    ULONG pos;                 /* OIM_READSOUND's next frame */
    UBYTE *io;                 /* a piece of the file: PIECE frames of 8 channels of 32 bits */
    WORD *tmp;                 /* that piece as 16 bits, all its channels */
};
ULONG dt_instsize = sizeof(struct PcmInst);

static BOOL wide;              /* sound.datatype V44+: 16-bit and stereo */

BOOL dt_init(void)
{
    struct Library *sound = OpenLibrary((CONST_STRPTR)"datatypes/sound.datatype", 44);
    if (sound) {
        wide = TRUE;
        CloseLibrary(sound);
    }
    return TRUE;
}

void dt_cleanup(void)
{
}

static unsigned long readAt(void *h, unsigned long off, void *buf, unsigned long n)
{
    BPTR fh = (BPTR)h;
    LONG got;
    if (Seek(fh, (LONG)off, OFFSET_BEGINNING) < 0)
        return 0;
    got = Read(fh, buf, (LONG)n);
    return got > 0 ? (unsigned long)got : 0;
}

/* The file's frames from frame pos, as 16-bit stereo or mono (the first
 * two of more channels); n at most CHUNK. */
static ULONG readFrames(struct PcmInst *pi, WORD *out, ULONG pos, ULONG n)
{
    const pcm_info *f = &pi->pi;
    ULONG done = 0, i, c;
    while (done < n) {
        ULONG m = n - done < PIECE ? n - done : PIECE, got;
        got = readAt((void *)pi->fh, f->data + (pos + done) * f->frame_bytes, pi->io, m * f->frame_bytes);
        m = got / f->frame_bytes;
        if (!m) break;
        pcm_to16(f, pi->io, (short *)pi->tmp, m);
        for (i = 0; i < m; i++)
            for (c = 0; c < pi->channels; c++)
                *out++ = pi->tmp[i * f->channels + c];
        done += m;
    }
    return done;
}

/* What sound.datatype plays: a copy, 16-bit (V44) or 8-bit. */
static BOOL givePlayback(Object *o, struct PcmInst *pi, STRPTR name)
{
    ULONG n = pi->frames * pi->channels, i;
    if (wide) {
        WORD *p = AllocVec(n ? n * 2 : 2, MEMF_ANY);
        if (!p) return FALSE;
        CopyMem(pi->pcm, p, n * 2);
        SetDTAttrs(o, NULL, NULL,
            DTA_ObjName, (ULONG)name,
            SDTA_SampleType, pi->channels == 2 ? SDTST_S16S : SDTST_M16S,
            SDTA_Sample, (ULONG)p,
            SDTA_SampleLength, pi->frames,
            SDTA_Frequency, pi->rate,
            SDTA_SamplesPerSec, pi->rate,
            SDTA_Period, PAL_CLOCK / pi->rate,
            SDTA_Volume, 64,
            SDTA_Cycles, 1,
            TAG_DONE);
        return TRUE;
    }
    if (pi->channels == 2) {
        BYTE *l = AllocVec(pi->frames ? pi->frames : 1, MEMF_ANY), *r = AllocVec(pi->frames ? pi->frames : 1, MEMF_ANY);
        if (!l || !r) { if (l) FreeVec(l); if (r) FreeVec(r); return FALSE; }
        for (i = 0; i < pi->frames; i++) {
            l[i] = (BYTE)(pi->pcm[i * 2] >> 8);
            r[i] = (BYTE)(pi->pcm[i * 2 + 1] >> 8);
        }
        SetDTAttrs(o, NULL, NULL,
            DTA_ObjName, (ULONG)name,
            SDTA_LeftSample, (ULONG)l,
            SDTA_RightSample, (ULONG)r,
            SDTA_SampleLength, pi->frames,
            SDTA_SamplesPerSec, pi->rate,
            SDTA_Period, PAL_CLOCK / pi->rate,
            SDTA_Volume, 64,
            SDTA_Cycles, 1,
            TAG_DONE);
        return TRUE;
    } else {
        BYTE *s = AllocVec(pi->frames ? pi->frames : 1, MEMF_ANY);
        if (!s) return FALSE;
        for (i = 0; i < pi->frames; i++)
            s[i] = (BYTE)(pi->pcm[i] >> 8);
        SetDTAttrs(o, NULL, NULL,
            DTA_ObjName, (ULONG)name,
            SDTA_Sample, (ULONG)s,
            SDTA_SampleLength, pi->frames,
            SDTA_SamplesPerSec, pi->rate,
            SDTA_Period, PAL_CLOCK / pi->rate,
            SDTA_Volume, 64,
            SDTA_Cycles, 1,
            TAG_DONE);
        return TRUE;
    }
}

/* OM_NEW's work: a program's sound (DTST_RAM) or the file. FALSE with
 * the reason in IoErr(). */
static BOOL load(Class *cl, Object *o, struct TagItem *attrs)
{
    struct PcmInst *pi = INST_DATA(cl, o);
    ULONG type = DTST_FILE, size;
    STRPTR name = NULL;
    BPTR fh = 0;
    LONG len;

    GetDTAttrs(o, DTA_SourceType, (ULONG)&type, DTA_Handle, (ULONG)&fh, DTA_Name, (ULONG)&name, TAG_DONE);
    if (type == DTST_RAM) {
        const WORD *pcm = (const WORD *)GetTagData(OIA_PCM, 0, attrs);
        pi->frames = GetTagData(SDTA_SampleLength, 0, attrs);
        pi->rate = GetTagData(SDTA_SamplesPerSec, GetTagData(SDTA_Frequency, 0, attrs), attrs);
        pi->channels = GetTagData(OIA_Channels, 1, attrs) == 2 ? 2 : 1;
        if (!pcm)                               /* an empty object, to be given nothing */
            return TRUE;
        if (!pi->rate || pi->frames > 0x3fffffffUL / pi->channels) {
            SetIoErr(DTERROR_INVALID_DATA);
            return FALSE;
        }
        if (!(pi->pcm = AllocVec(pi->frames * pi->channels * 2 + 2, MEMF_ANY))) {
            SetIoErr(ERROR_NO_FREE_STORE);
            return FALSE;
        }
        CopyMem((APTR)pcm, pi->pcm, pi->frames * pi->channels * 2);
        if (!givePlayback(o, pi, (STRPTR)"Sound")) {
            SetIoErr(ERROR_NO_FREE_STORE);
            return FALSE;
        }
        return TRUE;
    }
    if (type != DTST_FILE || !fh) {
        SetIoErr(ERROR_OBJECT_WRONG_TYPE);
        return FALSE;
    }
    if (Seek(fh, 0, OFFSET_END) < 0 || (len = Seek(fh, 0, OFFSET_BEGINNING)) < 0)
        return FALSE;
    size = (ULONG)len;
    if (pcm_parse(readAt, (void *)fh, size, &pi->pi)) {
        SetIoErr(DTERROR_INVALID_DATA);
        return FALSE;
    }
    pi->fh = fh;
    pi->frames = pi->pi.frames;
    pi->rate = pi->pi.rate;
    pi->channels = pi->pi.channels >= 2 ? 2 : 1;
    if (!(pi->io = AllocVec(PIECE * 8 * 4, MEMF_ANY)) || !(pi->tmp = AllocVec(PIECE * 8 * 2, MEMF_ANY))) {
        SetIoErr(ERROR_NO_FREE_STORE);
        return FALSE;
    }
    pi->reader = GetTagData(OIA_Reader, FALSE, attrs) ? 1 : 0;
    if (pi->reader) {                         /* streamed by OIM_READSOUND; the length for programs */
        SetDTAttrs(o, NULL, NULL,
            DTA_ObjName, (ULONG)(name ? FilePart(name) : (STRPTR)"Sound"),
            SDTA_SampleLength, pi->frames,
            SDTA_SamplesPerSec, pi->rate,
            SDTA_Period, PAL_CLOCK / pi->rate,
            TAG_DONE);
        return TRUE;
    }
    if (pi->frames > 0x3fffffffUL / pi->channels ||
        !(pi->pcm = AllocVec(pi->frames * pi->channels * 2 + 2, MEMF_ANY))) {
        SetIoErr(ERROR_NO_FREE_STORE);
        return FALSE;
    }
    {
        ULONG at = 0;
        while (at < pi->frames) {
            ULONG n = pi->frames - at < CHUNK ? pi->frames - at : CHUNK;
            ULONG got = readFrames(pi, pi->pcm + at * pi->channels, at, n);
            if (!got) break;
            at += got;
        }
        pi->frames = at;                       /* a file that ends early: what there is */
    }
    FreeVec(pi->io);
    FreeVec(pi->tmp);
    pi->io = NULL;
    pi->tmp = NULL;
    pi->fh = 0;
    if (!givePlayback(o, pi, name ? FilePart(name) : (STRPTR)"Sound")) {
        SetIoErr(ERROR_NO_FREE_STORE);
        return FALSE;
    }
    return TRUE;
}

static ULONG readSound(struct PcmInst *pi, struct oiReadSound *m)
{
    ULONG n = m->oirs_Frames, got;
    if (!m->oirs_Buffer) {
        SetIoErr(ERROR_REQUIRED_ARG_MISSING);
        return 0;
    }
    if (pi->pos >= pi->frames)
        return 0;
    if (n > pi->frames - pi->pos)
        n = pi->frames - pi->pos;
    if (pi->reader) {
        got = 0;
        while (got < n) {
            ULONG k = n - got < CHUNK ? n - got : CHUNK, r;
            r = readFrames(pi, m->oirs_Buffer + got * pi->channels, pi->pos + got, k);
            if (!r) break;
            got += r;
        }
    } else if (pi->pcm) {
        CopyMem(pi->pcm + pi->pos * pi->channels, m->oirs_Buffer, n * pi->channels * 2);
        got = n;
    } else
        got = 0;
    pi->pos += got;
    return got;
}

/* DTM_WRITE, DTWM_RAW: the sound as AIFF or WAV (or headerless). 0, or an
 * IoErr() code. */
static LONG writeSound(struct PcmInst *pi, struct dtWrite *w)
{
    ULONG bits = GetTagData(OIA_SampleBits, 16, w->dtw_AttrList) == 8 ? 8 : 16;
    BOOL bare = GetTagData(OIA_Headerless, FALSE, w->dtw_AttrList) ? TRUE : FALSE;
    UBYTE head[PCM_HEADER_MAX], *buf;
    ULONG at = 0;
    LONG err = 0;

    if (!pi->pcm)
        return pi->reader ? ERROR_NOT_IMPLEMENTED : DTERROR_NOT_ENOUGH_DATA;
    if (!w->dtw_FileHandle)
        return ERROR_REQUIRED_ARG_MISSING;
    if (!bare) {
        LONG n = pcm_header(CONTAINER, pi->channels, pi->rate, pi->frames, (int)bits, head);
        if (Write(w->dtw_FileHandle, head, n) != n)
            return IoErr();
    }
    if (!(buf = AllocVec(CHUNK * 2 * 2, MEMF_ANY)))
        return ERROR_NO_FREE_STORE;
    while (at < pi->frames) {
        ULONG n = pi->frames - at < CHUNK ? pi->frames - at : CHUNK;
        LONG bytes = (LONG)pcm_encode(CONTAINER, pi->channels, (int)bits, pi->pcm + at * pi->channels, n, buf);
        if (Write(w->dtw_FileHandle, buf, bytes) != bytes) {
            err = IoErr();
            break;
        }
        at += n;
    }
    if (!err && !bare && pcm_pad(pi->channels, pi->frames, (int)bits)) {
        buf[0] = 0;
        if (Write(w->dtw_FileHandle, buf, 1) != 1)
            err = IoErr();
    }
    FreeVec(buf);
    return err;
}

static void freeInst(struct PcmInst *pi)
{
    if (pi->pcm) FreeVec(pi->pcm);
    if (pi->io) FreeVec(pi->io);
    if (pi->tmp) FreeVec(pi->tmp);
    pi->pcm = NULL;
    pi->io = NULL;
    pi->tmp = NULL;
}

ULONG dt_dispatch(Class *cl, Object *o, Msg msg)
{
    switch (msg->MethodID) {
    case OM_NEW: {
        Object *obj = (Object *)DoSuperMethodA(cl, o, msg);
        if (obj && !load(cl, obj, ((struct opSet *)msg)->ops_AttrList)) {
            LONG err = IoErr();
            CoerceMethod(cl, obj, OM_DISPOSE);
            SetIoErr(err);
            obj = NULL;
        }
        return (ULONG)obj;
    }
    case OM_DISPOSE:
        freeInst(INST_DATA(cl, o));
        return DoSuperMethodA(cl, o, msg);
    case OM_GET: {
        struct opGet *g = (struct opGet *)msg;
        struct PcmInst *pi = INST_DATA(cl, o);
        if (g->opg_AttrID == OIA_Channels) {
            *g->opg_Storage = pi->channels;
            return TRUE;
        }
        return DoSuperMethodA(cl, o, msg);
    }
    case OIM_READSOUND:
        return readSound(INST_DATA(cl, o), (struct oiReadSound *)msg);
    case DTM_WRITE: {
        struct dtWrite *w = (struct dtWrite *)msg;
        LONG err;
        if (w->dtw_Mode != DTWM_RAW)
            return DoSuperMethodA(cl, o, msg);  /* DTWM_IFF: sound.datatype's 8SVX */
        err = writeSound(INST_DATA(cl, o), w);
        SetIoErr(err);
        return err ? FALSE : TRUE;
    }
    default:
        return DoSuperMethodA(cl, o, msg);
    }
}
