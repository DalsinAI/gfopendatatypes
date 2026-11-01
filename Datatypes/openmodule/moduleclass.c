/*
 * gfopenmodule.datatype: music modules for AmigaOS 3.x, played on the Amiga
 * itself: ProTracker, NoiseTracker and SoundTracker MODs, MED and OctaMED,
 * Oktalyzer, DigiBooster, FastTracker 2, Scream Tracker 3 and Impulse
 * Tracker, and the other formats libxmp reads. A sound.datatype subclass.
 *
 * Streamed, not decoded whole: libxmp (MIT) mixes the module a chunk at a
 * time, and a player process of the object's own keeps it playing: 16-bit
 * stereo through ahi.device's unit 0 (two half-second requests taking
 * turns through ahir_Link), else 8-bit stereo on a left and a right Paula
 * channel through audio.device (four eighth-second buffers). sound.datatype V44 and newer describe this
 * as a streaming subclass: SDTA_Sample stays NULL, SDTA_SampleLength and
 * SDTA_SamplesPerSec give the length, and DTM_TRIGGER plays, pauses and
 * stops. See DESIGN.md beside this file for why it is done this way.
 *
 * All of libxmp's work (loading, and mixing, which uses some floating
 * point) happens in the player process, never in the caller's task, so
 * the ROM math libraries it calls on a 68k without an FPU only ever change
 * that process's FPU state.
 *
 * Who mixes is a ladder (DESIGN.md section 7), the Team's order: a cores
 * board core, one job a chunk, through gfopenmulticore.library; else
 * media.decode/1 on a services card or a paired Cradle, through
 * gfopenservice.device; else this CPU.
 * OIA_DecodedBy and OIA_Stats (include/datatypes/gfopenimage.h) say which.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team. libxmp keeps its MIT licence.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/var.h>
#include <devices/audio.h>
#include <devices/timer.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/soundclass.h>
#include <intuition/classes.h>
#include <intuition/gadgetclass.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/datatypes.h>
#include <proto/timer.h>
#include <proto/gfopenmulticore.h>
#include <clib/alib_protos.h>
#include <datatypes/gfopenimage.h>

#include <xmp.h>

#include "dtlib.h"
#include "dtservice.h"
#include "xmpglue.h"
#include "ahidev.h"

const char LibName[] = "gfopenmodule.datatype";
const char LibIdString[] = "gfopenmodule.datatype 47.3 (8.10.2026) AmiCradle Team, played by libxmp 4.7.3";
const UWORD LibVersion = 47;
const UWORD LibRevision = 3;
static const char version[] __attribute__((used)) = "$VER: gfopenmodule.datatype 47.3 (8.10.2026)";

const char dt_superclass[] = "sound.datatype";
const UWORD dt_superversion = 39;

#ifdef OM_DEBUG
#include <stdarg.h>
int vsnprintf(char *buffer, unsigned long size, const char *format, va_list ap);
/* A trace (build with -DOM_DEBUG, and -DOM_DEBUG_LOG=\"file\" for another
 * place than T:gfopenmodule.log); from processes only. */
#ifndef OM_DEBUG_LOG
#define OM_DEBUG_LOG "T:gfopenmodule.log"
#endif
static void dlog(const char *fmt, ...)
{
    char line[160];
    va_list ap;
    BPTR f;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line - 1, fmt, ap);
    va_end(ap);
    strcat(line, "\n");
    if ((f = Open((CONST_STRPTR)OM_DEBUG_LOG, MODE_READWRITE))) {
        Seek(f, 0, OFFSET_END);
        Write(f, line, strlen(line));
        Close(f);
    }
}
#else
#define dlog(...) ((void)0)
#endif

/* Buffers queued on each channel, and how long each one plays. */
#define NBUF 4
#define CHUNK_DIV 8                    /* an eighth of a second */
#define PLAYER_STACK 65536
#define PLAYER_PRI 2

/* Rung 2: a job's stack, how long one may take, the most it may write. */
#define JOB_STACK 32768
#define JOB_TIMEOUT_MS 4000
#define ARENA_MAX (15UL * 1024 * 1024)

/* Rung 3: media.decode/1 (openamigaservice docs/MEDIA_DECODE.md), asked
 * for SVC_SECONDS of sound at a time. */
#define MD_PROBE 1
#define MD_DECODE 2
#define MD_KIND_SOUND 3
#define SVC_SECONDS 2
/* With the service, Paula's buffers are a quarter of a second, so a second
 * is queued while a piece is fetched. */
#define CHUNK_DIV_SVC 4

/* AHI: two requests of half a second, each linked to the one playing
 * (ahi.device starts a linked request only when the one it is linked to
 * plays: a third, linked to one still waiting, was never played). */
#define NBUF_AHI 2
#define CHUNK_DIV_AHI 2

/* The ladder (DESIGN.md section 7) */
enum { RUNG_NONE, RUNG_CPU, RUNG_CORE, RUNG_SERVICE };

/* Player commands: PC_QUIT by message, the others by mi->want and
 * SIGBREAKF_CTRL_F, so they can be given from any task (a click on the
 * gadget comes from input.device's) without waiting; the latest wins. */
enum { PC_NONE, PC_PLAY, PC_PAUSE, PC_STOP, PC_REWIND, PC_QUIT };

/* Player states */
enum { PS_STOPPED, PS_PLAYING, PS_PAUSED, PS_DRAINING };

struct ModInst {
    struct Process *player;
    struct MsgPort *port;          /* the player's commands */
    UBYTE *data;                   /* the module file, kept for DTM_WRITE */
    ULONG size;
    ULONG rate, period, frames, ms;
    LONG interp;
    volatile UWORD volume;
    volatile UWORD repeat;
    volatile UWORD state;
    volatile UWORD want;
    UWORD immediate, started;
    UWORD rung, coreFailed;
    char decodedBy[80];
    char stats[160];
    struct Task *sigTask;
    ULONG sigMask;
    char title[XMP_NAME_SIZE + 1];
    char type[XMP_NAME_SIZE + 1];
    struct timeval replay;         /* SDTA_ReplayPeriod's answer: OM_GET hands out its address */
};

ULONG dt_instsize = sizeof(struct ModInst);

struct PlayerMsg {
    struct Message pm_Msg;
    ULONG pm_Cmd;
    struct ModInst *pm_Inst;
    LONG pm_Result;
};

static ULONG get32(const UBYTE *b)
{
    return (ULONG)b[0] << 24 | (ULONG)b[1] << 16 | (ULONG)b[2] << 8 | b[3];
}

/* The CPU decides the defaults: 68040 and 68060 mix at 28000 Hz with
 * linear interpolation, the 68020 and 68030 at 16000 Hz without. */
static BOOL fastCpu;

static const struct DTMethod triggers[] = {
    {(STRPTR)"Play", (STRPTR)"PLAY", STM_PLAY},
    {(STRPTR)"Pause", (STRPTR)"PAUSE", STM_PAUSE},
    {(STRPTR)"Stop", (STRPTR)"STOP", STM_STOP},
    {NULL, NULL, 0}
};

BOOL dt_init(void)
{
    fastCpu = (SysBase->AttnFlags & (AFF_68040 | (1 << 7))) != 0;   /* bit 7: 68060 */
    return TRUE;
}

void dt_cleanup(void)
{
}

