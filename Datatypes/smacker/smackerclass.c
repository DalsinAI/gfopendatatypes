/*
 * smacker.datatype: Smacker films (RAD Game Tools' SMK2 and SMK4, the
 * films of many 1990s PC games) for AmigaOS 3.x, an animation.datatype
 * subclass. Decoded on the Amiga itself, a frame at a time (smk_core.c),
 * with no card or Cradle: 8-bit pixels shown through each frame's palette,
 * and the first sound track Smacker's own coding or PCM carries, handed
 * out with each frame as 8-bit mono. Bink audio tracks (SMK4) stay silent.
 *
 * From a file, or from memory (DTST_MEMORY, V44: DTA_SourceAddress and
 * DTA_SourceSize, kept by the program while the object lives), so a game
 * plays the films packed in its own archives.
 *
 * Frames are decoded in order: each draws over the one before. A frame
 * behind the last one decoded has the film decoded again from frame 0.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <string.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <dos/dos.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/modeid.h>
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
#include <proto/utility.h>
#include <clib/alib_protos.h>

#include "dtlib.h"
#include "smk_core.h"

#ifndef DTST_MEMORY
#define DTST_MEMORY 5
#endif
#ifndef DTA_SourceAddress
#define DTA_SourceAddress (DTA_Dummy + 39)
#define DTA_SourceSize    (DTA_Dummy + 40)
#endif

const char LibName[] = "smacker.datatype";
const char LibIdString[] = "smacker.datatype 47.1 (22.11.2026) AmiCradle Team";
const UWORD LibVersion = 47;
const UWORD LibRevision = 1;
static const char version[] __attribute__((used)) = "$VER: smacker.datatype 47.1 (22.11.2026)";

const char dt_superclass[] = "animation.datatype";
const UWORD dt_superversion = 40;

#define PAL_CLOCK 3546895UL
/* The highest rate Paula plays on a standard screen (period 124) */
#define PAULA_MAX 28600UL
/* Frames whose sound and colours are kept at once: animation.datatype
 * loads a few ahead, never this many. */
#define RING 64

typedef struct {
    BPTR fh;                       /* our own handle on the film, or 0 for memory */
    const UBYTE *mem;              /* DTST_MEMORY: the program's bytes */
    ULONG memSize;
    LONG pos;                      /* where fh is, -1 not known */
    struct SignalSemaphore lock;   /* the loader and OM_NEW take turns with the decoder */
    struct smk_io io;
    struct smk smk;
    int track;                     /* the track played, -1 none */
    UBYTE *pcm;                    /* a frame's sound as the track has it */
    ULONG step;                    /* every step-th sample kept, for Paula */
    struct BitMap *keyFrame;
    BYTE *ring;                    /* RING slots of soundSlot bytes in chip RAM, by frame */
    ULONG soundSlot, period;
    struct ColorMap *cmaps[RING];  /* frames whose palette is not the first's */
    struct ColorMap *firstMap;
    UBYTE firstPalette[256 * 3];
} SmkData;

ULONG dt_instsize = sizeof(SmkData);

BOOL dt_init(void)
{
    return TRUE;
}

void dt_cleanup(void)
{
}

static long fileRead(void *ctx, uint32_t at, void *buf, uint32_t n)
{
    SmkData *d = ctx;
    LONG got;
    if ((LONG)at != d->pos && Seek(d->fh, at, OFFSET_BEGINNING) < 0) {
        d->pos = -1;
        return -1;
    }
    got = Read(d->fh, buf, n);
    d->pos = got < 0 ? -1 : (LONG)(at + got);
    return got;
}

static long memRead(void *ctx, uint32_t at, void *buf, uint32_t n)
{
    SmkData *d = ctx;
    if (at > d->memSize)
        return -1;
    if (n > d->memSize - at)
        n = d->memSize - at;
    CopyMem((APTR)(d->mem + at), buf, n);
    return n;
}

static void *smkAlloc(void *ctx, uint32_t n)
{
    (void)ctx;
    return AllocVec(n, MEMF_ANY);
}

static void smkFree(void *ctx, void *p)
{
    (void)ctx;
    FreeVec(p);
}

