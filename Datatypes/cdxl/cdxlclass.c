/*
 * cdxl.datatype: CDXL films, the CDTV's and CD32's streaming video, for
 * AmigaOS 3.x, an animation.datatype subclass. Played on the Amiga itself,
 * a frame at a time from the file: bitplanes of 1 to 8 colours deep, HAM6
 * and HAM8, each frame with its own palette, and chunky RGB films (for
 * graphics cards) shown in 256 colours. The sound, mono or stereo, is
 * handed out with each frame as 8-bit mono.
 *
 * DTM_WRITE in raw mode (47.2) writes the film as it plays: each frame the
 * object found, in order, as it is in the file, so an intact film comes out
 * byte for byte the same. Never onto the file it plays from. IFF mode is
 * animation.datatype's.
 *
 * media.cdxl/1 (DalsinAI/openamigaservice docs/MEDIA_CDXL.md) and ToCDXL
 * make CDXL from any film; this plays them, and the CDXL on CDTV and CD32
 * discs.
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
#include <clib/alib_protos.h>

#include "dtlib.h"
#include "cdxl_core.h"

const char LibName[] = "cdxl.datatype";
const char LibIdString[] = "cdxl.datatype 47.2 (23.10.2026) AmiCradle Team";
const UWORD LibVersion = 47;
const UWORD LibRevision = 2;
static const char version[] __attribute__((used)) = "$VER: cdxl.datatype 47.2 (23.10.2026)";

const char dt_superclass[] = "animation.datatype";
const UWORD dt_superversion = 40;

/* The PAL colour clock, for periods. */
#define PAL_CLOCK 3546895UL
/* Frames whose sound and colours are kept at once: animation.datatype
 * loads a few ahead, never this many. */
#define RING 64

typedef struct {
    BPTR fh;                       /* our own handle on the film */
    struct SignalSemaphore lock;   /* the loader and OM_NEW take turns with it */
    ULONG *offsets, frames, maxSize;
    struct cdxl_frame first;
    UBYTE palette[256 * 3];        /* the first frame's */
    UBYTE depth;
    struct BitMap *keyFrame;
    BYTE *ring;                    /* RING slots of soundSlot bytes in chip RAM, by frame */
    ULONG soundSlot, period;
    struct ColorMap *cmaps[RING];  /* frames whose palette is not the first's */
} CdxlData;

ULONG dt_instsize = sizeof(CdxlData);

BOOL dt_init(void)
{
    return TRUE;
}

void dt_cleanup(void)
{
}