static ULONG envNumber(const char *name)
{
    char buf[16];
    LONG n = GetVar((CONST_STRPTR)name, (STRPTR)buf, sizeof buf, GVF_GLOBAL_ONLY);
    ULONG v = 0;
    LONG i;

    for (i = 0; i < n && buf[i] >= '0' && buf[i] <= '9'; i++)
        v = v * 10 + (buf[i] - '0');
    return v;
}

/* ENV:GFOpenImage/ModuleMix: nearest, linear or spline. */
static LONG envInterp(void)
{
    char buf[16];
    LONG n = GetVar((CONST_STRPTR)"GFOpenImage/ModuleMix", (STRPTR)buf, sizeof buf, GVF_GLOBAL_ONLY);
    if (n > 0) {
        if (buf[0] == 'n' || buf[0] == 'N' || buf[0] == '0')
            return XMP_INTERP_NEAREST;
        if (buf[0] == 'l' || buf[0] == 'L' || buf[0] == '1')
            return XMP_INTERP_LINEAR;
        if (buf[0] == 's' || buf[0] == 'S' || buf[0] == '2')
            return XMP_INTERP_SPLINE;
    }
    return fastCpu ? XMP_INTERP_LINEAR : XMP_INTERP_NEAREST;
}

/* --- the player process ------------------------------------------------------- */

/* The ROM math libraries libnix's soft floating point calls (xmpglue.c):
 * opened by each player process for itself. The bases libnix calls through
 * are shared by every player of every object, so each player keeps its own
 * opens and closes only those: it sets the shared bases only once its opens
 * have all worked (the same library, so the same base, while any player
 * holds it open) and never clears them, so one player that fails, or ends,
 * cannot take the libraries from another that is still mixing. */
extern struct Library *MathIeeeDoubBasBase, *MathIeeeDoubTransBase, *MathIeeeSingBasBase;

/* timer.device's E clock, to time the mixing (each player opens it). */
struct Device *TimerBase;

/* gfopenmulticore.library, for rung 2 (each player opens it). */
struct Library *OpenMulticoreBase;

/* Where the sound goes: AHI's unit 0 (16-bit stereo at the unit's rate,
 * two requests of half a second taking turns through ahir_Link), else
 * Paula through audio.device (8-bit, four buffers on a left and a right
 * channel). */
struct Audio {
    UBYTE ahi;
    UBYTE nbuf;
    UBYTE open, ended;
    UBYTE pervol;                  /* Paula: the next write sets period and volume */
    UBYTE head;                    /* AHI: the request that finishes next */
    BYTE lastSent;                 /* AHI: the last one sent, -1 none */
    UBYTE done[NBUF];              /* AHI: replied, not yet taken */
    struct MsgPort *port;
    ULONG chunk;                   /* frames a buffer */
    UBYTE busy[NBUF];
    /* Paula */
    struct IOAudio *ctl;
    struct IOAudio *req[NBUF][2];
    UBYTE *chip[NBUF][2];
    /* AHI */
    struct AHIRequest *ahiDev;     /* the one OpenDevice() opened */
    struct AHIRequest *areq[NBUF];
    UBYTE *abuf[NBUF];
};

/* What the player process keeps: the module in libxmp, and what each rung
 * needs. */
struct Player {
    struct ModInst *mi;
    xmp_context ctx;
    ULONG bpf;                     /* bytes a frame: 4 for AHI (16-bit stereo), 2 for Paula */
    LONG xmpFormat;
    UBYTE *mix;                    /* a chunk, as it goes out */
    /* the cores board */
    struct om_arena arena;         /* all of libxmp's memory, the job's written buffer */
    ULONG mark;                    /* the arena's use before xmp_start_player */
    UBYTE *coreMix;                /* in the arena: what a core mixes into */
    UBYTE *jobStack;
    struct OMCJob job;
    ULONG jobArgs[4];
    /* the service */
    struct dt_service svc;
    BOOL svcOpen;
    WORD *pcm16;                   /* a piece as media.decode/1 gives it */
    UBYTE *pcmOut;                 /* the same as it goes out */
    ULONG svcChannels, svcPos, pcmHave, pcmAt;
    /* the clock and the numbers */
    struct timerequest treq;
    BOOL timer;
    ULONG efreq;
    ULONG chunks, mixTicks, mixFrames, jobs, jobTicks, svcCalls, svcTicks;
};

static ULONG eclock(struct Player *p)
{
    struct EClockVal ev;
    if (!p->timer)
        return 0;
    p->efreq = ReadEClock(&ev);
    return ev.ev_lo;
}

/* Ticks of the E clock as a percentage of n frames' playing time. */
static ULONG loadPercent(struct Player *p, ULONG ticks, ULONG frames)
{
    if (!p->efreq || !frames)
        return 0;
    return (ULONG)(((unsigned long long)ticks * p->mi->rate * 100) / ((unsigned long long)p->efreq * frames));
}

/* --- Paula and AHI ---------------------------------------------------------------- */

/* Left and right: Paula's channels 0 and 3 play left, 1 and 2 right. */
static UBYTE allocMap[] = {3, 5, 10, 12};

static void paulaCommand(struct Audio *a, UWORD cmd)
{
    a->ctl->ioa_Request.io_Command = cmd;
    a->ctl->ioa_Request.io_Flags = 0;
    DoIO((struct IORequest *)a->ctl);
}

static BOOL audioOpen(struct Audio *a)
{
    ULONG unit;
    int i, s;

    a->ended = FALSE;
    for (i = 0; i < NBUF; i++)
        a->busy[i] = 0;
    if (a->ahi) {                 /* the device stays open: nothing to take */
        for (i = 0; i < NBUF; i++)
            a->done[i] = 0;
        a->lastSent = -1;
        a->head = 0;
        a->open = TRUE;
        return TRUE;
    }
    a->ctl->ioa_Request.io_Message.mn_Node.ln_Pri = 0;
    a->ctl->ioa_Request.io_Command = ADCMD_ALLOCATE;
    a->ctl->ioa_Request.io_Flags = ADIOF_NOWAIT;
    a->ctl->ioa_Data = allocMap;
    a->ctl->ioa_Length = sizeof allocMap;
    if (OpenDevice((CONST_STRPTR)AUDIONAME, 0, (struct IORequest *)a->ctl, 0))
        return FALSE;
    unit = (ULONG)a->ctl->ioa_Request.io_Unit;
    for (i = 0; i < NBUF; i++)
        for (s = 0; s < 2; s++) {
            struct IOAudio *r = a->req[i][s];
            *r = *a->ctl;
            r->ioa_Request.io_Message.mn_ReplyPort = a->port;
            /* s 0: the left channel (0 or 3), s 1: the right (1 or 2) */
            r->ioa_Request.io_Unit = (struct Unit *)(unit & (s == 0 ? 9 : 6));
        }
    a->open = TRUE;
    return TRUE;
}

/* AHI's replies, taken off the port: each one marks its request done. */
static void ahiCollect(struct Audio *a)
{
    struct Message *m;
    int i;
    while ((m = GetMsg(a->port)))
        for (i = 0; i < a->nbuf; i++)
            if (m == (struct Message *)a->areq[i]) {
                a->done[i] = 1;
                dlog("ahi: %ld back, error %ld", (long)i, (long)a->areq[i]->ahir_Std.io_Error);
            }
}

