/*
 * gfopenvideo.datatype: MP4, MOV, MKV, AVI, WMV and MPEG video (H.264, HEVC,
 * AV1, VP9, MPEG-4, MPEG-1/2, WMV) for AmigaOS 3.x, an animation.datatype
 * subclass. The media.decode/1 service (DalsinAI/openamigaservice
 * docs/MEDIA_DECODE.md) on the services card or a paired Cradle keeps the
 * video open and sends each frame, scaled to fit ENV:GFOpenImage/VideoWidth x
 * VideoHeight: in 256 colours (webm.datatype's dithered colour cube), or in
 * HAM8 on an AGA screen (ENV:GFOpenImage/VideoMode). The sound comes the same
 * way, 8-bit mono handed out with each frame.
 *
 * Streams: a NETSTREAM file (what the NETSTREAM: handler gives for a
 * channel, or a saved one: "#NETSTREAM", then NAME= and URL= lines) is
 * opened by the host from its URL, and played as it comes. A file bigger
 * than ENV:GFOpenImage/VideoChunk KB (default 2048) goes to the host in
 * parts, so the Amiga never holds it whole.
 *
 * Without a card or a Cradle the video does not open: these codecs are far
 * too slow for a 68k.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include <string.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/gfxbase.h>
#include <graphics/modeid.h>
#include <graphics/view.h>
#include <intuition/screens.h>
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
#include <stdarg.h>
#include <stdio.h>                 /* vsnprintf: dtlib.c's own */

#include "dtlib.h"
#include "dtservice.h"

const char LibName[] = "gfopenvideo.datatype";
const char LibIdString[] = "gfopenvideo.datatype 47.2 (20.10.2026) AmiCradle Team, decoded by media.decode/1";
const UWORD LibVersion = 47;
const UWORD LibRevision = 2;
static const char version[] __attribute__((used)) = "$VER: gfopenvideo.datatype 47.2 (20.10.2026)";

const char dt_superclass[] = "animation.datatype";
const UWORD dt_superversion = 40;

/* media.decode/1 (docs/MEDIA_DECODE.md) */
#define MD_PROBE  1
#define MD_DECODE 2
#define MD_VOPEN  3
#define MD_VFRAME 4
#define MD_VCLOSE 5
#define MD_VOPENURL 6
#define MD_VSOUND 7
#define MD_VUPLOAD 8
#define MD_KIND_ANIMATION 2
#define MD_FLAG_SOUND 2
#define MD_FLAG_LOOP 4
#define MD_FLAG_LIVE 8
#define MD_PIX_CUBE 0
#define MD_PIX_HAM8 2
#define MD_HAM8_FIXED 1
#define HAM8_PALETTE 192           /* 64 base colours, R, G, B */

/* A stream that never ends is played as this many frames (4 hours at 25 a second). */
#define LIVE_FRAMES 360000UL
/* Frames whose sound and colours are kept at once: animation.datatype
 * loads a few ahead, never this many. */
#define RING 64
/* A file goes to the host in parts this big. */
#define PART (256UL * 1024)
#define SOUND_RATE 22050
/* openOnHost: the memory ran out here (the rest are the service's OSERR_*). */
#define VERR_NOMEM (-100)
#define NO_MODE ((ULONG)INVALID_ID)

#ifndef DTA_Repeat
#define DTA_Repeat (DTA_Dummy + 302)
#endif

/* The PAL colour clock, for periods. */
#define PAL_CLOCK 3546895UL

typedef struct {
    ULONG handle;                  /* the host's, from VOPEN; 0 when none */
    ULONG width, height, frames;
    ULONG fps1000;
    BOOL ham8;                     /* frames in HAM8, not 256 colours */
    BOOL ownColours;               /* HAM8: each frame's own base colours, handed out with it */
    BOOL sound;                    /* VSOUND with each frame */
    struct BitMap *keyFrame;
    BYTE *ring;                    /* RING slots of soundSlot bytes in chip RAM, by frame */
    ULONG soundSlot;
    struct ColorMap *cmaps[RING];  /* HAM8 with own colours: by frame */
    ULONG period;                  /* Paula period for the sound, handed out with every frame */
} VideoData;

