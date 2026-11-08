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
 * animation.datatype's, and so is raw mode with OIA_WriteFormat (an MP4,
 * datatypes/gfopenimage.h).
 *
 * media.cdxl/1 (DalsinAI/gfopenservice docs/MEDIA_CDXL.md) and ToCDXL
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
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include <datatypes/gfopenimage.h>

#include "dtlib.h"
#include "cdxl_core.h"

const char LibName[] = "cdxl.datatype";
const char LibIdString[] = "cdxl.datatype 47.3 (7.11.2026) AmiCradle Team";
const UWORD LibVersion = 47;
const UWORD LibRevision = 3;
static const char version[] __attribute__((used)) = "$VER: cdxl.datatype 47.3 (7.11.2026)";

const char dt_superclass[] = "animation.datatype";
const UWORD dt_superversion = 40;

/* The PAL colour clock, for periods. */
#define PAL_CLOCK 3546895UL
/* Frames whose sound and colours are kept at once: animation.datatype
 * loads a few ahead, never this many. */
#define RING 64

/* GFOpenDatatypes' animation.datatype shows frames from any memory (it
 * remaps them, or copies them to Chip RAM first): OM_GET answers TRUE */
#define ADT_GFA_FramesAnyMem (TAG_USER + 0x47460001UL)

typedef struct {
    BPTR fh;                       /* our own handle on the film */
    struct SignalSemaphore lock;   /* the loader and OM_NEW take turns with it (the handle, chunky) */
    struct cdxl_index ix;          /* where the frames are (cdxl_index_open) */
    struct cdxl_io io;             /* over fh */
    LONG pos;                      /* where fh is, -1 not known: no Seek when a read follows the last */
    struct cdxl_frame first;
    UBYTE palette[256 * 3];        /* the first frame's */
    UBYTE depth, anyMem;           /* anyMem: the superclass shows frames from any memory */
    struct BitMap *keyFrame;
    BYTE *ring;                    /* RING slots of soundSlot bytes in chip RAM, by frame */
    ULONG soundSlot, period;
    struct ColorMap *cmaps[RING];  /* frames whose palette is not the first's */
    struct ColorMap *firstMap;     /* the first's palette, for frames with it */
    UBYTE *chunky;                 /* a chunky film's frame as cube indices, width x height */
} CdxlData;

/* A frame handed out (alf_UserData): the frame read whole into buf, whose
 * bitplanes are bm's planes as they lie in the file (no copy), in Chip RAM
 * unless the superclass remaps the frame anyway; or a chunky film's frame,
 * dithered into an 8-plane bitmap. animation.datatype hands it back to be
 * read into again. */
struct CdxlFrame {
    struct BitMap bm;
    UBYTE *buf;
    ULONG size, memf;
    struct BitMap *chunky;
};

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

/* The film through fh, for cdxl_core: a read where the last one ended
 * needs no Seek (one DOS packet a frame in order) */
static long dosRead(void *ctx, uint32_t at, void *buf, uint32_t n)
{
    CdxlData *d = ctx;
    LONG got;
    if ((LONG)at != d->pos && Seek(d->fh, at, OFFSET_BEGINNING) < 0) {
        d->pos = -1;
        return -1;
    }
    got = Read(d->fh, buf, n);
    d->pos = got < 0 ? -1 : (LONG)(at + got);
    return got;
}

static long dosSize(void *ctx)
{
    CdxlData *d = ctx;
    LONG len;
    d->pos = -1;
    if (Seek(d->fh, 0, OFFSET_END) < 0 || (len = Seek(d->fh, 0, OFFSET_BEGINNING)) < 0)
        return -1;
    d->pos = 0;
    return len;
}

static void *dosAlloc(void *ctx, uint32_t n)
{
    (void)ctx;
    return AllocVec(n, MEMF_ANY);
}

static void dosFree(void *ctx, void *p)
{
    (void)ctx;
    FreeVec(p);
}

