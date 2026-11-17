/*
 * gfopenmidi.datatype: MIDI files (.mid, .midi, .kar, .rmi) for AmigaOS
 * 3.x, played by GFOpenSynth, the AmiCradle Team's own synth. A
 * sound.datatype subclass of the streaming kind (sound.datatype V44:
 * SDTA_Sample stays NULL, SDTA_SampleLength and SDTA_SamplesPerSec give
 * the length, DTM_TRIGGER plays, pauses and stops), as
 * gfopenmodule.datatype is for modules.
 *
 * The datatype holds the file and a synth of gfopensynth.library's
 * (GSOpenSynthA). The library chooses where the music sounds: on
 * AkikoPlus's MIDI port when the board has one, where the host's synth
 * plays it as an MT-32 or a General MIDI box plugged into that port would;
 * else on this CPU from a SoundFont, to ahi.device. The library's player
 * process keeps the time, so nothing here mixes.
 *
 * For programs: opened with OIA_Reader, nothing plays. The synth runs on
 * this CPU (GSOUT_CALLER) at OIA_MixRate and OIM_READSOUND hands out
 * 16-bit stereo a piece at a time, once through the file, then a second
 * for the last notes to die away.
 *
 * ENV:GFOpenImage/MidiEngine set to "mt32" plays every file as an MT-32
 * would (its programs and channels 2 to 10); otherwise General MIDI.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <string.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/var.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/soundclass.h>
#include <intuition/classes.h>
#include <intuition/gadgetclass.h>
#include <utility/tagitem.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/datatypes.h>
#include <clib/alib_protos.h>
#include <datatypes/gfopenimage.h>
#include <libraries/gfopensynth.h>

#include "dtlib.h"
#include "midifile.h"

struct Library *GFOpenSynthBase;
#define __NOLIBBASE__
#include <inline/gfopensynth.h>

const char LibName[] = "gfopenmidi.datatype";
const char LibIdString[] = "gfopenmidi.datatype 47.1 (17.11.2026) AmiCradle Team, played by GFOpenSynth";
const UWORD LibVersion = 47;
const UWORD LibRevision = 1;
static const char version[] __attribute__((used)) = "$VER: gfopenmidi.datatype 47.1 (17.11.2026)";

const char dt_superclass[] = "sound.datatype";
const UWORD dt_superversion = 39;

#define PLAY_RATE 22050                /* the length's unit while the library plays: its own default rate */
#define TAIL_MS 1000                   /* a reader's last notes dying away */
#define WATCH_MICROS 200000            /* the control process looks five times a second while a file plays */

enum { MS_STOPPED, MS_PLAYING, MS_PAUSED };
enum { PC_NONE, PC_PLAY, PC_PAUSE, PC_STOP };

struct MidiInst {
    struct GSSynth *synth;
    UBYTE *data;                   /* the file as it came, kept for DTM_WRITE */
    ULONG size;
    const UBYTE *midi;             /* the MIDI file in it (past a .rmi's wrapper) */
    ULONG midiLen;
    ULONG rate, frames, ms;
    ULONG engine;
    volatile UWORD volume;
    volatile UWORD repeat;
    volatile UWORD state;
    UWORD immediate, started;
    struct Task *sigTask;
    ULONG sigMask;
    struct Process *control;       /* the object's control process (not a reader's) */
    volatile UWORD want;
    struct MsgPort *quitPort;      /* where quitMsg comes when the process has gone */
    struct Message quitMsg;
    struct timeval replay;
    UWORD reader;                  /* OIA_Reader: OIM_READSOUND, no playing */
    ULONG mixRate;
    ULONG readDone, readAll;       /* frames handed out, and all there are */
    char title[64];
    char type[48];
    char decodedBy[64];
};

ULONG dt_instsize = sizeof(struct MidiInst);

static const struct DTMethod triggers[] = {
    {(STRPTR)"Play", (STRPTR)"PLAY", STM_PLAY},
    {(STRPTR)"Pause", (STRPTR)"PAUSE", STM_PAUSE},
    {(STRPTR)"Stop", (STRPTR)"STOP", STM_STOP},
    {NULL, NULL, 0}
};

BOOL dt_init(void)
{
    return TRUE;
}

void dt_cleanup(void)
{
    if (GFOpenSynthBase) {
        CloseLibrary(GFOpenSynthBase);
        GFOpenSynthBase = NULL;
    }
}

/* gfopensynth.library, opened at the first MIDI file, kept until the
 * datatype goes. */
static BOOL synthLib(void)
{
    BOOL ok;
    Forbid();
    if (!GFOpenSynthBase)
        GFOpenSynthBase = OpenLibrary((CONST_STRPTR)GFOPENSYNTHNAME, GFOPENSYNTH_VERSION);
    ok = GFOpenSynthBase != NULL;
    Permit();
    return ok;
}