ULONG dt_instsize = sizeof(VideoData);

BOOL dt_init(void)
{
    return TRUE;
}

void dt_cleanup(void)
{
}

static ULONG get32(const UBYTE *p)
{
    return (ULONG)p[0] << 24 | (ULONG)p[1] << 16 | (ULONG)p[2] << 8 | p[3];
}

static ULONG envNumber(const char *name, ULONG fallback, ULONG least)
{
    char buf[16];
    LONG n = GetVar((CONST_STRPTR)name, (STRPTR)buf, sizeof buf, GVF_GLOBAL_ONLY);
    ULONG v = 0;
    LONG i;

    for (i = 0; i < n && buf[i] >= '0' && buf[i] <= '9'; i++)
        v = v * 10 + (buf[i] - '0');
    return v >= least ? v : fallback;
}

/* With ENV:GFOpenImage/VideoLog set, each method animation.datatype sends
 * is added to T:gfopenvideo.log, for finding out how it plays a video.
 * Only from processes: the loader and player are, a plain task can't use DOS. */
static void vlog(const char *fmt, ...)
{
    char buf[4], line[160];
    struct Task *me = FindTask(NULL);
    va_list ap;
    BPTR f;
    LONG n;

    if (me->tc_Node.ln_Type != NT_PROCESS || GetVar((CONST_STRPTR)"GFOpenImage/VideoLog", (STRPTR)buf, sizeof buf, GVF_GLOBAL_ONLY) < 0)
        return;
    for (n = 0; me->tc_Node.ln_Name && me->tc_Node.ln_Name[n] && n < 40; n++)
        line[n] = me->tc_Node.ln_Name[n];
    line[n++] = ':';
    line[n++] = ' ';
    va_start(ap, fmt);
    n += vsnprintf(line + n, sizeof line - n - 1, fmt, ap);
    va_end(ap);
    if (n > (LONG)sizeof line - 2)
        n = sizeof line - 2;
    line[n++] = '\n';
    if ((f = Open((CONST_STRPTR)"T:gfopenvideo.log", MODE_READWRITE)) != 0) {
        Seek(f, 0, OFFSET_END);
        Write(f, line, n);
        Close(f);
    }
}

/* An ENV: variable's text, or "" when it is not set. */
static void envText(const char *name, char *buf, LONG size)
{
    if (GetVar((CONST_STRPTR)name, (STRPTR)buf, size, GVF_GLOBAL_ONLY) < 0)
        buf[0] = 0;
}

/* The colours set on the object: webm.datatype's 256 (the 6x6x6 cube, then a
 * grey ramp), or for HAM8 the 64 base colours host and Amiga agree on (the
 * 4x4x4 cube, each step 85), or given ones (pal, R, G, B bytes). */