/* Frame index read from the film, whole, into buf (d->ix.max_size bytes) */
static BOOL readFrame(CdxlData *d, ULONG index, UBYTE *buf, struct cdxl_frame *f)
{
    BOOL ok;
    ObtainSemaphore(&d->lock);
    ok = !cdxl_read_frame(&d->io, &d->ix, index, buf, f);
    ReleaseSemaphore(&d->lock);
    return ok;
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

/* The slot's bitmap made over its buffer: the frame's planes where they
 * are, one after another (CDXL_PLANAR) or each row's in turn (CDXL_LINE,
 * an interleaved bitmap) */
static void frameBitMap(struct CdxlFrame *s, const struct cdxl_frame *f)
{
    ULONG row = cdxl_rowbytes(f), p;
    UBYTE *img = s->buf + cdxl_image_at(f);
    InitBitMap(&s->bm, f->planes, f->width, f->height);
    if (f->layout == CDXL_LINE)
        s->bm.BytesPerRow = (UWORD)(row * f->planes);
    for (p = 0; p < 8; p++)
        s->bm.Planes[p] = p < f->planes ? img + (f->layout == CDXL_LINE ? p * row : p * f->height * row) : NULL;
}

static void freeFrame(struct CdxlFrame *s)
{
    if (!s)
        return;
    WaitBlit();
    if (s->chunky)
        FreeBitMap(s->chunky);
    if (s->buf)
        FreeVec(s->buf);
    FreeVec(s);
}

/* Frame index into the slot s: read whole into its buffer (one DOS read in
 * order); a chunky film's dithered into its 8-plane bitmap. Its header
 * into *f. */
static BOOL loadInto(CdxlData *d, Object *o, struct CdxlFrame *s, ULONG index, struct cdxl_frame *f)
{
    ULONG remap = FALSE, memf = MEMF_CHIP;
    BOOL ok;
    /* Fast RAM when it is only read by the CPU: animation.datatype ours,
     * and remapping (no HAM) or a chunky film's */
    if (d->first.layout == CDXL_CHUNKY
        || (d->anyMem && !d->first.ham && GetDTAttrs(o, ADTA_Remap, (ULONG)&remap, TAG_DONE) == 1 && remap))
        memf = MEMF_ANY;
    if (!s->buf || s->size < d->ix.max_size || (s->memf != memf && memf == MEMF_CHIP)) {
        WaitBlit();
        if (s->buf)
            FreeVec(s->buf);
        s->size = 0;
        if (!(s->buf = AllocVec(d->ix.max_size, memf)))
            return FALSE;
        s->size = d->ix.max_size;
        s->memf = memf;
    } else
        WaitBlit();                     /* a blit of the frame before may still read it */
    if (d->first.layout == CDXL_CHUNKY && !s->chunky
        && !(s->chunky = AllocBitMap(d->first.width, d->first.height, 8, 0, NULL)))
        return FALSE;
    ObtainSemaphore(&d->lock);
    ok = !cdxl_read_frame(&d->io, &d->ix, index, s->buf, f);
    if (ok && f->layout == CDXL_CHUNKY) {
        struct RastPort rp;
        cdxl_rgb_to_cube(s->buf, f, d->chunky);
        InitRastPort(&rp);
        rp.BitMap = s->chunky;
        WriteChunkyPixels(&rp, 0, 0, f->width - 1, f->height - 1, d->chunky, f->width);
    }
    ReleaseSemaphore(&d->lock);
    if (ok && f->layout != CDXL_CHUNKY)
        frameBitMap(s, f);
    return ok;
}

/* The frame's palette as a colour map, for every frame that has one: the
 * first's for a frame with the first's palette (not NULL: a frame after
 * one with other colours, going back, must get them back), else its own,
 * in its slot. */
static struct ColorMap *frameColours(CdxlData *d, ULONG index, const UBYTE *frame, const struct cdxl_frame *f)
{
    UBYTE pal[256 * 3];
    struct ColorMap **cm = &d->cmaps[index % RING];
    ULONG i;

    if (f->layout == CDXL_CHUNKY || !f->colours)
        return NULL;
    cdxl_palette(frame, f, pal);
    if (f->colours == d->first.colours && !memcmp(pal, d->palette, f->colours * 3))
        return d->firstMap;
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
    if (d->firstMap)
        FreeColorMap(d->firstMap);
    d->firstMap = NULL;
    if (d->chunky)
        FreeVec(d->chunky);
    d->chunky = NULL;
    cdxl_index_free(&d->io, &d->ix);
    if (d->fh)
        Close(d->fh);
    d->fh = 0;
}

static BOOL loadFilm(Class *cl, Object *o)
{
    CdxlData *d = INST_DATA(cl, o);
    ULONG sourceType = DTST_FILE, mode = 0, n, any = 0, i;
    BPTR given = 0, lock;
    STRPTR name = NULL;
    struct cdxl_frame f;
    UBYTE *frame;
    LONG err = DTERROR_INVALID_DATA;

    memset(d, 0, sizeof *d);
    InitSemaphore(&d->lock);
    d->io.ctx = d;
    d->io.read = dosRead;
    d->io.size = dosSize;
    d->io.alloc = dosAlloc;
    d->io.free = dosFree;
    d->anyMem = GetDTAttrs(o, ADT_GFA_FramesAnyMem, (ULONG)&any, TAG_DONE) == 1 && any == 1;
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
    /* in constant time when the frames are all one size (cdxl_core.c) */
    d->pos = 0;
    if (cdxl_index_open(&d->io, &d->ix))
        goto out;
    d->first = d->ix.first;
    d->depth = d->first.layout == CDXL_CHUNKY ? 8 : d->first.planes;
    if (!(frame = AllocVec(d->ix.max_size, MEMF_ANY))) {
        err = ERROR_NO_FREE_STORE;
        goto out;
    }
    if (!readFrame(d, 0, frame, &f)) {
        FreeVec(frame);
        goto out;
    }
    if (f.layout != CDXL_CHUNKY)
        cdxl_palette(frame, &f, d->palette);
    if (!(d->keyFrame = AllocBitMap(f.width, f.height, d->depth, BMF_CLEAR, NULL))
        || (f.layout == CDXL_CHUNKY && !(d->chunky = AllocVec((ULONG)f.width * f.height, MEMF_ANY)))) {
        FreeVec(frame);
        err = ERROR_NO_FREE_STORE;
        goto out;
    }
    drawFrame(d, frame, &f, d->keyFrame);
    FreeVec(frame);
    if (f.layout != CDXL_CHUNKY && f.colours && (d->firstMap = GetColorMap(256)) != NULL)
        for (i = 0; i < f.colours; i++)
            SetRGB32CM(d->firstMap, i, d->palette[i * 3] * 0x01010101UL, d->palette[i * 3 + 1] * 0x01010101UL,
                       d->palette[i * 3 + 2] * 0x01010101UL);

    if (d->ix.max_audio) {
        d->soundSlot = (d->ix.max_audio + 1) & ~1UL;
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
        ADTA_Frames, d->ix.frames,
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
    UBYTE *frame = NULL;

    if (!out)
        return ERROR_REQUIRED_ARG_MISSING;
    if (!d->fh || !d->ix.frames)
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
    if (!err && !(frame = AllocVec(d->ix.max_size, MEMF_ANY)))
        err = ERROR_NO_FREE_STORE;
    for (i = 0; !err && i < d->ix.frames; i++) {
        if (!readFrame(d, i, frame, &f))
            err = IoErr() ? IoErr() : ERROR_SEEK_ERROR;
        else if (Write(out, frame, f.size) != (LONG)f.size)
            err = IoErr() ? IoErr() : ERROR_DISK_FULL;
    }
    if (frame)
        FreeVec(frame);
    return err;
}

ULONG dt_dispatch(Class *cl, Object *o, Msg msg)
{
    switch (msg->MethodID) {
    case DTM_WRITE: {
        struct dtWrite *w = (struct dtWrite *)msg;
        LONG err;
        /* IFF, or another format (OIA_WriteFormat: an MP4), is animation.datatype's */
        if (w->dtw_Mode != DTWM_RAW || GetTagData(OIA_WriteFormat, 0, w->dtw_AttrList))
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
        /* As gfopenvideo.datatype: animation.datatype 47 hands frames back
         * in alf_BitMap (alf_UserData, our CdxlFrame, with it) to be read
         * into again; each is freed once, when the frame holding it last
         * unloads. */
        CdxlData *d = INST_DATA(cl, o);
        struct adtFrame *alf = (struct adtFrame *)msg;
        ULONG index = alf->alf_TimeStamp;
        struct BitMap *given = alf->alf_BitMap;
        struct CdxlFrame *s = alf->alf_UserData;
        struct cdxl_frame f;
        BOOL drawn;
        if (index >= d->ix.frames)
            index = d->ix.frames - 1;
        alf->alf_Frame = index;
        alf->alf_Duration = 1;
        alf->alf_CMap = NULL;
        alf->alf_Sample = NULL;
        alf->alf_SampleLength = 0;
        if (!given || !s || (given != &s->bm && given != s->chunky))
            s = NULL;                   /* none of ours handed back */
        if (!s && !(s = AllocVec(sizeof *s, MEMF_PUBLIC | MEMF_CLEAR))) {
            SetIoErr(ERROR_NO_FREE_STORE);
            return 0;
        }
        drawn = loadInto(d, o, s, index, &f);
        if (!drawn && !given) {
            freeFrame(s);
            alf->alf_BitMap = NULL;
            alf->alf_UserData = NULL;
            return 0;
        }
        if (drawn) {
            alf->alf_CMap = frameColours(d, index, s->buf, &f);
            if (d->ring && f.audio && f.audio <= d->soundSlot) {
                BYTE *at = d->ring + (index % RING) * d->soundSlot;
                cdxl_mono(s->buf, &f, (int8_t *)at);
                alf->alf_Sample = at;          /* ours, in the ring: freed with the object */
                alf->alf_SampleLength = f.audio;
                alf->alf_Period = d->period;
            }
        }
        alf->alf_BitMap = d->first.layout == CDXL_CHUNKY ? s->chunky : &s->bm;
        alf->alf_UserData = s;
        return drawn ? 1 : 0;
    }
    case ADTM_UNLOADFRAME: {
        struct adtFrame *alf = (struct adtFrame *)msg;
        freeFrame(alf->alf_UserData);
        alf->alf_UserData = NULL;
        alf->alf_BitMap = NULL;
        return 0;
    }
    default:
        return DoSuperMethodA(cl, o, msg);
    }
}
