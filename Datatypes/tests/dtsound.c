/*
 * dtsound: open sounds through datatypes.library and say which datatype
 * took each, with its name, length and rate; optionally play it for some
 * seconds through DTM_TRIGGER (as GFOpenPlay does), pausing in the middle,
 * to hear or record it.
 *
 *   dtsound [PLAY=seconds] [VOL=volume] [WAIT=seconds] [LOAD=1] [MARK=file] FILE...
 *
 * With PLAY, each sound plays for that many seconds: STM_PLAY, a pause of
 * a second (STM_PAUSE) half way, STM_PLAY again, then STM_STOP. VOL sets
 * SDTA_Volume (0 to 64) as it plays again after the pause. WAIT instead
 * plays it once (STM_PLAY) and waits that long for the end of the sound
 * (SDTA_SignalTask), saying whether it came. With MARK, lines "PLAY
 * <file>" and so on are added to that file as each starts, so a PC
 * recording the machine's sound knows when. With LOAD=1 the waiting is
 * done by counting, at a priority below everything, and the count against
 * one taken with nothing playing says how much of the main CPU playing
 * took. openamigaimage's own attributes (OIA_DecodedBy, OIA_Stats) are
 * printed when the datatype has them. Each sound's SDTA_ReplayPeriod is
 * printed too, asked the way sound.datatype answers it: a pointer to the
 * object's own timeval.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/soundclass.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/datatypes.h>
#include <clib/alib_protos.h>
#include <datatypes/gfopenimage.h>

struct Library *DataTypesBase;

static void mark(const char *file, const char *what, const char *name)
{
    BPTR f;
    if (!file)
        return;
    if (!(f = Open((CONST_STRPTR)file, MODE_READWRITE)))
        return;
    Seek(f, 0, OFFSET_END);
    FPrintf(f, (CONST_STRPTR)"%s %s\n", (ULONG)what, (ULONG)name);
    Close(f);
}

static int loadMode;
static unsigned long baseRate;        /* counts a tick with nothing playing */
static unsigned long spunCount, spunTicks;

/* Waits ticks (1/50 s), counting at the lowest priority when LOAD=1. */
static void pause_ticks(LONG ticks)
{
    struct DateStamp d0, d1;
    volatile unsigned long n = 0;
    LONG el = 0;
    BYTE old;
    if (!loadMode) {
        Delay(ticks);
        return;
    }
    old = SetTaskPri(FindTask(NULL), -100);
    DateStamp(&d0);
    do {
        unsigned long k;
        for (k = 0; k < 4096; k++)
            n++;
        DateStamp(&d1);
        el = (d1.ds_Minute - d0.ds_Minute) * 3000 + (d1.ds_Tick - d0.ds_Tick);
    } while (el < ticks);
    SetTaskPri(FindTask(NULL), old);
    spunCount += n;
    spunTicks += el;
}

static void trig(Object *o, ULONG what)
{
    DoDTMethod(o, NULL, NULL, DTM_TRIGGER, NULL, what, NULL);
}