static void audioClose(struct Audio *a)
{
    int i, s;

    if (!a->open)
        return;
    if (a->ahi) {
        /* The newest first, so none starts as the one before it goes; each
         * comes back to the port (ahi.device's requests are collected with
         * GetMsg, not CheckIO: see ahiCollect). */
        BOOL left;
        for (i = a->nbuf - 1; i >= 0; i--) {
            int k = (a->head + i) % a->nbuf;
            if (a->busy[k] && !a->done[k])
                AbortIO((struct IORequest *)a->areq[k]);
        }
        do {
            left = FALSE;
            ahiCollect(a);
            for (i = 0; i < a->nbuf; i++)
                if (a->busy[i] && !a->done[i])
                    left = TRUE;
            if (left)
                WaitPort(a->port);
        } while (left);
        for (i = 0; i < a->nbuf; i++)
            a->busy[i] = a->done[i] = 0;
        a->lastSent = -1;
        a->open = FALSE;
        return;
    }
    /* CMD_FLUSH returns every write on both channels, playing or queued,
     * in one go inside audio.device; then each is collected. */
    paulaCommand(a, CMD_FLUSH);
    for (i = 0; i < NBUF; i++) {
        if (!a->busy[i])
            continue;
        for (s = 0; s < 2; s++)
            WaitIO((struct IORequest *)a->req[i][s]);
        a->busy[i] = 0;
    }
    paulaCommand(a, CMD_RESET);
    CloseDevice((struct IORequest *)a->ctl);
    a->open = FALSE;
}

/* --- the three rungs ------------------------------------------------------------ */

/* The cores board's job, run on another core: no OS, its arguments and
 * the arena only (GFOpenMulticore's rules). libxmp allocates nothing while
 * it mixes, and the arena holds all it writes. */
__attribute__((noinline)) LONG om_job_mix(xmp_context ctx, void *out, LONG bytes, LONG loop)
{
    return xmp_play_buffer(ctx, out, bytes, loop);
}

/* This CPU mixes. */
static UBYTE *fillCpu(struct Player *p, ULONG n)
{
    ULONG t = eclock(p);
    LONG r = xmp_play_buffer(p->ctx, p->mix, n * p->bpf, p->mi->repeat ? 0 : 1);
    p->mixTicks += eclock(p) - t;
    p->mixFrames += n;
    return r < 0 ? NULL : p->mix;
}

/* One job on any core of the board, mixing n frames into the arena. NULL
 * at the end of the song; *failed when the job did not run. */
static UBYTE *fillCore(struct Player *p, ULONG n, BOOL *failed)
{
    struct OMCJob *j = &p->job;
    ULONG t, used = p->arena.high;
    LONG st;

    *failed = FALSE;
    j->omj_Entry = (APTR)om_job_mix;
    memset(j->omj_Regs, 0, sizeof j->omj_Regs);
    j->omj_FPCR = 0;
    j->omj_Stack = p->jobStack;
    j->omj_StackSize = JOB_STACK;
    p->jobArgs[0] = (ULONG)p->ctx;
    p->jobArgs[1] = (ULONG)p->coreMix;
    p->jobArgs[2] = n * p->bpf;
    p->jobArgs[3] = p->mi->repeat ? 0 : 1;
    j->omj_NArgs = 4;
    j->omj_Args = p->jobArgs;
    j->omj_Grants[0].og_Addr = p->arena.base;
    j->omj_Grants[0].og_Length = (used + 63) & ~63UL;
    j->omj_Grants[0].og_Mode = OMCG_READ | OMCG_WRITE;
    j->omj_NGrants = 1;
    j->omj_TimeoutMS = JOB_TIMEOUT_MS;
    j->omj_Flags = OMCF_BOARD;         /* permissive reads: the code, libxmp's tables, ROM */
    j->omj_Target = OMC_ANY;
    t = eclock(p);
    st = OMC_Run68k(j);
    p->jobTicks += eclock(p) - t;
    p->mixFrames += n;
    if (st != OMCERR_OK || j->omj_Status != OMCERR_OK || j->omj_Where != OMCW_BOARD) {
        dlog("core job: status %ld/%ld where %ld vector %ld pc %08lx", (long)st, (long)j->omj_Status,
             (long)j->omj_Where, (long)j->omj_FaultVector, (unsigned long)j->omj_FaultPC);
        *failed = TRUE;
        return NULL;
    }
    p->jobs++;
    CacheClearE(p->coreMix, n * p->bpf, CACRF_ClearD);
    return (LONG)j->omj_Regs[0] < 0 ? NULL : p->coreMix;
}

/* media.decode/1 gives SVC_SECONDS at a time as 16-bit PCM; this keeps it
 * as it goes out (16-bit stereo, or 8-bit for Paula) and hands it out a
 * chunk at a time. */
static UBYTE *fillService(struct Player *p, ULONG n)
{
    struct ModInst *mi = p->mi;
    ULONG have = 0, bpf = p->bpf;

    while (have < n) {
        ULONG take;
        if (p->pcmAt >= p->pcmHave) {
            struct OSBuffer buf[4];
            ULONG extra[4], got = 0, piece = mi->rate * SVC_SECONDS, i, t, c = p->svcChannels;
            LONG st;
            memset(buf, 0, sizeof buf);
            buf[0].ob_Data = mi->data;
            buf[0].ob_Length = mi->size;
            buf[1].ob_Data = (UBYTE *)p->pcm16;
            buf[1].ob_Length = piece * c * 2;
            extra[0] = 2;
            extra[1] = mi->rate;
            extra[2] = extra[3] = 0;
            t = eclock(p);
            st = dt_service_call(&p->svc, MD_DECODE, p->svcPos, 2, buf, extra, &got, NULL);
            p->svcTicks += eclock(p) - t;
            p->svcCalls++;
            if (st != OSERR_OK || !got) {
                if (st == OSERR_OK && mi->repeat && p->svcPos) {
                    p->svcPos = 0;                /* round again */
                    continue;
                }
                break;
            }
            if (got > piece)
                got = piece;
            if (bpf == 4) {
                WORD *o = (WORD *)p->pcmOut;
                if (c == 2)
                    memcpy(o, p->pcm16, got * 4);
                else
                    for (i = 0; i < got; i++)
                        o[i * 2] = o[i * 2 + 1] = p->pcm16[i];
            } else
                for (i = 0; i < got; i++) {       /* Paula: the high byte of each sample */
                    p->pcmOut[i * 2] = (UBYTE)(p->pcm16[i * c] >> 8);
                    p->pcmOut[i * 2 + 1] = (UBYTE)(p->pcm16[i * c + c - 1] >> 8);
                }
            p->pcmHave = got;
            p->pcmAt = 0;
            p->svcPos += got;
        }
        take = p->pcmHave - p->pcmAt;
        if (take > n - have)
            take = n - have;
        memcpy(p->mix + have * bpf, p->pcmOut + p->pcmAt * bpf, take * bpf);
        p->pcmAt += take;
        have += take;
    }
    p->mixFrames += have;
    if (!have)
        return NULL;
    if (have < n)
        memset(p->mix + have * bpf, 0, (n - have) * bpf);
    return p->mix;
}

static void describeRung(struct Player *p, struct Audio *a);