/* The 6x6x6 cube for chunky films, else the given palette. */
static void setPalette(Object *o, const UBYTE *pal, ULONG n)
{
    struct ColorRegister *cmap = NULL;
    ULONG *cregs = NULL, *gregs = NULL, i;

    SetDTAttrs(o, NULL, NULL, ADTA_NumColors, n, TAG_DONE);
    GetDTAttrs(o, ADTA_ColorRegisters, (ULONG)&cmap, ADTA_CRegs, (ULONG)&cregs, ADTA_GRegs, (ULONG)&gregs, TAG_DONE);
    for (i = 0; i < n; i++) {
        UBYTE r, g, b;
        if (pal) {
            r = pal[i * 3];
            g = pal[i * 3 + 1];
            b = pal[i * 3 + 2];
        } else if (i < 216) {
            r = (i / 36) * 51;
            g = ((i / 6) % 6) * 51;
            b = (i % 6) * 51;
        } else
            r = g = b = 0;
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

/* Frame index read from the film, whole; NULL when it can't be. */
static UBYTE *readFrame(CdxlData *d, ULONG index, struct cdxl_frame *f)
{
    UBYTE *frame = AllocVec(d->maxSize, MEMF_ANY);
    BOOL ok = FALSE;

    if (!frame)
        return NULL;
    ObtainSemaphore(&d->lock);
    if (Seek(d->fh, d->offsets[index], OFFSET_BEGINNING) >= 0 && Read(d->fh, frame, CDXL_HEAD) == CDXL_HEAD
        && !cdxl_parse(frame, f) && f->size <= d->maxSize
        && Read(d->fh, frame + CDXL_HEAD, f->size - CDXL_HEAD) == (LONG)(f->size - CDXL_HEAD))
        ok = TRUE;
    ReleaseSemaphore(&d->lock);
    if (!ok) {
        FreeVec(frame);
        frame = NULL;
    }
    return frame;
}

/* The frame's picture into bm: bitplanes copied row by row into a standard
 * bitmap, else (a graphics card's) through WriteChunkyPixels. */
static BOOL drawFrame(CdxlData *d, const UBYTE *frame, const struct cdxl_frame *f, struct BitMap *bm)
{
    struct RastPort rp;
    UBYTE *chunky;
    ULONG p, y, row = cdxl_rowbytes(f);

    if (f->layout != CDXL_CHUNKY && (GetBitMapAttr(bm, BMA_FLAGS) & BMF_STANDARD) && bm->Depth >= f->planes
        && bm->BytesPerRow >= row) {
        const UBYTE *img = frame + cdxl_image_at(f);
        for (p = 0; p < f->planes; p++)
            for (y = 0; y < f->height; y++)
                CopyMem((APTR)(img + cdxl_plane_row(f, p, y)), bm->Planes[p] + y * bm->BytesPerRow, row);
        for (; p < bm->Depth; p++)             /* planes the film does not have */
            for (y = 0; y < f->height; y++)
                memset(bm->Planes[p] + y * bm->BytesPerRow, 0, row);
        return TRUE;
    }
    if (!(chunky = AllocVec((ULONG)f->width * f->height, MEMF_ANY)))
        return FALSE;
    if (f->layout == CDXL_CHUNKY)
        cdxl_rgb_to_cube(frame, f, chunky);
    else
        cdxl_to_chunky(frame, f, chunky);
    InitRastPort(&rp);
    rp.BitMap = bm;
    WriteChunkyPixels(&rp, 0, 0, f->width - 1, f->height - 1, chunky, f->width);
    FreeVec(chunky);
    (void)d;
    return TRUE;
}

/* The frame's palette, when it is not the first's: into its slot's colour map. */
static struct ColorMap *frameColours(CdxlData *d, ULONG index, const UBYTE *frame, const struct cdxl_frame *f)
{
    UBYTE pal[256 * 3];
    struct ColorMap **cm = &d->cmaps[index % RING];
    ULONG i;

    if (f->layout == CDXL_CHUNKY || !f->colours)
        return NULL;
    cdxl_palette(frame, f, pal);
    if (f->colours == d->first.colours && !memcmp(pal, d->palette, f->colours * 3))
        return NULL;
    if (!*cm && !(*cm = GetColorMap(256)))
        return NULL;
    for (i = 0; i < f->colours; i++)
        SetRGB32CM(*cm, i, pal[i * 3] * 0x01010101UL, pal[i * 3 + 1] * 0x01010101UL, pal[i * 3 + 2] * 0x01010101UL);
    return *cm;
}

static void freeData(CdxlData *d)
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
    if (d->offsets)
        FreeVec(d->offsets);
    d->offsets = NULL;
    if (d->fh)
        Close(d->fh);
    d->fh = 0;
}

/* Walks the film's headers, noting where each frame starts. Frames that
 * change size, depth or layout end it, as a cut-short file does. */
static BOOL scanFilm(CdxlData *d, ULONG *maxAudio)
{
    UBYTE head[CDXL_HEAD];
    struct cdxl_frame f;
    ULONG at = 0, cap = 0;

    *maxAudio = 0;
    for (;;) {
        if (Seek(d->fh, at, OFFSET_BEGINNING) < 0 || Read(d->fh, head, CDXL_HEAD) != CDXL_HEAD || cdxl_parse(head, &f))
            break;
        if (d->frames && (f.width != d->first.width || f.height != d->first.height || f.planes != d->first.planes
                          || f.layout != d->first.layout || f.ham != d->first.ham))
            break;
        if (d->frames == cap) {
            ULONG *more = AllocVec((cap = cap ? cap * 2 : 256) * sizeof *more, MEMF_ANY);
            if (!more)
                break;
            if (d->offsets) {
                CopyMem(d->offsets, more, d->frames * sizeof *more);
                FreeVec(d->offsets);
            }
            d->offsets = more;
        }
        if (!d->frames)
            d->first = f;
        d->offsets[d->frames++] = at;
        if (f.size > d->maxSize)
            d->maxSize = f.size;
        if (f.audio > *maxAudio)
            *maxAudio = f.audio;
        at += f.size;
    }
    return d->frames != 0;
}

static BOOL loadFilm(Class *cl, Object *o)
{
    CdxlData *d = INST_DATA(cl, o);
    ULONG sourceType = DTST_FILE, maxAudio, mode = 0, n;
    BPTR given = 0, lock;
    STRPTR name = NULL;
    struct cdxl_frame f;
    UBYTE *frame;
    LONG err = DTERROR_INVALID_DATA;

    memset(d, 0, sizeof *d);
    InitSemaphore(&d->lock);
    GetDTAttrs(o, DTA_Name, (ULONG)&name, DTA_SourceType, (ULONG)&sourceType, DTA_Handle, (ULONG)&given, TAG_DONE);
    if (sourceType != DTST_FILE || !given) {
        SetIoErr(ERROR_OBJECT_WRONG_TYPE);
        return FALSE;
    }
    /* A handle of our own, for the loader process to read frames from
     * while the film plays. */
    if ((lock = DupLockFromFH(given)) != 0 && !(d->fh = OpenFromLock(lock)))
        UnLock(lock);
    if (!d->fh) {
        SetIoErr(ERROR_OBJECT_WRONG_TYPE);
        return FALSE;
    }
    if (!scanFilm(d, &maxAudio))
        goto out;
    d->depth = d->first.layout == CDXL_CHUNKY ? 8 : d->first.planes;
    if (!(frame = readFrame(d, 0, &f)))
        goto out;
    if (f.layout != CDXL_CHUNKY)
        cdxl_palette(frame, &f, d->palette);
    if (!(d->keyFrame = AllocBitMap(f.width, f.height, d->depth, BMF_CLEAR, NULL))) {
        FreeVec(frame);
        err = ERROR_NO_FREE_STORE;
        goto out;
    }
    drawFrame(d, frame, &f, d->keyFrame);
    FreeVec(frame);

    if (maxAudio) {
        d->soundSlot = (maxAudio + 1) & ~1UL;
        if ((d->ring = AllocVec(RING * d->soundSlot, MEMF_CHIP | MEMF_CLEAR)) != NULL) {
            d->period = PAL_CLOCK / cdxl_rate(&d->first);
            SetDTAttrs(o, NULL, NULL, ADTA_Period, d->period, ADTA_Volume, 64, ADTA_Cycles, 1, TAG_DONE);
        }
    }
    n = f.layout == CDXL_CHUNKY ? 256 : f.colours;
    setPalette(o, f.layout == CDXL_CHUNKY ? NULL : d->palette, n);
    if (f.layout != CDXL_CHUNKY) {
        mode = (f.ham ? HAM_KEY : 0) | (f.width > 400 ? HIRES_KEY : 0) | (f.height > 300 ? LORESLACE_KEY : 0);
        SetDTAttrs(o, NULL, NULL, ADTA_ModeID, mode, TAG_DONE);
    }
    SetDTAttrs(o, NULL, NULL,
        DTA_ObjName, (ULONG)(name ? FilePart(name) : (STRPTR)"CDXL"),
        DTA_NominalHoriz, f.width,
        DTA_NominalVert, f.height,
        ADTA_Width, f.width,
        ADTA_Height, f.height,
        ADTA_Depth, d->depth,
        ADTA_Frames, d->frames,
        ADTA_FramesPerSecond, cdxl_fps(&d->first),
        ADTA_KeyFrame, (ULONG)d->keyFrame,
        TAG_DONE);
    return TRUE;
out:
    freeData(d);
    SetIoErr(err);
    return FALSE;
}

/* DTM_WRITE, DTWM_RAW: the film's frames, in order, as they are in the file.
 * 0, or an IoErr() code. */
static LONG writeFilm(CdxlData *d, BPTR out)
{
    struct cdxl_frame f;
    BPTR a, b;
    ULONG i;
    LONG err = 0;

    if (!out)
        return ERROR_REQUIRED_ARG_MISSING;
    if (!d->fh || !d->frames)
        return ERROR_OBJECT_WRONG_TYPE;
    /* never onto the film it plays from (AmigaDOS's locks stop most of these
     * already; a handle opened with MODE_READWRITE would not be stopped) */
    a = DupLockFromFH(out);
    b = DupLockFromFH(d->fh);
    if (a && b && SameLock(a, b) == LOCK_SAME)
        err = ERROR_OBJECT_IN_USE;
    if (a)
        UnLock(a);
    if (b)
        UnLock(b);
    for (i = 0; !err && i < d->frames; i++) {
        UBYTE *frame = readFrame(d, i, &f);
        if (!frame)
            err = IoErr() ? IoErr() : ERROR_SEEK_ERROR;
        else {
            if (Write(out, frame, f.size) != (LONG)f.size)
                err = IoErr() ? IoErr() : ERROR_DISK_FULL;
            FreeVec(frame);
        }
    }
    return err;
}

ULONG dt_dispatch(Class *cl, Object *o, Msg msg)
{
    switch (msg->MethodID) {
    case DTM_WRITE: {
        struct dtWrite *w = (struct dtWrite *)msg;
        LONG err;
        if (w->dtw_Mode != DTWM_RAW)
            return DoSuperMethodA(cl, o, msg);
        if ((err = writeFilm(INST_DATA(cl, o), w->dtw_FileHandle))) {
            SetIoErr(err);
            return FALSE;
        }
        return TRUE;
    }
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
        CdxlData keep = *(CdxlData *)INST_DATA(cl, o);
        ULONG rc = DoSuperMethodA(cl, o, msg);
        freeData(&keep);
        return rc;
    }
    case ADTM_LOADFRAME: {
        /* As gfopenvideo.datatype: animation.datatype 47 hands bitmaps back
         * in alf_BitMap to be drawn again; alf_UserData follows a bitmap
         * and it is freed once, when the frame holding it last unloads. */
        CdxlData *d = INST_DATA(cl, o);
        struct adtFrame *alf = (struct adtFrame *)msg;
        ULONG index = alf->alf_TimeStamp;
        struct BitMap *bm = alf->alf_BitMap;
        struct cdxl_frame f;
        UBYTE *frame;
        BOOL drawn = FALSE;
        if (index >= d->frames)
            index = d->frames - 1;
        alf->alf_Frame = index;
        alf->alf_Duration = 1;
        alf->alf_CMap = NULL;
        alf->alf_Sample = NULL;
        alf->alf_SampleLength = 0;
        if (!bm)
            bm = AllocBitMap(d->first.width, d->first.height, d->depth, BMF_CLEAR, NULL);
        if (!bm) {
            SetIoErr(ERROR_NO_FREE_STORE);
            return 0;
        }
        if ((frame = readFrame(d, index, &f)) != NULL) {
            drawn = drawFrame(d, frame, &f, bm);
            alf->alf_CMap = frameColours(d, index, frame, &f);
            if (d->ring && f.audio && f.audio <= d->soundSlot) {
                BYTE *at = d->ring + (index % RING) * d->soundSlot;
                cdxl_mono(frame, &f, (int8_t *)at);
                alf->alf_Sample = at;          /* ours, in the ring: freed with the object */
                alf->alf_SampleLength = f.audio;
                alf->alf_Period = d->period;
            }
            FreeVec(frame);
        }
        alf->alf_BitMap = bm;
        alf->alf_UserData = bm;
        return drawn ? 1 : 0;
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