int main(int argc, char **argv)
{
    int i, play = 0, vol = -1, wait = 0;
    const char *markFile = NULL;

    if (!(DataTypesBase = OpenLibrary((CONST_STRPTR)"datatypes.library", 39)))
        return 20;
    for (i = 1; i < argc; i++) {
        struct DataType *dtn = NULL;
        STRPTR objName = NULL, annotation = NULL;
        ULONG len = 0, rate = 0, period = 0;
        BYTE *sample = (BYTE *)1;
        Object *o;

        if (!strncmp(argv[i], "PLAY=", 5)) {
            play = atoi(argv[i] + 5);
            continue;
        }
        if (!strncmp(argv[i], "VOL=", 4)) {
            vol = atoi(argv[i] + 4);
            continue;
        }
        if (!strncmp(argv[i], "WAIT=", 5)) {
            wait = atoi(argv[i] + 5);
            continue;
        }
        if (!strncmp(argv[i], "LOAD=", 5)) {
            loadMode = atoi(argv[i] + 5);
            if (loadMode && !baseRate) {
                spunCount = spunTicks = 0;
                pause_ticks(100);
                baseRate = spunTicks ? spunCount / spunTicks : 1;
                printf("idle: %lu counts a tick\n", baseRate);
                fflush(stdout);
            }
            continue;
        }
        if (!strncmp(argv[i], "MARK=", 5)) {
            markFile = argv[i] + 5;
            continue;
        }
        o = NewDTObject((APTR)argv[i], DTA_GroupID, GID_SOUND, TAG_DONE);
        if (!o) {
            printf("%s: NEWDTOBJECT_FAIL ioerr=%ld\n", argv[i], (long)IoErr());
            fflush(stdout);
            continue;
        }
        GetDTAttrs(o, DTA_DataType, (ULONG)&dtn, DTA_ObjName, (ULONG)&objName,
                   DTA_ObjAnnotation, (ULONG)&annotation, SDTA_SampleLength, (ULONG)&len,
                   SDTA_Period, (ULONG)&period, SDTA_Sample, (ULONG)&sample, TAG_DONE);
        GetDTAttrs(o, SDTA_SamplesPerSec, (ULONG)&rate, TAG_DONE);
        {
            struct timeval *replay = NULL;
            GetDTAttrs(o, SDTA_ReplayPeriod, (ULONG)&replay, TAG_DONE);
            if (!replay)
                printf("%s: replay period not given\n", argv[i]);
            else if (replay->tv_secs == 0xFFFFFFFFUL)
                printf("%s: replay period continuous\n", argv[i]);
            else
                printf("%s: replay period %lu.%06lu s\n", argv[i], (unsigned long)replay->tv_secs, (unsigned long)replay->tv_micro);
        }
        printf("%s: datatype=%s descriptor=\"%s\" name=\"%s\" kind=\"%s\" frames=%lu rate=%lu period=%lu sample=%s seconds=%lu\n",
               argv[i], dtn ? (char *)dtn->dtn_Header->dth_BaseName : "?",
               dtn ? (char *)dtn->dtn_Header->dth_Name : "?",
               objName ? (char *)objName : "", annotation ? (char *)annotation : "",
               (unsigned long)len, (unsigned long)rate, (unsigned long)period,
               sample ? "set" : "NULL", rate ? (unsigned long)(len / rate) : 0UL);
        fflush(stdout);
        if (play > 0) {
            STRPTR by = NULL, stats = NULL;
            mark(markFile, "PLAY", argv[i]);
            spunCount = spunTicks = 0;
            trig(o, STM_PLAY);
            pause_ticks(50 * (play / 2));
            trig(o, STM_PAUSE);
            mark(markFile, "PAUSE", argv[i]);
            Delay(50);
            if (vol >= 0)
                SetDTAttrs(o, NULL, NULL, SDTA_Volume, (ULONG)vol, TAG_DONE);
            trig(o, STM_PLAY);
            mark(markFile, "RESUME", argv[i]);
            pause_ticks(50 * (play - play / 2));
            if (GetDTAttrs(o, OIA_DecodedBy, (ULONG)&by, TAG_DONE) && by) {
                GetDTAttrs(o, OIA_Stats, (ULONG)&stats, TAG_DONE);
                printf("%s: played by %s: %s\n", argv[i], (char *)by, stats ? (char *)stats : "");
            }
            if (loadMode && baseRate && spunTicks)
                printf("%s: main CPU taken while playing: %lu%%\n", argv[i],
                       spunCount / spunTicks >= baseRate ? 0UL : 100UL - spunCount / spunTicks * 100 / baseRate);
            fflush(stdout);
            trig(o, STM_STOP);
            mark(markFile, "STOP", argv[i]);
        }
        if (wait > 0) {
            BYTE bit = AllocSignal(-1);
            ULONG got = 0;
            int t;
            if (bit >= 0) {
                SetDTAttrs(o, NULL, NULL, SDTA_SignalTask, (ULONG)FindTask(NULL),
                           SDTA_SignalBit, 1UL << bit, TAG_DONE);
                SetSignal(0, 1UL << bit);
                mark(markFile, "PLAY", argv[i]);
                trig(o, STM_PLAY);
                for (t = 0; t < wait * 10 && !got; t++) {
                    Delay(5);
                    got = SetSignal(0, 0) & (1UL << bit);
                }
                mark(markFile, got ? "ENDED" : "NO-END", argv[i]);
                printf("%s: end signal %s after %d.%d s\n", argv[i], got ? "came" : "did not come", t / 10, t % 10);
                fflush(stdout);
                trig(o, STM_STOP);
                FreeSignal(bit);
            }
        }
        DisposeDTObject(o);
    }
    CloseLibrary(DataTypesBase);
    return 0;
}