/* The next chunk from whichever rung plays; NULL at the end of the song. */
static UBYTE *fill(struct Player *p, struct Audio *a, ULONG n)
{
    struct ModInst *mi = p->mi;
    UBYTE *r = NULL;
    BOOL failed;

    p->chunks++;
    switch (mi->rung) {
    case RUNG_CORE:
        r = fillCore(p, n, &failed);
        if (failed) {
            /* The board refused it or the job faulted: libxmp's state is
             * not to be trusted, so the song ends here, and the next play
             * starts afresh on this CPU. */
            mi->rung = RUNG_CPU;
            mi->coreFailed = 1;
            r = NULL;
        }
        break;
    case RUNG_SERVICE:
        r = fillService(p, n);
        break;
    default:
        r = fillCpu(p, n);
        break;
    }
    if ((p->chunks & (a->ahi ? 1 : 7)) == 0 || !r)        /* about once a second */
        describeRung(p, a);
    return r;
}

/* Mixes the next buffer into slot i and sends it. FALSE when the song is
 * over (nothing sent). */
static BOOL audioQueue(struct Player *p, struct Audio *a, int i)
{
    struct ModInst *mi = p->mi;
    UBYTE *src;
    ULONG n = a->chunk;
    int s;

    if (a->ended || !(src = fill(p, a, n))) {
        a->ended = TRUE;
        return FALSE;
    }
    if (a->ahi) {
        struct AHIRequest *r = a->areq[i];
        memcpy(a->abuf[i], src, n * 4);
        r->ahir_Std.io_Command = CMD_WRITE;
        r->ahir_Std.io_Data = a->abuf[i];
        r->ahir_Std.io_Length = n * 4;
        r->ahir_Std.io_Offset = 0;
        r->ahir_Type = AHIST_S16S;
        r->ahir_Frequency = mi->rate;
        r->ahir_Volume = (LONG)(mi->volume * 0x10000UL / 64);
        r->ahir_Position = 0x8000;
        /* played straight after the one before, which is playing */
        r->ahir_Link = a->lastSent >= 0 && a->busy[a->lastSent] && !a->done[a->lastSent] ? a->areq[a->lastSent] : NULL;
        a->done[i] = 0;
        dlog("ahi: send %ld linked to %08lx, %lu bytes at %lu Hz", (long)i, (unsigned long)r->ahir_Link,
             (unsigned long)r->ahir_Std.io_Length, (unsigned long)r->ahir_Frequency);
        SendIO((struct IORequest *)r);
        a->lastSent = i;
        a->busy[i] = 1;
        return TRUE;
    }
    {
        UBYTE *l = a->chip[i][0], *rr = a->chip[i][1];
        while (n--) {
            *l++ = *src++;
            *rr++ = *src++;
        }
    }
    for (s = 0; s < 2; s++) {
        struct IOAudio *q = a->req[i][s];
        /* Only the first write sets the period and volume: later ones
         * keep the channel's, so a change of volume (ADCMD_PERVOL) is
         * heard at once rather than after the queued buffers. */
        q->ioa_Request.io_Command = CMD_WRITE;
        q->ioa_Request.io_Flags = a->pervol ? ADIOF_PERVOL : 0;
        q->ioa_Data = a->chip[i][s];
        q->ioa_Length = a->chunk;
        q->ioa_Period = mi->period;
        q->ioa_Volume = mi->volume;
        q->ioa_Cycles = 1;
        BeginIO((struct IORequest *)q);
    }
    a->busy[i] = 1;
    a->pervol = FALSE;
    return TRUE;
}

/* Starts from where the song is. Paula: every buffer mixed and queued on
 * stopped channels, then both started together so left and right stay in
 * step. AHI: the requests sent one after the other, each linked to the one
 * before. */
static BOOL audioStart(struct Player *p, struct Audio *a)
{
    int i;

    if (!audioOpen(a))
        return FALSE;
    if (!a->ahi) {
        paulaCommand(a, CMD_STOP);
        a->pervol = TRUE;
    }
    for (i = 0; i < a->nbuf; i++)
        if (!audioQueue(p, a, i))
            break;
    if (!a->ahi)
        paulaCommand(a, CMD_START);
    return TRUE;
}

/* Requests played: mix the next ones into them. FALSE when nothing is
 * left playing. */
static BOOL audioRefill(struct Player *p, struct Audio *a)
{
    BOOL any = FALSE;
    int i;

    if (a->ahi) {
        /* in the order they were sent, so each links to the one before */
        ahiCollect(a);
        for (i = 0; i < a->nbuf; i++) {
            int k = a->head;
            if (!a->busy[k] || !a->done[k])
                break;
            if (a->areq[k]->ahir_Std.io_Error)
                a->ended = TRUE;
            a->busy[k] = a->done[k] = 0;
            a->head = (k + 1) % a->nbuf;
            if (!a->ended)
                audioQueue(p, a, k);
        }
    } else
        for (i = 0; i < a->nbuf; i++)
            if (a->busy[i] && CheckIO((struct IORequest *)a->req[i][0]) && CheckIO((struct IORequest *)a->req[i][1])) {
                BYTE e0 = WaitIO((struct IORequest *)a->req[i][0]);
                BYTE e1 = WaitIO((struct IORequest *)a->req[i][1]);
                a->busy[i] = 0;
                if (e0 || e1)
                    a->ended = TRUE;           /* channels taken away */
                else
                    audioQueue(p, a, i);
            }
    for (i = 0; i < a->nbuf; i++)
        if (a->busy[i])
            any = TRUE;
    return any;
}

/* libxmp from the top. With an arena, xmp_start_player's memory is taken
 * again from where it was (nothing is allocated while it plays). */
static BOOL startXmp(struct Player *p)
{
    if (p->arena.base) {
        om_arena_use(&p->arena);
        p->arena.used = p->arena.last = p->mark;
    }
    if (xmp_start_player(p->ctx, p->mi->rate, p->xmpFormat) < 0) {
        om_arena_use(NULL);
        return FALSE;
    }
    om_arena_use(NULL);
    xmp_set_player(p->ctx, XMP_PLAYER_INTERP, p->mi->interp);
    xmp_play_buffer(p->ctx, NULL, 0, 0);
    return TRUE;
}

static void rewindSong(struct Player *p)
{
    if (p->mi->rung == RUNG_SERVICE) {
        p->svcPos = p->pcmHave = p->pcmAt = 0;
        return;
    }
    xmp_end_player(p->ctx);
    startXmp(p);
}

static void signalEnd(struct ModInst *mi)
{
    if (mi->sigTask && mi->sigMask)
        Signal(mi->sigTask, mi->sigMask);
}

static const char *cpuName(void)
{
    UWORD f = SysBase->AttnFlags;
    return f & (1 << 7) ? "68060" : f & AFF_68040 ? "68040" : f & AFF_68030 ? "68030" : f & AFF_68020 ? "68020" : "68000";
}

/* OIA_DecodedBy and OIA_Stats, in words. Written under Forbid() so a
 * reader in another task never sees half a line. */