static ULONG envEngine(void)
{
    char buf[16];
    LONG n = GetVar((STRPTR)"GFOpenImage/MidiEngine", (STRPTR)buf, sizeof buf, GVF_GLOBAL_ONLY);
    if (n >= 4 && (buf[0] | 0x20) == 'm' && (buf[1] | 0x20) == 't' && buf[2] == '3' && buf[3] == '2')
        return GSE_MT32;
    return GSE_GM;
}

/* The track name of a file's first track (meta event 3), when it starts
 * the track as it almost always does: "" otherwise. */
static void trackName(const UBYTE *midi, ULONG len, char *out, ULONG max)
{
    ULONG at = 8 + ((ULONG)midi[4] << 24 | (ULONG)midi[5] << 16 | (ULONG)midi[6] << 8 | midi[7]);
    ULONG end, n, i;
    out[0] = 0;
    if (at + 8 > len || memcmp(midi + at, "MTrk", 4) != 0)
        return;
    end = at + 8 + ((ULONG)midi[at + 4] << 24 | (ULONG)midi[at + 5] << 16 | (ULONG)midi[at + 6] << 8 | midi[at + 7]);
    if (end > len)
        end = len;
    at += 8;
    while (at + 3 < end && midi[at] == 0 && midi[at + 1] == 0xFF) {     /* meta events at time 0 */
        UBYTE kind = midi[at + 2];
        at += 3;
        n = 0;
        do {
            if (at >= end)
                return;
            n = n << 7 | (midi[at] & 0x7F);
        } while (midi[at++] & 0x80);
        if (n > end - at)
            return;
        if (kind == 3) {
            if (n >= max)
                n = max - 1;
            for (i = 0; i < n; i++)
                out[i] = midi[at + i] >= 32 ? (char)midi[at + i] : ' ';
            out[n] = 0;
            return;
        }
        at += n;
    }
}

static void signalEnd(struct MidiInst *mi)
{
    if (mi->sigTask && mi->sigMask)
        Signal(mi->sigTask, mi->sigMask);
}

/* Play from the start, or from where a pause left it: in the control process. */
static void doPlay(struct MidiInst *mi)
{
    struct TagItem tags[2];
    if (mi->state == MS_PAUSED) {
        GSPauseFile(mi->synth, FALSE);
        mi->state = MS_PLAYING;
        return;
    }
    if (mi->state == MS_PLAYING && GSGetAttr(mi->synth, GSA_Playing))
        return;
    tags[0].ti_Tag = GSA_Loop;
    tags[0].ti_Data = mi->repeat;
    tags[1].ti_Tag = TAG_DONE;
    GSSetVolume(mi->synth, (ULONG)mi->volume * GS_VOLUME_FULL / 64);
    if (GSPlayMemA(mi->synth, mi->midi, mi->midiLen, tags))
        mi->state = MS_PLAYING;
}

/* The control process, one an object. Commands come as a word in the
 * object and SIGBREAKF_CTRL_F, so they can be given from any task (a click
 * on the gadget comes from input.device's, which must not wait or call
 * DOS) and the latest wins; the library is only ever called from here.
 * While a file plays it looks five times a second (timer.device) whether
 * the library's player has reached the end, which the library does not
 * say, and then sets the object back and signals SDTA_SignalTask, as
 * sound.datatype does at a sound's end. SIGBREAKF_CTRL_E: a new volume.
 * SIGBREAKF_CTRL_C: the object is going. */