static void setPalette(Object *o, BOOL ham8, const UBYTE *pal)
{
    struct ColorRegister *cmap = NULL;
    ULONG *cregs = NULL, *gregs = NULL;
    int i;

    SetDTAttrs(o, NULL, NULL, ADTA_NumColors, ham8 ? 64 : 256, TAG_DONE);
    GetDTAttrs(o, ADTA_ColorRegisters, (ULONG)&cmap, ADTA_CRegs, (ULONG)&cregs, ADTA_GRegs, (ULONG)&gregs, TAG_DONE);
    for (i = 0; i < (ham8 ? 64 : 256); i++) {
        UBYTE r, g, b;
        if (pal) {
            r = pal[i * 3];
            g = pal[i * 3 + 1];
            b = pal[i * 3 + 2];
        } else if (ham8) {
            r = (i >> 4) * 85;
            g = ((i >> 2) & 3) * 85;
            b = (i & 3) * 85;
        } else if (i < 216) {
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

/* The default public screen's display mode, when it is a native (chipset)
 * one; INVALID_ID for a graphics card's, or none. */
static ULONG nativeMode(void)
{
    struct Screen *scr = LockPubScreen(NULL);
    ULONG id = NO_MODE;

    if (scr) {
        id = GetVPModeID(&scr->ViewPort);
        UnlockPubScreen(NULL, scr);
    }
    if (id != NO_MODE && (id & MONITOR_ID_MASK) > DBLPAL_MONITOR_ID)
        id = NO_MODE;                   /* a graphics card's mode */
    return id;
}

/* HAM8 or 256 colours: ENV:GFOpenImage/VideoMode HAM8 or 256; AUTO (the
 * default) is HAM8 on an AGA machine whose screen is a chipset one, and
 * 256 colours on a graphics card, where HAM shows nothing. */
static BOOL wantHam8(void)
{
    char mode[8];

    envText("GFOpenImage/VideoMode", mode, sizeof mode);
    if ((mode[0] | 0x20) == 'h')
        return TRUE;
    if (mode[0] == '2')
        return FALSE;
    return (GfxBase->ChipRevBits0 & GFXF_AA_ALICE) && nativeMode() != NO_MODE;
}

/* Frame index from the host, drawn into "into" (a bitmap animation.datatype
 * hands back to be used again) or into a new 8-bit one. svc is opened by
 * the caller's own process: animation.datatype loads frames from its own.
 * In HAM8 with its own colours the frame's base colours go into its slot's
 * colour map (and to *pal when pal is given). NULL with *nomem set when
 * the memory ran out. */
static struct BitMap *fetchFrame(VideoData *d, struct dt_service *svc, ULONG index, struct BitMap *into,
                                 BOOL *nomem, UBYTE *pal)
{
    struct OSBuffer buf[4];
    ULONG extra[4], skip = d->ham8 ? HAM8_PALETTE : 0;
    struct BitMap *bm;
    struct RastPort rp;
    UBYTE *chunky;
    BOOL ok = FALSE;

    /* The chip RAM first: when it has run out, no frame is decoded for nothing. */
    *nomem = TRUE;
    if (!(bm = into ? into : AllocBitMap(d->width, d->height, 8, BMF_CLEAR, NULL)))
        return NULL;
    if (!(chunky = AllocVec(skip + d->width * d->height, MEMF_ANY))) {
        if (!into)
            FreeBitMap(bm);
        return NULL;
    }
    *nomem = FALSE;
    memset(buf, 0, sizeof buf);
    buf[1].ob_Data = chunky;
    buf[1].ob_Length = skip + d->width * d->height;
    extra[0] = index;
    extra[1] = d->ham8 ? MD_PIX_HAM8 : MD_PIX_CUBE;
    extra[2] = d->ham8 && !d->ownColours ? MD_HAM8_FIXED : 0;
    extra[3] = 0;
    if (dt_service_call(svc, MD_VFRAME, d->handle, 2, buf, extra, NULL, NULL) == OSERR_OK) {
        InitRastPort(&rp);
        rp.BitMap = bm;
        /* HAM8's control bits are bits 7-6: planes 7 and 8, as AGA reads them. */
        WriteChunkyPixels(&rp, 0, 0, d->width - 1, d->height - 1, chunky + skip, d->width);
        if (d->ham8 && d->ownColours) {
            struct ColorMap **cm = &d->cmaps[index % RING];
            int i;
            if (!*cm)
                *cm = GetColorMap(256);
            for (i = 0; *cm && i < 64; i++)
                SetRGB32CM(*cm, i, chunky[i * 3] * 0x01010101UL, chunky[i * 3 + 1] * 0x01010101UL,
                           chunky[i * 3 + 2] * 0x01010101UL);
            if (pal)
                memcpy(pal, chunky, HAM8_PALETTE);
        }
        ok = TRUE;
    }
    FreeVec(chunky);
    if (!ok) {
        if (!into)
            FreeBitMap(bm);
        bm = NULL;
    }
    return bm;
}

/* Frame index's sound into its slot in the chip RAM ring; its length, or 0. */
static ULONG fetchSound(VideoData *d, struct dt_service *svc, ULONG index, BYTE **at)
{
    struct OSBuffer buf[4];
    ULONG extra[4] = { index, SOUND_RATE, 0, 0 }, got = 0;

    if (!d->sound || !d->ring)
        return 0;
    *at = d->ring + (index % RING) * d->soundSlot;
    memset(buf, 0, sizeof buf);
    buf[1].ob_Data = *at;
    buf[1].ob_Length = d->soundSlot;
    if (dt_service_call(svc, MD_VSOUND, d->handle, 2, buf, extra, &got, NULL) != OSERR_OK)
        return 0;
    return got <= d->soundSlot ? got : 0;
}

static void freeData(VideoData *d)
{
    struct dt_service svc;
    struct OSBuffer buf[4];
    ULONG extra[4] = { 0, 0, 0, 0 };
    int i;

    if (d->handle && dt_service_open(&svc, "media.decode/1")) {
        memset(buf, 0, sizeof buf);
        dt_service_call(&svc, MD_VCLOSE, d->handle, 0, buf, extra, NULL, NULL);
        dt_service_close(&svc);
        vlog("video closed on the host");
    }
    d->handle = 0;
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
}

/* A NETSTREAM file's URL= line, into url (at most size - 1 bytes), and its
 * NAME= line into name; FALSE when the text is not one. */
static BOOL parseStream(const UBYTE *text, LONG n, char *url, LONG size, char *name, LONG namesize)
{
    LONG i = 0;

    if (n < 10 || memcmp(text, "#NETSTREAM", 10))
        return FALSE;
    url[0] = 0;
    while (i < n) {
        LONG e = i, k;
        while (e < n && text[e] != '\n' && text[e] != '\r')
            e++;
        if (e - i > 4 && !memcmp(text + i, "URL=", 4)) {
            for (k = 0; k < e - i - 4 && k < size - 1; k++)
                url[k] = text[i + 4 + k];
            url[k] = 0;
        } else if (e - i > 5 && !memcmp(text + i, "NAME=", 5)) {
            for (k = 0; k < e - i - 5 && k < namesize - 1; k++)
                name[k] = text[i + 5 + k];
            name[k] = 0;
        }
        i = e + 1;
    }
    return url[0] != 0;
}

/* Opens the video on the host from the object's file: a NETSTREAM file's
 * stream by its URL, a big file in parts, a small one whole. extra holds
 * the size wanted; info, handle and fps1000 are VOPEN's answer. */
static LONG openOnHost(Object *o, struct dt_service *svc, ULONG extra[4], UBYTE info[24], ULONG *handle,
                       ULONG *fps1000, char *name, LONG namesize)
{
    struct OSBuffer buf[4];
    ULONG sourceType = DTST_FILE, upload = 0, chunk, sent = 0, part[4] = { 0, 0, 0, 0 };
    BPTR fh = 0;
    LONG size, n, st;
    UBYTE head[16], *data;

    GetDTAttrs(o, DTA_SourceType, (ULONG)&sourceType, DTA_Handle, (ULONG)&fh, TAG_DONE);
    if (sourceType != DTST_FILE || !fh)
        return OSERR_BADREQUEST;
    if (Seek(fh, 0, OFFSET_END) < 0 || (size = Seek(fh, 0, OFFSET_BEGINNING)) <= 0)
        return OSERR_BADREQUEST;
    memset(buf, 0, sizeof buf);
    buf[1].ob_Data = info;
    buf[1].ob_Length = 24;

    /* A stream: the host opens it from its URL. */
    n = Read(fh, head, sizeof head);
    Seek(fh, 0, OFFSET_BEGINNING);
    if (n >= 10 && !memcmp(head, "#NETSTREAM", 10)) {
        char url[1024];
        if (size > 4096 || !(data = AllocVec(size, MEMF_ANY)))
            return OSERR_BADREQUEST;
        n = Read(fh, data, size);
        if (!parseStream(data, n, url, sizeof url, name, namesize)) {
            FreeVec(data);
            return OSERR_BADREQUEST;
        }
        FreeVec(data);
        vlog("stream %s", url);
        buf[0].ob_Data = url;
        buf[0].ob_Length = strlen(url);
        return dt_service_call(svc, MD_VOPENURL, 0, 2, buf, extra, handle, fps1000);
    }

    /* A big file: in parts, so it is never all in memory here. */
    chunk = envNumber("GFOpenImage/VideoChunk", 2048, 1) * 1024;
    if ((ULONG)size > chunk) {
        if (!(data = AllocVec(PART, MEMF_ANY)))
            return VERR_NOMEM;
        st = OSERR_OK;
        while (st == OSERR_OK && sent < (ULONG)size) {
            n = Read(fh, data, PART);
            if (n <= 0) {
                st = OSERR_BADREQUEST;
                break;
            }
            sent += n;
            part[0] = sent >= (ULONG)size ? 1 : 0;
            buf[0].ob_Data = data;
            buf[0].ob_Length = n;
            st = dt_service_call(svc, MD_VUPLOAD, upload, 0, buf, part, &upload, NULL);
        }
        FreeVec(data);
        vlog("sent %lu bytes in parts: %ld", (unsigned long)sent, (long)st);
        if (st != OSERR_OK)
            return st;
        buf[0].ob_Data = NULL;
        buf[0].ob_Length = 0;
        extra[2] = upload;
        return dt_service_call(svc, MD_VOPEN, 0, 2, buf, extra, handle, fps1000);
    }

    /* A small one whole. */
    if (!(data = dt_read_source(o, (ULONG *)&size)))
        return VERR_NOMEM;
    buf[0].ob_Data = data;
    buf[0].ob_Length = size;
    st = dt_service_call(svc, MD_VOPEN, 0, 2, buf, extra, handle, fps1000);
    FreeVec(data);
    return st;
}

static BOOL loadVideo(Class *cl, Object *o)
{
    VideoData *d = INST_DATA(cl, o);
    struct dt_service svc;
    ULONG extra[4], handle = 0, fps1000 = 0, fps, mode, flags;
    UBYTE info[24], pal[HAM8_PALETTE];
    STRPTR name = NULL;
    char title[64], cmapVar[4];
    BOOL ok = FALSE, nomem;
    LONG err = DTERROR_INVALID_DATA, st;

    memset(d, 0, sizeof *d);
    title[0] = 0;
    GetDTAttrs(o, DTA_Name, (ULONG)&name, TAG_DONE);
    if (!dt_service_open(&svc, "media.decode/1")) {
        SetIoErr(ERROR_NOT_IMPLEMENTED);       /* no card and no paired Cradle */
        return FALSE;
    }
    d->ham8 = wantHam8();
    envText("GFOpenImage/VideoCMap", cmapVar, sizeof cmapVar);
    d->ownColours = d->ham8 && cmapVar[0];
    /* HAM8 defaults to a PAL low-resolution screen's size, 256 colours to 640 x 480. */
    extra[0] = envNumber("GFOpenImage/VideoWidth", d->ham8 ? 320 : 640, 16);
    extra[1] = envNumber("GFOpenImage/VideoHeight", d->ham8 ? 256 : 480, 16);
    /* Each frame is a planar bitmap in chip RAM, and animation.datatype
     * keeps a few: ask for a smaller picture when chip RAM is short, so a
     * third or fourth video still opens. */
    while (extra[0] > 160 && AvailMem(MEMF_CHIP | MEMF_LARGEST) < 4 * extra[0] * extra[1]) {
        extra[0] /= 2;
        extra[1] /= 2;
    }
    extra[2] = extra[3] = 0;
    if ((st = openOnHost(o, &svc, extra, info, &handle, &fps1000, title, sizeof title)) != OSERR_OK
        || get32(info) != MD_KIND_ANIMATION) {
        if (st == VERR_NOMEM)
            err = ERROR_NO_FREE_STORE;
        goto out;
    }
    d->handle = handle;
    flags = get32(info + 8);
    d->frames = get32(info + 12);
    d->width = get32(info + 16);
    d->height = get32(info + 20);
    if (!d->frames && (flags & MD_FLAG_LIVE))
        d->frames = LIVE_FRAMES;               /* a channel that never ends */
    if (!d->frames || !d->width || !d->height || d->width > 4096 || d->height > 4096)
        goto out;
    d->fps1000 = fps1000 ? fps1000 : 25000;
    fps = (d->fps1000 + 500) / 1000;
    if (!fps)
        fps = 1;
    vlog("film flags %lx, %lu frames, %s", flags, d->frames, d->ham8 ? "HAM8" : "256 colours");
    if (flags & MD_FLAG_SOUND) {
        /* A ring of chip RAM for each frame's sound, a slot a frame. */
        d->soundSlot = (SOUND_RATE * 1000UL / d->fps1000 + 4) & ~1UL;
        if ((d->ring = AllocVec(RING * d->soundSlot, MEMF_CHIP | MEMF_CLEAR)) != NULL) {
            d->sound = TRUE;
            d->period = PAL_CLOCK / SOUND_RATE;
            SetDTAttrs(o, NULL, NULL, ADTA_Period, d->period, ADTA_Volume, 64, ADTA_Cycles, 1, TAG_DONE);
        }
    }
    if (!(d->keyFrame = fetchFrame(d, &svc, 0, NULL, &nomem, pal))) {
        if (nomem)
            err = ERROR_NO_FREE_STORE;
        goto out;
    }

    setPalette(o, d->ham8, d->ownColours ? pal : NULL);
    if (d->ham8) {
        /* HAM8 on the screen's own monitor (PAL or NTSC), in high
         * resolution and interlaced when the picture needs them. */
        mode = nativeMode();
        mode = (mode != NO_MODE ? mode & MONITOR_ID_MASK : 0) | HAM_KEY
             | (d->width > 400 ? HIRES_KEY : 0) | (d->height > 300 ? LORESLACE_KEY : 0);
        SetDTAttrs(o, NULL, NULL, ADTA_ModeID, mode, TAG_DONE);
    }
    SetDTAttrs(o, NULL, NULL,
        DTA_ObjName, (ULONG)(title[0] ? (STRPTR)title : name ? FilePart(name) : (STRPTR)"Video"),
        DTA_NominalHoriz, d->width,
        DTA_NominalVert, d->height,
        ADTA_Width, d->width,
        ADTA_Height, d->height,
        ADTA_Depth, 8,
        ADTA_Frames, d->frames,
        ADTA_FramesPerSecond, fps,
        ADTA_KeyFrame, (ULONG)d->keyFrame,
        /* an animated GIF or PNG that asks to play more than once */
        DTA_Repeat, (flags & MD_FLAG_LOOP) ? TRUE : FALSE,
        TAG_DONE);
    ok = TRUE;
out:
    dt_service_close(&svc);
    if (!ok) {
        freeData(d);
        SetIoErr(err);
    }
    return ok;
}

ULONG dt_dispatch(Class *cl, Object *o, Msg msg)
{
    if (msg->MethodID != ADTM_LOADFRAME && msg->MethodID != ADTM_UNLOADFRAME && msg->MethodID >= ADTM_Dummy)
        vlog("method %lx", (unsigned long)msg->MethodID);
    else if (msg->MethodID == DTM_TRIGGER)
        vlog("trigger %lu", (unsigned long)((struct dtTrigger *)msg)->dtt_Function);
    switch (msg->MethodID) {
    case OM_NEW: {
        Object *obj = (Object *)DoSuperMethodA(cl, o, msg);
        if (obj && !loadVideo(cl, obj)) {
            LONG err = IoErr();
            CoerceMethod(cl, obj, OM_DISPOSE);
            SetIoErr(err);
            obj = NULL;
        }
        return (ULONG)obj;
    }
    case OM_DISPOSE: {
        /* animation.datatype stops its loader and player in its own
         * OM_DISPOSE, and until then they may still be loading frames from
         * the host, playing the sound or showing the key frame: so the
         * superclass goes first, and what we own is freed after it. */
        VideoData keep = *(VideoData *)INST_DATA(cl, o);
        ULONG rc;
        vlog("dispose key=%lx", (unsigned long)keep.keyFrame);
        rc = DoSuperMethodA(cl, o, msg);
        vlog("superclass disposed");
        freeData(&keep);
        vlog("disposed");
        return rc;
    }
    case ADTM_LOADFRAME: {
        /* animation.datatype 47 (OS 3.2) loads a few frames ahead, then
         * hands each frame's bitmap back in alf_BitMap to be drawn again
         * with a later frame; it shows only bitmaps it handed out. A
         * bitmap moves from frame to frame without ADTM_UNLOADFRAME, so
         * alf_UserData follows it, and each is freed once, when the
         * frame holding it last is unloaded. alf_BitMap is NULL when a
         * new one is wanted (and always on older animation.datatypes). */
        VideoData *d = INST_DATA(cl, o);
        struct adtFrame *alf = (struct adtFrame *)msg;
        ULONG index = alf->alf_TimeStamp;
        struct BitMap *bm, *given = alf->alf_BitMap;
        struct dt_service svc;
        BYTE *sample = NULL;
        ULONG n = 0;
        BOOL nomem;
        if (index >= d->frames)
            index = d->frames - 1;
        vlog("loading ts=%lu given=%lx", (unsigned long)alf->alf_TimeStamp, (unsigned long)given);
        if (dt_service_open(&svc, "media.decode/1")) {
            bm = fetchFrame(d, &svc, index, given, &nomem, NULL);
            n = fetchSound(d, &svc, index, &sample);
            dt_service_close(&svc);
        } else {
            bm = NULL;
            nomem = FALSE;
        }
        if (nomem)
            SetIoErr(ERROR_NO_FREE_STORE);
        alf->alf_Frame = index;
        alf->alf_Duration = 1;
        alf->alf_BitMap = bm ? bm : given;
        /* HAM8 with each frame's own colours: hers, from the ring */
        alf->alf_CMap = d->ownColours ? d->cmaps[index % RING] : NULL;
        alf->alf_Sample = NULL;
        alf->alf_SampleLength = 0;
        if (n) {
            alf->alf_Sample = sample;          /* ours, in the ring: freed with the object */
            alf->alf_SampleLength = n;
            alf->alf_Period = d->period;       /* animation.datatype 47 plays each frame's sound at this */
        }
        alf->alf_UserData = alf->alf_BitMap;
        vlog("load ts=%lu frame=%lu given=%lx bm=%lx", (unsigned long)alf->alf_TimeStamp, (unsigned long)index,
             (unsigned long)given, (unsigned long)bm);
        return bm ? 1 : 0;
    }
    case ADTM_UNLOADFRAME: {
        struct adtFrame *alf = (struct adtFrame *)msg;
        vlog("unload ts=%lu frame=%lu bm=%lx", (unsigned long)alf->alf_TimeStamp, (unsigned long)alf->alf_Frame,
             (unsigned long)alf->alf_UserData);
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