static void describeRung(struct Player *p, struct Audio *a)
{
    struct ModInst *mi = p->mi;
    char by[sizeof mi->decodedBy], st[sizeof mi->stats];
    const char *out = a->ahi ? ", through AHI" : ", through Paula (no AHI)";

    switch (mi->rung) {
    case RUNG_CORE:
        snprintf(by, sizeof by, "a cores board core%s", out);
        snprintf(st, sizeof st, "%lu jobs on the cores board (cpu.m68k/1), each %lu%% of its sound's time; %lu Hz",
                 (unsigned long)p->jobs, (unsigned long)loadPercent(p, p->jobTicks, p->mixFrames),
                 (unsigned long)mi->rate);
        break;
    case RUNG_SERVICE:
        snprintf(by, sizeof by, "media.decode/1 (the Nursery)%s", out);
        snprintf(st, sizeof st, "%lu pieces of %lu s from media.decode/1, %lu ms each; %lu Hz",
                 (unsigned long)p->svcCalls, (unsigned long)SVC_SECONDS,
                 p->svcCalls && p->efreq ? (unsigned long)((unsigned long long)p->svcTicks * 1000 / p->efreq / p->svcCalls) : 0UL,
                 (unsigned long)mi->rate);
        break;
    default:
        snprintf(by, sizeof by, "this Amiga's CPU%s", out);
        snprintf(st, sizeof st, "mixing takes %lu%% of the %s at %lu Hz%s",
                 (unsigned long)loadPercent(p, p->mixTicks, p->mixFrames), cpuName(),
                 (unsigned long)mi->rate, mi->coreFailed ? " (the cores board failed it)" :
                 " (no cores board or service to hand it to)");
        break;
    }
    Forbid();
    strcpy(mi->decodedBy, by);
    strcpy(mi->stats, st);
    Permit();
}

/* ENV:GFOpenImage/ModulePlayer: auto (the default), cpu, cores or service. */
static int envPlayer(void)
{
    char buf[16];
    LONG n = GetVar((CONST_STRPTR)"GFOpenImage/ModulePlayer", (STRPTR)buf, sizeof buf, GVF_GLOBAL_ONLY);
    if (n > 0) {
        if (buf[0] == 'c' && buf[1] == 'p')
            return RUNG_CPU;
        if (buf[0] == 'c' && buf[1] == 'o')
            return RUNG_CORE;
        if (buf[0] == 's')
            return RUNG_SERVICE;
    }
    return 0;
}

/* ENV:GFOpenImage/ModuleOutput: paula plays through Paula even with AHI. */
static BOOL envPaula(void)
{
    char buf[16];
    LONG n = GetVar((CONST_STRPTR)"GFOpenImage/ModuleOutput", (STRPTR)buf, sizeof buf, GVF_GLOBAL_ONLY);
    return n > 0 && (buf[0] == 'p' || buf[0] == 'P');
}

/* AHI unit 0's mixing rate, from its prefs (ENV:Sys/ahi.prefs, the AHIU
 * chunk for unit 0), so AHI need not resample; 44100 when it can't say. */
static ULONG ahiRate(void)
{
    UBYTE buf[512];
    ULONG rate = 44100;
    BPTR f = Open((CONST_STRPTR)"ENV:Sys/ahi.prefs", MODE_OLDFILE);
    LONG n, i;
    if (!f)
        return rate;
    n = Read(f, buf, sizeof buf);
    Close(f);
    for (i = 12; i + 20 <= n;) {
        ULONG len = get32(buf + i + 4);
        if (!memcmp(buf + i, "AHIU", 4) && len >= 12 && buf[i + 8] == 0) {
            ULONG r = get32(buf + i + 16);
            if (r >= 8000 && r <= 96000)
                rate = r;
            break;
        }
        i += 8 + len + (len & 1);
    }
    return rate;
}

/* Is there a cores board this datatype can hand jobs to? */
static BOOL coresThere(void)
{
    struct Library *b = OpenLibrary((CONST_STRPTR)OPENMULTICORE_NAME, 0);
    ULONG n;
    if (!b)
        return FALSE;
    OpenMulticoreBase = b;
    n = OMC_CoreCount();
    /* the job's code must be where a core can read it: not Chip RAM */
    if (!n || (TypeOfMem((APTR)om_job_mix) & MEMF_CHIP)) {
        CloseLibrary(b);
        return FALSE;
    }
    return TRUE;                      /* left open: the player closes it */
}

/* The service ready: open, and the sound's length and rate known. */
static BOOL serviceStart(struct Player *p, BOOL ahi)
{
    struct ModInst *mi = p->mi;
    struct OSBuffer buf[4];
    ULONG extra[4];
    UBYTE info[24];

    if (!dt_service_open(&p->svc, "media.decode/1"))
        return FALSE;
    p->svcOpen = TRUE;
    memset(buf, 0, sizeof buf);
    buf[0].ob_Data = mi->data;
    buf[0].ob_Length = mi->size;
    buf[1].ob_Data = info;
    buf[1].ob_Length = sizeof info;
    extra[0] = 2;
    extra[1] = ahi ? 48000 : mi->rate;    /* AHI takes any rate; Paula its own */
    extra[2] = extra[3] = 0;
    if (dt_service_call(&p->svc, MD_PROBE, 0, 2, buf, extra, NULL, NULL) != OSERR_OK || get32(info) != MD_KIND_SOUND)
        return FALSE;
    {
        ULONG frames = get32(info + 12), rate = get32(info + 16), ch = get32(info + 20);
        ULONG period;
        if (!frames || rate < 4000 || !ch || ch > 2)
            return FALSE;
        period = (SysBase->ex_EClockFrequency * 5 + rate / 2) / rate;
        if (!ahi && period < 124)
            return FALSE;
        p->svcChannels = ch;
        if (!(p->pcm16 = AllocVec(rate * SVC_SECONDS * ch * 2, MEMF_ANY)) ||
            !(p->pcmOut = AllocVec(rate * SVC_SECONDS * p->bpf, MEMF_ANY)))
            return FALSE;
        mi->rate = rate;
        mi->period = period;
        mi->frames = frames;
    }
    return TRUE;
}

static void serviceStop(struct Player *p)
{
    if (p->svcOpen)
        dt_service_close(&p->svc);
    p->svcOpen = FALSE;
    if (p->pcm16)
        FreeVec(p->pcm16);
    if (p->pcmOut)
        FreeVec(p->pcmOut);
    p->pcm16 = NULL;
    p->pcmOut = NULL;
}

/* Who mixes (DESIGN.md section 7), the Team's order: another core first,
 * for every module (when the cores board takes the first job); else
 * media.decode/1 on a services card or a paired Cradle; else this CPU.
 * ENV:GFOpenImage/ModulePlayer forces one (falling to the next when it is
 * not there). */
static void chooseRung(struct Player *p, struct Audio *a, BOOL cores)
{
    struct ModInst *mi = p->mi;
    int forced = envPlayer();
    BOOL failed;

    if (forced == RUNG_CPU)
        goto cpu;
    if ((!forced || forced == RUNG_CORE) && cores) {
        fillCore(p, a->chunk, &failed);
        p->jobTicks = p->mixFrames = p->jobs = 0;
        xmp_end_player(p->ctx);
        startXmp(p);
        if (!failed) {
            mi->rung = RUNG_CORE;
            return;
        }
        mi->coreFailed = 1;
    }
    if (serviceStart(p, a->ahi)) {
        mi->rung = RUNG_SERVICE;
        return;
    }
    serviceStop(p);
cpu:
    mi->rung = RUNG_CPU;
}