static void controlMain(void)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    struct MidiInst *mi = (struct MidiInst *)me->pr_Task.tc_UserData;
    struct MsgPort *tp = CreateMsgPort();
    struct timerequest *tr = tp ? (struct timerequest *)CreateIORequest(tp, sizeof *tr) : NULL;
    BOOL timer = tr && !OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)tr, 0);
    BOOL ticking = FALSE;
    ULONG tsig = tp ? 1UL << tp->mp_SigBit : 0;

    for (;;) {
        ULONG got;
        if (timer && !ticking && mi->state == MS_PLAYING) {
            tr->tr_node.io_Command = TR_ADDREQUEST;
            tr->tr_time.tv_secs = 0;
            tr->tr_time.tv_micro = WATCH_MICROS;
            SendIO((struct IORequest *)tr);
            ticking = TRUE;
        }
        got = Wait(SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_E | SIGBREAKF_CTRL_F | tsig);
        if (ticking && (got & tsig) && GetMsg(tp))
            ticking = FALSE;
        if (got & SIGBREAKF_CTRL_C)
            break;
        if (got & SIGBREAKF_CTRL_F) {
            UWORD cmd;
            Disable();
            cmd = mi->want;
            mi->want = PC_NONE;
            Enable();
            switch (cmd) {
            case PC_PLAY:
                doPlay(mi);
                break;
            case PC_PAUSE:
                if (mi->state == MS_PLAYING) {
                    GSPauseFile(mi->synth, TRUE);
                    mi->state = MS_PAUSED;
                }
                break;
            case PC_STOP:
                mi->state = MS_STOPPED;
                GSStopFile(mi->synth);
                break;
            }
        }
        if (got & SIGBREAKF_CTRL_E)
            GSSetVolume(mi->synth, (ULONG)mi->volume * GS_VOLUME_FULL / 64);
        if (mi->state == MS_PLAYING && !GSGetAttr(mi->synth, GSA_Playing)) {
            mi->state = MS_STOPPED;                /* the file ended by itself */
            signalEnd(mi);
        }
    }
    if (ticking) {
        AbortIO((struct IORequest *)tr);
        WaitIO((struct IORequest *)tr);
    }
    if (timer)
        CloseDevice((struct IORequest *)tr);
    if (tr)
        DeleteIORequest((struct IORequest *)tr);
    if (tp)
        DeleteMsgPort(tp);
    GSStopFile(mi->synth);
    Forbid();                       /* until this process has gone: the datatype's code stays */
    PutMsg(mi->quitPort, &mi->quitMsg);
}

static void want(struct MidiInst *mi, UWORD cmd)
{
    if (!mi->control)
        return;
    mi->want = cmd;
    Signal(&mi->control->pr_Task, SIGBREAKF_CTRL_F);
}

static void trigger(struct MidiInst *mi, ULONG function)
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
    case STM_REWIND:
        want(mi, PC_STOP);
        break;
    }
}

/* OIM_READSOUND: the next frames, mixed on this CPU */
static ULONG readSound(struct MidiInst *mi, struct oiReadSound *m)
{
    ULONG n;
    if (!mi->reader || !mi->synth || !m->oirs_Buffer) {
        SetIoErr(mi->reader ? ERROR_REQUIRED_ARG_MISSING : ERROR_NOT_IMPLEMENTED);
        return 0;
    }
    n = m->oirs_Frames;
    if (n > mi->readAll - mi->readDone)
        n = mi->readAll - mi->readDone;
    if (!n) {
        SetIoErr(0);
        return 0;
    }
    n = GSRenderS16(mi->synth, m->oirs_Buffer, n);
    mi->readDone += n;
    return n;
}

/* Reads the file and makes its synth; FALSE with the reason in IoErr(). */
static BOOL loadMidi(Class *cl, Object *o)
{
    struct MidiInst *mi = INST_DATA(cl, o);
    struct midi_head head;
    struct TagItem tags[5];
    STRPTR name = NULL;
    int t = 0;

    GetDTAttrs(o, DTA_Name, (ULONG)&name, TAG_DONE);
    if (!(mi->data = dt_read_source(o, &mi->size)))
        return FALSE;
    if (!midi_file_head(mi->data, mi->size, &head)) {
        SetIoErr(DTERROR_INVALID_DATA);
        return FALSE;
    }
    mi->midi = mi->data + head.start;
    mi->midiLen = mi->size - head.start;
    midi_file_kind(&head, mi->type);
    trackName(mi->midi, mi->midiLen, mi->title, sizeof mi->title);

    if (!synthLib()) {
        SetIoErr(ERROR_OBJECT_NOT_FOUND);       /* no gfopensynth.library in LIBS: */
        return FALSE;
    }
    mi->engine = envEngine();
    mi->ms = GSMemLength(mi->midi, mi->midiLen);
    if (!mi->ms) {
        SetIoErr(DTERROR_INVALID_DATA);
        return FALSE;
    }
    mi->rate = mi->reader ? mi->mixRate : PLAY_RATE;
    mi->frames = midi_frames(mi->ms + (mi->reader ? TAIL_MS : 0), mi->rate);
    mi->readAll = mi->frames;

    tags[t].ti_Tag = GSA_Engine; tags[t++].ti_Data = mi->engine;
    if (mi->reader) {
        tags[t].ti_Tag = GSA_Output; tags[t++].ti_Data = GSOUT_CALLER;
        tags[t].ti_Tag = GSA_Rate; tags[t++].ti_Data = mi->mixRate;
    }
    tags[t].ti_Tag = TAG_DONE;
    if (!(mi->synth = GSOpenSynthA(tags))) {
        SetIoErr(ERROR_OBJECT_NOT_FOUND);       /* no port and no SoundFont, or no AHI */
        return FALSE;
    }
    if (mi->reader) {                           /* the file runs as the caller reads */
        struct TagItem none[1];
        none[0].ti_Tag = TAG_DONE;
        if (!GSPlayMemA(mi->synth, mi->midi, mi->midiLen, none)) {
            LONG err = (LONG)GSGetAttr(mi->synth, GSA_Error);
            SetIoErr(err ? err : DTERROR_INVALID_DATA);
            return FALSE;
        }
    }
    else {
        Forbid();                               /* it reads tc_UserData first */
        mi->control = CreateNewProcTags(NP_Entry, (ULONG)controlMain, NP_Name, (ULONG)"gfopenmidi control",
                                        NP_StackSize, 16384, NP_Priority, 1,
                                        NP_Input, 0, NP_Output, 0, NP_CloseInput, FALSE, NP_CloseOutput, FALSE,
                                        TAG_DONE);
        if (mi->control)
            mi->control->pr_Task.tc_UserData = mi;
        Permit();
        if (!mi->control) {
            SetIoErr(ERROR_NO_FREE_STORE);
            return FALSE;
        }
    }
    strcpy(mi->decodedBy, mi->engine == GSE_MT32 ? "GFOpenSynth, as an MT-32" : "GFOpenSynth, General MIDI");

    SetDTAttrs(o, NULL, NULL,
        DTA_ObjName, (ULONG)(mi->title[0] ? mi->title : (name ? (char *)FilePart(name) : "MIDI file")),
        DTA_ObjAnnotation, (ULONG)mi->type,
        TAG_DONE);
    return TRUE;
}