/* The palette as the object's colours */
static void setPalette(Object *o, const UBYTE *pal)
{
    struct ColorRegister *cmap = NULL;
    ULONG *cregs = NULL, *gregs = NULL, i;

    SetDTAttrs(o, NULL, NULL, ADTA_NumColors, 256, TAG_DONE);
    GetDTAttrs(o, ADTA_ColorRegisters, (ULONG)&cmap, ADTA_CRegs, (ULONG)&cregs, ADTA_GRegs, (ULONG)&gregs, TAG_DONE);
    for (i = 0; i < 256; i++) {
        UBYTE r = pal[i * 3], g = pal[i * 3 + 1], b = pal[i * 3 + 2];
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

static void draw(SmkData *d, struct BitMap *bm)
{
    struct RastPort rp;
    InitRastPort(&rp);
    rp.BitMap = bm;
    WriteChunkyPixels(&rp, 0, 0, d->smk.width - 1, d->smk.height - 1, d->smk.pixels, d->smk.width);
}

/* The decoded frame's sound into out as signed 8-bit mono, every step-th
 * sample, channels mixed. How many bytes. */
static ULONG toMono(SmkData *d, ULONG bytes, BYTE *out)
{
    const struct smk_track *t = &d->smk.track[d->track];
    ULONG frame = (t->bits / 8) * t->channels, n = bytes / frame, i, o = 0;
    for (i = 0; i < n && o < d->soundSlot; i += d->step) {
        LONG v;
        if (t->bits == 16) {
            const WORD *s = (const WORD *)d->pcm + i * t->channels;
            v = t->channels == 2 ? ((LONG)s[0] + s[1]) >> 9 : s[0] >> 8;
        } else {
            const UBYTE *s = d->pcm + i * t->channels;
            v = t->channels == 2 ? (((LONG)s[0] + s[1]) >> 1) - 128 : (LONG)s[0] - 128;
        }
        out[o++] = (BYTE)v;
    }
    return o & ~1UL;
}

/* The frame's palette as a colour map: the first's when it is the same
 * (not NULL: a frame after one with other colours must get them back),
 * else its own, in its slot */
static struct ColorMap *frameColours(SmkData *d, ULONG index)
{
    struct ColorMap **cm = &d->cmaps[index % RING];
    const UBYTE *pal = d->smk.palette;
    ULONG i;

    if (!memcmp(pal, d->firstPalette, sizeof d->firstPalette))
        return d->firstMap;
    if (!*cm && !(*cm = GetColorMap(256)))
        return d->firstMap;
    for (i = 0; i < 256; i++)
        SetRGB32CM(*cm, i, pal[i * 3] * 0x01010101UL, pal[i * 3 + 1] * 0x01010101UL, pal[i * 3 + 2] * 0x01010101UL);
    return *cm;
}

static void freeData(SmkData *d)
{
    int i;
    if (d->keyFrame) {
        WaitBlit();
        FreeBitMap(d->keyFrame);
    }
    d->keyFrame = NULL;
    if (d->ring)
        FreeVec(d->ring);
    d->ring = NULL;
    for (i = 0; i < RING; i++)
        if (d->cmaps[i]) {
            FreeColorMap(d->cmaps[i]);
            d->cmaps[i] = NULL;
        }
    if (d->firstMap)
        FreeColorMap(d->firstMap);
    d->firstMap = NULL;
    if (d->pcm)
        FreeVec(d->pcm);
    d->pcm = NULL;
    smk_close(&d->smk);
    if (d->fh)
        Close(d->fh);
    d->fh = 0;
}

static BOOL loadFilm(Class *cl, Object *o)
{
    SmkData *d = INST_DATA(cl, o);
    ULONG sourceType = DTST_FILE, mode, i;
    BPTR given = 0, lock;
    STRPTR name = NULL;
    APTR address = NULL;
    ULONG size = 0;
    LONG err = DTERROR_INVALID_DATA;

    memset(d, 0, sizeof *d);
    InitSemaphore(&d->lock);
    d->track = -1;
    d->io.ctx = d;
    d->io.alloc = smkAlloc;
    d->io.free = smkFree;
    GetDTAttrs(o, DTA_Name, (ULONG)&name, DTA_SourceType, (ULONG)&sourceType, DTA_Handle, (ULONG)&given, TAG_DONE);
    if (sourceType == DTST_MEMORY) {
        GetDTAttrs(o, DTA_SourceAddress, (ULONG)&address, DTA_SourceSize, (ULONG)&size, TAG_DONE);
        if (!address || !size) {
            SetIoErr(ERROR_REQUIRED_ARG_MISSING);
            return FALSE;
        }
        d->mem = address;
        d->memSize = size;
        d->io.read = memRead;
    } else if (sourceType == DTST_FILE && given) {
        /* A handle of our own, for the loader process to read frames from */
        if ((lock = DupLockFromFH(given)) != 0 && !(d->fh = OpenFromLock(lock)))
            UnLock(lock);
        if (!d->fh) {
            SetIoErr(ERROR_OBJECT_WRONG_TYPE);
            return FALSE;
        }
        d->pos = -1;
        d->io.read = fileRead;
    } else {
        SetIoErr(ERROR_OBJECT_WRONG_TYPE);
        return FALSE;
    }

    if (smk_open(&d->smk, &d->io))
        goto out;
    for (i = 0; i < SMK_TRACKS; i++)
        if (d->smk.track[i].playable && d->smk.track[i].max_bytes) {
            d->track = i;
            break;
        }
    if (d->track >= 0) {
        const struct smk_track *t = &d->smk.track[d->track];
        ULONG frame = (t->bits / 8) * t->channels;
        d->step = 1;
        while (t->rate / d->step > PAULA_MAX)
            d->step++;
        d->soundSlot = ((t->max_bytes / frame) / d->step + 2) & ~1UL;
        if (!(d->pcm = AllocVec(t->max_bytes + 4, MEMF_ANY))
            || !(d->ring = AllocVec(RING * d->soundSlot, MEMF_CHIP | MEMF_CLEAR))) {
            err = ERROR_NO_FREE_STORE;
            goto out;
        }
        d->period = PAL_CLOCK / (t->rate / d->step);
        SetDTAttrs(o, NULL, NULL, ADTA_Period, d->period, ADTA_Volume, 64, ADTA_Cycles, 1, TAG_DONE);
    }
    if (!(d->keyFrame = AllocBitMap(d->smk.width, d->smk.height, 8, BMF_CLEAR, NULL))) {
        err = ERROR_NO_FREE_STORE;
        goto out;
    }
    if (smk_next(&d->smk, -1, NULL, NULL))
        goto out;
    draw(d, d->keyFrame);
    memcpy(d->firstPalette, d->smk.palette, sizeof d->firstPalette);
    if ((d->firstMap = GetColorMap(256)) != NULL)
        for (i = 0; i < 256; i++)
            SetRGB32CM(d->firstMap, i, d->firstPalette[i * 3] * 0x01010101UL,
                       d->firstPalette[i * 3 + 1] * 0x01010101UL, d->firstPalette[i * 3 + 2] * 0x01010101UL);
    setPalette(o, d->firstPalette);
    mode = (d->smk.width > 400 ? HIRES_KEY : 0) | (d->smk.height > 300 ? LORESLACE_KEY : 0);
    SetDTAttrs(o, NULL, NULL,
        ADTA_ModeID, mode,
        DTA_ObjName, (ULONG)(name ? FilePart(name) : (STRPTR)"Smacker"),
        DTA_NominalHoriz, d->smk.width,
        DTA_NominalVert, d->smk.height,
        ADTA_Width, d->smk.width,
        ADTA_Height, d->smk.height,
        ADTA_Depth, 8,
        ADTA_Frames, d->smk.frames,
        ADTA_FramesPerSecond, smk_fps(&d->smk),
        ADTA_KeyFrame, (ULONG)d->keyFrame,
        TAG_DONE);
    return TRUE;
out:
    freeData(d);
    SetIoErr(err);
    return FALSE;
}

ULONG dt_dispatch(Class *cl, Object *o, Msg msg)
{
    switch (msg->MethodID) {
    case OM_NEW: {
        Object *obj = (Object *)DoSuperMethodA(cl, o, msg);
        if (obj && !loadFilm(cl, obj)) {
            LONG err = IoErr();
            CoerceMethod(cl, obj, OM_DISPOSE);
            SetIoErr(err);
            obj = NULL;
        }
        return (ULONG)obj;
    }
    case OM_DISPOSE: {
        /* The superclass stops its loader and player first; what we own
         * is freed after it. */
        SmkData keep = *(SmkData *)INST_DATA(cl, o);
        ULONG rc = DoSuperMethodA(cl, o, msg);
        freeData(&keep);
        return rc;
    }
    case ADTM_LOADFRAME: {
        /* animation.datatype hands its bitmap back (alf_UserData set to it)
         * to be drawn into again; each is freed once, when it unloads. */
        SmkData *d = INST_DATA(cl, o);
        struct adtFrame *alf = (struct adtFrame *)msg;
        ULONG index = alf->alf_TimeStamp, bytes = 0;
        struct BitMap *bm = alf->alf_UserData;
        BOOL ok;
        if (index >= d->smk.frames)
            index = d->smk.frames - 1;
        alf->alf_Frame = index;
        alf->alf_Duration = 1;
        alf->alf_CMap = NULL;
        alf->alf_Sample = NULL;
        alf->alf_SampleLength = 0;
        if (!bm || alf->alf_BitMap != bm)
            bm = NULL;
        if (!bm && !(bm = AllocBitMap(d->smk.width, d->smk.height, 8, 0, NULL))) {
            SetIoErr(ERROR_NO_FREE_STORE);
            return 0;
        }
        ObtainSemaphore(&d->lock);
        ok = (d->smk.next == index || !smk_seek(&d->smk, index))
             && !smk_next(&d->smk, d->track, d->pcm, &bytes);
        if (ok) {
            WaitBlit();
            draw(d, bm);
            alf->alf_CMap = frameColours(d, index);
            if (d->ring && bytes) {
                BYTE *at = d->ring + (index % RING) * d->soundSlot;
                ULONG n = toMono(d, bytes, at);
                if (n) {
                    alf->alf_Sample = at;      /* ours, in the ring: freed with the object */
                    alf->alf_SampleLength = n;
                    alf->alf_Period = d->period;
                }
            }
        }
        ReleaseSemaphore(&d->lock);
        alf->alf_BitMap = bm;
        alf->alf_UserData = bm;
        return ok ? 1 : 0;
    }
    case ADTM_UNLOADFRAME: {
        struct adtFrame *alf = (struct adtFrame *)msg;
        if (alf->alf_UserData) {
            WaitBlit();
            FreeBitMap(alf->alf_UserData);
        }
        alf->alf_UserData = NULL;
        alf->alf_BitMap = NULL;
        return 0;
    }
    default:
        return DoSuperMethodA(cl, o, msg);
    }
}