static void playerMain(void)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    struct PlayerMsg *start, *pm;
    struct ModInst *mi;
    struct xmp_module_info info;
    struct Audio a;
    struct Player pl, *p = &pl;
    struct Library *dbas, *dtrans, *sbas;      /* this player's own opens */
    LONG err = DTERROR_INVALID_DATA;
    UWORD lastVolume;
    BOOL cores = FALSE, omc = FALSE;
    ULONG maxChunk;
    int i, s;

    WaitPort(&me->pr_MsgPort);
    start = (struct PlayerMsg *)GetMsg(&me->pr_MsgPort);
    mi = start->pm_Inst;

    memset(&a, 0, sizeof a);
    memset(p, 0, sizeof *p);
    p->mi = mi;
    dbas = OpenLibrary((CONST_STRPTR)"mathieeedoubbas.library", 34);
    dtrans = OpenLibrary((CONST_STRPTR)"mathieeedoubtrans.library", 34);
    sbas = OpenLibrary((CONST_STRPTR)"mathieeesingbas.library", 34);
    if (!dbas || !dtrans || !sbas) {
        err = ERROR_INVALID_RESIDENT_LIBRARY;
        goto fail;
    }
    Forbid();
    MathIeeeDoubBasBase = dbas;
    MathIeeeDoubTransBase = dtrans;
    MathIeeeSingBasBase = sbas;
    Permit();
    if (!OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK, (struct IORequest *)&p->treq, 0)) {
        TimerBase = p->treq.tr_node.io_Device;
        p->timer = TRUE;
        eclock(p);
    }
    if (!(mi->port = CreateMsgPort()) || !(a.port = CreateMsgPort())) {
        err = ERROR_NO_FREE_STORE;
        goto fail;
    }

    /* AHI's unit 0, the one AHI prefs sets up, in 16-bit stereo at its own
     * rate; without AHI, Paula as before. */
    if (!envPaula() && (a.ahiDev = (struct AHIRequest *)CreateIORequest(a.port, sizeof(struct AHIRequest)))) {
        a.ahiDev->ahir_Version = 4;
        if (!OpenDevice((CONST_STRPTR)AHINAME, AHI_DEFAULT_UNIT, (struct IORequest *)a.ahiDev, 0))
            a.ahi = TRUE;
        else {
            DeleteIORequest((struct IORequest *)a.ahiDev);
            a.ahiDev = NULL;
        }
    }
    if (a.ahi) {
        mi->rate = ahiRate();
        mi->period = (SysBase->ex_EClockFrequency * 5 + mi->rate / 2) / mi->rate;
        p->bpf = 4;
        p->xmpFormat = 0;                      /* 16-bit stereo */
        a.nbuf = NBUF_AHI;
        a.chunk = (mi->rate / CHUNK_DIV_AHI) & ~1UL;
    } else {
        p->bpf = 2;
        p->xmpFormat = XMP_FORMAT_8BIT;
        a.nbuf = NBUF;
        a.chunk = (mi->rate / CHUNK_DIV) & ~1UL;
    }
    maxChunk = ((mi->rate > 48000 ? mi->rate : 48000) / 2 + 2);   /* the longest chunk: AHI's half second */

    /* With a cores board, libxmp's memory is one arena in Fast RAM that a
     * job may write (xmpglue.h): the module's size a few times over, and
     * room for the player; at most what a job may write (16 MB). */
    if (envPlayer() != RUNG_CPU && envPlayer() != RUNG_SERVICE && (omc = cores = coresThere())) {
        ULONG want = mi->size * 3 + 1024 * 1024 + mi->rate * 16;
        if (want > ARENA_MAX || !om_arena_init(&p->arena, want)) {
            om_arena_free(&p->arena);
            cores = FALSE;
        }
    }
    for (;;) {
        om_arena_use(p->arena.base ? &p->arena : NULL);
        if ((p->ctx = xmp_create_context())) {
            i = xmp_load_module_from_memory(p->ctx, mi->data, mi->size);
            if (i >= 0)
                break;
            xmp_free_context(p->ctx);
            p->ctx = NULL;
        } else
            i = -XMP_ERROR_SYSTEM;
        om_arena_use(NULL);
        if (!p->arena.base) {
            err = i == -XMP_ERROR_SYSTEM ? ERROR_NO_FREE_STORE : DTERROR_INVALID_DATA;
            goto fail;
        }
        om_arena_free(&p->arena);              /* too small, perhaps: without it */
        cores = FALSE;
    }
    xmp_get_module_info(p->ctx, &info);
    strncpy(mi->title, info.mod->name, XMP_NAME_SIZE);
    strncpy(mi->type, info.mod->type, XMP_NAME_SIZE);
    mi->ms = info.seq_data[0].duration;
    mi->frames = mi->ms / 1000 * mi->rate + mi->ms % 1000 * mi->rate / 1000;

    if (p->arena.base) {
        p->coreMix = malloc(maxChunk * p->bpf);   /* in the arena */
        p->jobStack = AllocVec(JOB_STACK, MEMF_FAST | MEMF_PUBLIC);
        if (!p->coreMix || !p->jobStack)
            cores = FALSE;
        p->mark = p->arena.used;
    }
    om_arena_use(NULL);
    if (!startXmp(p))
        goto fail;
    if (!(p->mix = AllocVec(maxChunk * p->bpf, MEMF_ANY))) {
        err = ERROR_NO_FREE_STORE;
        goto fail;
    }

    chooseRung(p, &a, cores);
    if (mi->rung == RUNG_SERVICE) {            /* its rate; libxmp is not needed */
        a.chunk = (mi->rate / (a.ahi ? CHUNK_DIV_AHI : CHUNK_DIV_SVC)) & ~1UL;
        xmp_end_player(p->ctx);
        xmp_release_module(p->ctx);
        xmp_free_context(p->ctx);
        p->ctx = NULL;
        om_arena_free(&p->arena);
    }
    describeRung(p, &a);
    dlog("rung %ld, %s, %lu channels, rate %lu", (long)mi->rung, a.ahi ? "AHI" : "Paula",
         (unsigned long)info.mod->chn, (unsigned long)mi->rate);

    if (a.ahi) {
        for (i = 0; i < a.nbuf; i++) {
            if (!(a.areq[i] = AllocVec(sizeof(struct AHIRequest), MEMF_PUBLIC | MEMF_CLEAR)) ||
                !(a.abuf[i] = AllocVec(a.chunk * 4, MEMF_PUBLIC))) {
                err = ERROR_NO_FREE_STORE;
                goto fail;
            }
            CopyMem(a.ahiDev, a.areq[i], sizeof(struct AHIRequest));
        }
    } else {
        a.ctl = (struct IOAudio *)CreateIORequest(a.port, sizeof(struct IOAudio));
        if (!a.ctl) {
            err = ERROR_NO_FREE_STORE;
            goto fail;
        }
        for (i = 0; i < NBUF; i++)
            for (s = 0; s < 2; s++)
                if (!(a.req[i][s] = AllocVec(sizeof(struct IOAudio), MEMF_PUBLIC | MEMF_CLEAR)) ||
                    !(a.chip[i][s] = AllocVec(a.chunk, MEMF_CHIP))) {
                    err = ERROR_NO_FREE_STORE;
                    goto fail;
                }
    }

    start->pm_Result = 0;
    ReplyMsg(&start->pm_Msg);
    start = NULL;
    lastVolume = mi->volume;

    for (;;) {
        ULONG sigs = Wait(1UL << mi->port->mp_SigBit | 1UL << a.port->mp_SigBit | SIGBREAKF_CTRL_E | SIGBREAKF_CTRL_F);
        BOOL kick = FALSE, quit = FALSE;
        UWORD cmd;
        /* The volume: at once on Paula's playing channels; AHI's requests
         * each carry it, so it is heard from the next one sent. */
        if (mi->volume != lastVolume && a.open && !a.ahi) {
            lastVolume = mi->volume;
            a.ctl->ioa_Request.io_Command = ADCMD_PERVOL;
            a.ctl->ioa_Request.io_Flags = 0;
            a.ctl->ioa_Period = mi->period;
            a.ctl->ioa_Volume = lastVolume;
            DoIO((struct IORequest *)a.ctl);
        }

        if (sigs & SIGBREAKF_CTRL_F) {
            Forbid();
            cmd = mi->want;
            mi->want = PC_NONE;
            Permit();
            dlog("player: command %ld in state %ld", (long)cmd, (long)mi->state);
            if (cmd == PC_REWIND || cmd == PC_STOP) {
                if (mi->state != PS_STOPPED) {
                    audioClose(&a);
                    rewindSong(p);
                    mi->state = PS_STOPPED;
                }
                if (cmd == PC_REWIND)
                    cmd = PC_PLAY;
            }
            if (cmd == PC_PLAY) {
                if (mi->state == PS_PAUSED) {
                    if (a.ahi) {               /* on from where the mixing is */
                        if (audioStart(p, &a))
                            mi->state = a.ended ? PS_DRAINING : PS_PLAYING;
                    } else {
                        paulaCommand(&a, CMD_START);
                        mi->state = a.ended ? PS_DRAINING : PS_PLAYING;
                    }
                } else if (mi->state == PS_STOPPED)
                    kick = TRUE;
            } else if (cmd == PC_PAUSE) {
                if (mi->state == PS_PLAYING || mi->state == PS_DRAINING) {
                    /* AHI: the queued requests are dropped (what they held,
                     * at most a second, is skipped); stopping the unit
                     * would stop other programs too */
                    if (a.ahi)
                        audioClose(&a);
                    else
                        paulaCommand(&a, CMD_STOP);
                    mi->state = PS_PAUSED;
                }
            }
        }

        while ((pm = (struct PlayerMsg *)GetMsg(mi->port))) {
            if (pm->pm_Cmd == PC_QUIT) {
                quit = TRUE;
                start = pm;                       /* replied on the way out */
                break;
            }
            pm->pm_Result = 0;
            ReplyMsg(&pm->pm_Msg);
        }
        if (quit)
            break;

        if (kick) {
            lastVolume = mi->volume;
            if (audioStart(p, &a))
                mi->state = PS_PLAYING;
        }

        if (a.open && mi->state != PS_PAUSED) {
            BOOL any = audioRefill(p, &a);
            if (a.ended && mi->state == PS_PLAYING)
                mi->state = PS_DRAINING;
            if (!any) {                           /* the song is over */
                audioClose(&a);
                rewindSong(p);
                mi->state = PS_STOPPED;
                signalEnd(mi);
            }
        }
    }

    audioClose(&a);
    err = 0;