static void disposeMidi(struct MidiInst *mi)
{
    if (mi->control) {              /* quitMsg comes when it has stopped the file and gone */
        if ((mi->quitPort = CreateMsgPort())) {
            Signal(&mi->control->pr_Task, SIGBREAKF_CTRL_C);
            WaitPort(mi->quitPort);
            GetMsg(mi->quitPort);
            DeleteMsgPort(mi->quitPort);
            mi->quitPort = NULL;
            mi->control = NULL;
        }
    }
    if (mi->synth && !mi->control) {
        GSCloseSynth(mi->synth);
        mi->synth = NULL;
    }
    if (mi->data) {
        FreeVec(mi->data);
        mi->data = NULL;
    }
}

static void takeAttrs(struct MidiInst *mi, struct TagItem *tags, BOOL isNew)
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
        if (!isNew && mi->control)
            Signal(&mi->control->pr_Task, SIGBREAKF_CTRL_E);
    }
}

ULONG dt_dispatch(Class *cl, Object *o, Msg msg)
{
    struct MidiInst *mi;

    switch (msg->MethodID) {
    case OM_NEW: {
        Object *obj = (Object *)DoSuperMethodA(cl, o, msg);
        if (obj) {
            struct TagItem *at = ((struct opSet *)msg)->ops_AttrList;
            mi = INST_DATA(cl, obj);
            memset(mi, 0, sizeof *mi);
            mi->volume = 64;
            takeAttrs(mi, at, TRUE);
            mi->reader = GetTagData(OIA_Reader, FALSE, at) ? 1 : 0;
            mi->mixRate = GetTagData(OIA_MixRate, 44100, at);
            if (mi->mixRate < 8000 || mi->mixRate > 48000)      /* what the synth mixes at */
                mi->mixRate = 44100;
            if (!loadMidi(cl, obj)) {
                LONG err = IoErr();
                CoerceMethod(cl, obj, OM_DISPOSE);
                SetIoErr(err);
                obj = NULL;
            }
        }
        return (ULONG)obj;
    }
    case OM_DISPOSE:
        disposeMidi(INST_DATA(cl, o));
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
        case SDTA_Volume:
            *g->opg_Storage = mi->volume;
            return TRUE;
        case DTA_Repeat:
            *g->opg_Storage = mi->repeat;
            return TRUE;
        case SDTA_ReplayPeriod:
            /* As sound.datatype answers it: the address of a timeval the
             * object keeps. Looping, both fields are all ones. */
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
        case OIA_Channels:
            if (!mi->reader)
                break;
            *g->opg_Storage = 2;
            return TRUE;
        }
        return DoSuperMethodA(cl, o, msg);
    }
    case DTM_TRIGGER:
        if (((struct MidiInst *)INST_DATA(cl, o))->reader)
            return FALSE;                      /* a reader plays nothing */
        trigger(INST_DATA(cl, o), ((struct dtTrigger *)msg)->dtt_Function);
        return TRUE;
    case OIM_READSOUND:
        return readSound(INST_DATA(cl, o), (struct oiReadSound *)msg);
    case GM_GOACTIVE:
        /* A click plays, as it does a sound. */
        mi = INST_DATA(cl, o);
        if (!mi->reader && mi->state != MS_PLAYING)
            want(mi, PC_PLAY);
        return GMR_NOREUSE;
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
        /* Saved as it came: there is no 8SVX of a MIDI file to write. */
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