fail:
    dlog("player: cleanup, err %ld", (long)err);
    audioClose(&a);
    if (p->ctx) {
        xmp_end_player(p->ctx);
        xmp_release_module(p->ctx);
        xmp_free_context(p->ctx);
    }
    om_arena_use(NULL);
    om_arena_free(&p->arena);
    serviceStop(p);
    if (p->jobStack)
        FreeVec(p->jobStack);
    if (p->mix)
        FreeVec(p->mix);
    for (i = 0; i < NBUF; i++) {
        for (s = 0; s < 2; s++) {
            if (a.req[i][s])
                FreeVec(a.req[i][s]);
            if (a.chip[i][s])
                FreeVec(a.chip[i][s]);
        }
        if (a.areq[i])
            FreeVec(a.areq[i]);
        if (a.abuf[i])
            FreeVec(a.abuf[i]);
    }
    if (a.ahiDev) {
        CloseDevice((struct IORequest *)a.ahiDev);
        DeleteIORequest((struct IORequest *)a.ahiDev);
    }
    if (a.ctl)
        DeleteIORequest((struct IORequest *)a.ctl);
    if (a.port)
        DeleteMsgPort(a.port);
    if (mi->port) {
        DeleteMsgPort(mi->port);
        mi->port = NULL;
    }
    if (omc)
        CloseLibrary(OpenMulticoreBase);
    if (p->timer)
        CloseDevice((struct IORequest *)&p->treq);
    CloseLibrary(sbas);                         /* this player's opens only; NULL is allowed */
    CloseLibrary(dtrans);
    CloseLibrary(dbas);
    /* Forbid() until the process is gone: the code it runs is the
     * datatype's, which may be unloaded as soon as the reply is seen. */
    Forbid();
    if (start) {
        start->pm_Result = err;
        ReplyMsg(&start->pm_Msg);
    }
}

/* --- talking to the player ---------------------------------------------------- */

/* Asks the player to play, pause, stop or rewind, from any task. */
static void want(struct ModInst *mi, UWORD cmd)
{
    if (!mi->player)
        return;
    mi->want = cmd;
    Signal(&mi->player->pr_Task, SIGBREAKF_CTRL_F);
}

/* A message to the player, waiting for its answer (PC_QUIT). */
static LONG sendCommand(struct ModInst *mi, ULONG cmd)
{
    struct PlayerMsg pm;
    struct MsgPort *reply;

    if (!mi->port)
        return -1;
    if (!(reply = CreateMsgPort()))
        return -1;
    memset(&pm, 0, sizeof pm);
    pm.pm_Msg.mn_ReplyPort = reply;
    pm.pm_Msg.mn_Length = sizeof pm;
    pm.pm_Cmd = cmd;
    pm.pm_Inst = mi;
    dlog("send %ld", (long)cmd);
    PutMsg(mi->port, &pm.pm_Msg);
    WaitPort(reply);
    dlog("send %ld: answered", (long)cmd);
    GetMsg(reply);
    DeleteMsgPort(reply);
    return pm.pm_Result;
}

/* Reads the file, starts the player, which loads the module; FALSE (with
 * the reason in IoErr()) when libxmp can't. */
static BOOL loadModule(Class *cl, Object *o)
{
    struct ModInst *mi = INST_DATA(cl, o);
    struct PlayerMsg pm;
    struct MsgPort *reply;
    STRPTR name = NULL;
    ULONG rate;

    mi->interp = envInterp();
    rate = envNumber("GFOpenImage/SoundRate");
    if (rate < 4000)
        rate = fastCpu ? 28000 : 16000;
    /* Paula: the period is whole colour clocks, 124 at the least. */
    mi->period = (SysBase->ex_EClockFrequency * 5 + rate / 2) / rate;
    if (mi->period < 124)
        mi->period = 124;
    mi->rate = (SysBase->ex_EClockFrequency * 5 + mi->period / 2) / mi->period;

    GetDTAttrs(o, DTA_Name, (ULONG)&name, TAG_DONE);
    if (!(mi->data = dt_read_source(o, &mi->size)))
        return FALSE;
    if (!(reply = CreateMsgPort())) {
        SetIoErr(ERROR_NO_FREE_STORE);
        return FALSE;
    }
    mi->player = CreateNewProcTags(NP_Entry, (ULONG)playerMain, NP_Name, (ULONG)"openmodule player",
                                   NP_StackSize, PLAYER_STACK, NP_Priority, PLAYER_PRI,
                                   NP_Input, 0, NP_Output, 0, NP_CloseInput, FALSE, NP_CloseOutput, FALSE,
                                   TAG_DONE);
    if (!mi->player) {
        DeleteMsgPort(reply);
        SetIoErr(ERROR_NO_FREE_STORE);
        return FALSE;
    }
    memset(&pm, 0, sizeof pm);
    pm.pm_Msg.mn_ReplyPort = reply;
    pm.pm_Msg.mn_Length = sizeof pm;
    pm.pm_Inst = mi;
    PutMsg(&mi->player->pr_MsgPort, &pm.pm_Msg);
    WaitPort(reply);
    GetMsg(reply);
    DeleteMsgPort(reply);
    if (pm.pm_Result) {
        mi->player = NULL;                    /* it has gone */
        SetIoErr(pm.pm_Result);
        return FALSE;
    }

    SetDTAttrs(o, NULL, NULL,
        DTA_ObjName, (ULONG)(mi->title[0] ? mi->title : (name ? (char *)FilePart(name) : "Module")),
        DTA_ObjAnnotation, (ULONG)mi->type,
        TAG_DONE);
    return TRUE;
}

static void disposeModule(struct ModInst *mi)
{
    if (mi->player && mi->port)
        sendCommand(mi, PC_QUIT);
    mi->player = NULL;
    if (mi->data) {
        FreeVec(mi->data);
        mi->data = NULL;
    }
}

/* The attributes the object keeps for itself, from OM_NEW and OM_SET. */
static void takeAttrs(struct ModInst *mi, struct TagItem *tags, BOOL isNew)
{
    struct TagItem *ti, *state = tags;
    UWORD volume = mi->volume;

    while ((ti = NextTagItem(&state))) {
        switch (ti->ti_Tag) {
        case SDTA_Volume:
            volume = ti->ti_Data > 64 ? 64 : (UWORD)ti->ti_Data;
            break;
        case DTA_Repeat:
            mi->repeat = ti->ti_Data ? 1 : 0;
            break;
        case DTA_Immediate:
            mi->immediate = ti->ti_Data ? 1 : 0;
            break;
        case SDTA_SignalTask:
            mi->sigTask = (struct Task *)ti->ti_Data;
            break;
        case SDTA_SignalBit:
            mi->sigMask = ti->ti_Data;
            break;
        case SDTA_SignalBitNumber:
            mi->sigMask = (LONG)ti->ti_Data >= 0 && ti->ti_Data < 32 ? 1UL << ti->ti_Data : 0;
            break;
        }
    }
    if (volume != mi->volume) {
        mi->volume = volume;
        if (!isNew && mi->player)
            Signal(&mi->player->pr_Task, SIGBREAKF_CTRL_E);
    }
}

static void trigger(struct ModInst *mi, ULONG function)
{
    switch (function & STMF_METHOD_MASK) {
    case STM_PLAY:
    case STM_RESUME:
        want(mi, PC_PLAY);
        break;
    case STM_PAUSE:
        want(mi, PC_PAUSE);
        break;
    case STM_STOP:
        want(mi, PC_STOP);
        break;
    case STM_REWIND:
        want(mi, PC_REWIND);
        break;
    }
}

ULONG dt_dispatch(Class *cl, Object *o, Msg msg)
{
    struct ModInst *mi;

    switch (msg->MethodID) {
    case OM_NEW: {
        Object *obj = (Object *)DoSuperMethodA(cl, o, msg);
        if (obj) {
            mi = INST_DATA(cl, obj);
            memset(mi, 0, sizeof *mi);
            mi->volume = 64;
            takeAttrs(mi, ((struct opSet *)msg)->ops_AttrList, TRUE);
            if (!loadModule(cl, obj)) {
                LONG err = IoErr();
                CoerceMethod(cl, obj, OM_DISPOSE);
                SetIoErr(err);
                obj = NULL;
            }
        }
        return (ULONG)obj;
    }
    case OM_DISPOSE:
        disposeModule(INST_DATA(cl, o));
        return DoSuperMethodA(cl, o, msg);
    case OM_SET:
    case OM_UPDATE:
        takeAttrs(INST_DATA(cl, o), ((struct opSet *)msg)->ops_AttrList, FALSE);
        return DoSuperMethodA(cl, o, msg);
    case OM_GET: {
        struct opGet *g = (struct opGet *)msg;
        mi = INST_DATA(cl, o);
        switch (g->opg_AttrID) {
        case SDTA_SampleLength:
            *g->opg_Storage = mi->frames;
            return TRUE;
        case SDTA_SamplesPerSec:
            *g->opg_Storage = mi->rate;
            return TRUE;
        case SDTA_Period:
            *g->opg_Storage = mi->period;
            return TRUE;
        case SDTA_Volume:
            *g->opg_Storage = mi->volume;
            return TRUE;
        case DTA_Repeat:
            *g->opg_Storage = mi->repeat;
            return TRUE;
        case SDTA_ReplayPeriod:
            /* As sound.datatype answers it: the address of a timeval the
             * object keeps (struct timeval *tv; GetDTAttrs(o,
             * SDTA_ReplayPeriod, &tv)), not one the caller passes in.
             * Looping, both fields are all ones, as a continuous sound's. */
            mi->replay.tv_secs = mi->repeat ? 0xFFFFFFFFUL : mi->ms / 1000;
            mi->replay.tv_micro = mi->repeat ? 0xFFFFFFFFUL : mi->ms % 1000 * 1000;
            *g->opg_Storage = (ULONG)&mi->replay;
            return TRUE;
        case DTA_TriggerMethods:
            *g->opg_Storage = (ULONG)triggers;
            return TRUE;
        case OIA_DecodedBy:
            *g->opg_Storage = (ULONG)mi->decodedBy;
            return TRUE;
        case OIA_Stats:
            *g->opg_Storage = (ULONG)mi->stats;
            return TRUE;
        case OIA_Rung:
            *g->opg_Storage = mi->rung;
            return TRUE;
        }
        return DoSuperMethodA(cl, o, msg);
    }
    case DTM_TRIGGER:
        trigger(INST_DATA(cl, o), ((struct dtTrigger *)msg)->dtt_Function);
        return TRUE;
    case GM_GOACTIVE: {
        /* A click plays, as it does a sound. */
        mi = INST_DATA(cl, o);
        if (mi->state != PS_PLAYING && mi->state != PS_DRAINING)
            want(mi, PC_PLAY);
        return GMR_NOREUSE;
    }
    case GM_LAYOUT:
    case DTM_PROCLAYOUT: {
        ULONG r = DoSuperMethodA(cl, o, msg);
        mi = INST_DATA(cl, o);
        if (mi->immediate && !mi->started) {
            mi->started = 1;
            want(mi, PC_PLAY);
        }
        return r;
    }
    case DTM_WRITE: {
        /* Saved as it came: there is no 8SVX of a module to write. */
        struct dtWrite *w = (struct dtWrite *)msg;
        mi = INST_DATA(cl, o);
        if (w->dtw_Mode == DTWM_RAW && w->dtw_FileHandle && mi->data)
            return Write(w->dtw_FileHandle, mi->data, mi->size) == (LONG)mi->size;
        SetIoErr(ERROR_NOT_IMPLEMENTED);
        return FALSE;
    }
    case DTM_COPY:
        SetIoErr(ERROR_NOT_IMPLEMENTED);
        return FALSE;
    default:
        return DoSuperMethodA(cl, o, msg);
    }
}
