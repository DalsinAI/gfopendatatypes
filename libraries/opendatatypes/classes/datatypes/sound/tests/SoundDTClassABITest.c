/* SoundDTClassABITest: OS 3.2's sound.datatype and GFOpenSound side by side.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * Makes sounds in memory (DTST_RAM objects of sound.datatype itself) and
 * asks each class the same: its defaults, what each SDTA_ attribute
 * answers after it is set (period and rate from each other, volume past
 * 64, the signal bit as a mask or a number, the sample-type extra),
 * SDTA_ReplayPeriod, DTA_TriggerMethods, DTM_TRIGGER's answers, and
 * DTM_WRITE (the 8SVX it writes, chunk by chunk). With PLAY it plays each
 * through audio.device and waits for the signal. The PROBE lines answer
 * what sound_core.cpp marks "ABI test".
 *   SoundDTClassABITest [PROBE] [LIB=dir] [DIR=dir] [PLAY]
 */
#define DTC_TEST_CLASS "sound.datatype"
#include "../../picture/tests/dtc_abitest.h"
#include <datatypes/soundclass.h>

#ifndef SDTA_SampleType
#define SDTA_SampleType (SDTA_Dummy + 30)
#endif
#ifndef SDTA_SignalBitNumber
#define SDTA_SignalBitNumber (SDTA_Dummy + 10)
#define SDTA_SamplesPerSec (SDTA_Dummy + 11)
#define SDTA_ReplayPeriod (SDTA_Dummy + 12)
#define SDTA_LeftSample (SDTA_Dummy + 13)
#define SDTA_RightSample (SDTA_Dummy + 14)
#define SDTA_Pan (SDTA_Dummy + 15)
#define SDTA_FreeSampleData (SDTA_Dummy + 16)
#endif

static int play;

static BYTE *sample(ULONG n)
{
    BYTE *s = AllocVec(n, MEMF_CHIP | MEMF_CLEAR);
    ULONG i;
    for (i = 0; s && i < n; i++) s[i] = (BYTE)((i * 37) & 0xff);
    return s;
}

static void dump_8svx(const char *tag, Object *o, ULONG mode)
{
    char p[256];
    BPTR fh;
    UBYTE buf[1024];
    LONG n, i;
    path_of(p, sizeof p, "odt_snd_write.8svx");
    if (!(fh = Open((CONST_STRPTR)p, MODE_NEWFILE))) return;
    SetIoErr(0);
    n = DoMethod(o, DTM_WRITE, NULL, fh, mode, NULL);
    probe("%s write mode %lu = %ld ioerr %ld", tag, mode, n, (long)IoErr());
    Close(fh);
    if (!(fh = Open((CONST_STRPTR)p, MODE_OLDFILE))) return;
    n = Read(fh, buf, sizeof buf);
    Close(fh);
    for (i = 12; i + 8 <= n;) {
        ULONG size = (buf[i + 4] << 24) | (buf[i + 5] << 16) | (buf[i + 6] << 8) | buf[i + 7], k;
        char line[80];
        int m = 0;
        for (k = 0; k < size && k < 24 && i + 8 + k < (ULONG)n; k++) m += sprintf(line + m, "%02x", buf[i + 8 + k]);
        line[m] = 0;
        probe("%s chunk %.4s %lu %s", tag, buf + i, size, line);
        i += 8 + size + (size & 1);
    }
}

static void run_set(const char *cls)
{
    Object *o = new_ram_obj(cls, NULL);
    struct timeval *tv;
    struct DTMethod *tm;
    BYTE sigbit;
    if (!o) { out("new fail %ld", (long)IoErr()); return; }
    probe("default volume %lu cycles %lu period %lu rate %lu", get1(o, SDTA_Volume), get1(o, SDTA_Cycles),
          get1(o, SDTA_Period), get1(o, SDTA_SamplesPerSec));
    probe("default pan %lx free %lu sampletype %lx continuous %lu", get1(o, SDTA_Pan),
          get1(o, SDTA_FreeSampleData), get1(o, SDTA_SampleType), get1(o, SDTA_Continuous));
    probe("trigger play empty %lu", DoMethod(o, DTM_TRIGGER, NULL, STM_PLAY, NULL));
    SetAttrs(o, SDTA_Sample, (ULONG)sample(1001), SDTA_SampleLength, 1001, SDTA_SamplesPerSec, 22050, TAG_DONE);
    probe("rate 22050: period %lu rate %lu", get1(o, SDTA_Period), get1(o, SDTA_SamplesPerSec));
    SetAttrs(o, SDTA_Period, 300, TAG_DONE);
    probe("period 300: period %lu rate %lu", get1(o, SDTA_Period), get1(o, SDTA_SamplesPerSec));
    SetAttrs(o, SDTA_Period, 100, TAG_DONE);
    probe("period 100: period %lu", get1(o, SDTA_Period));
    SetAttrs(o, SDTA_Volume, 99, TAG_DONE);
    probe("volume 99: %lu", get1(o, SDTA_Volume));
    SetAttrs(o, SDTA_SignalBitNumber, 12, TAG_DONE);
    probe("signalbitnumber 12: signalbit %lx", get1(o, SDTA_SignalBit));
    probe("sampletype set %lu", SetAttrs(o, SDTA_SampleType, 2, TAG_DONE));
    SetAttrs(o, SDTA_SampleType, 0, SDTA_SamplesPerSec, 8000, TAG_DONE);
    tv = (struct timeval *)get1(o, SDTA_ReplayPeriod);
    if (tv && tv != (struct timeval *)0xfeedfaceUL)
        probe("replay %lu %lu", tv->tv_secs, tv->tv_micro);
    tm = (struct DTMethod *)get1(o, DTA_TriggerMethods);
    while (tm && tm != (struct DTMethod *)0xfeedfaceUL && tm->dtm_Label) {
        probe("trigger method %s %s %lu", tm->dtm_Label, tm->dtm_Command, tm->dtm_Method);
        tm++;
    }
    dump_8svx("mono", o, DTWM_IFF);
    dump_8svx("mono", o, DTWM_RAW);
    if (play && (sigbit = AllocSignal(-1)) >= 0) {
        SetAttrs(o, SDTA_SignalTask, (ULONG)FindTask(NULL), SDTA_SignalBit, 1UL << sigbit, TAG_DONE);
        out("play %lu", DoMethod(o, DTM_TRIGGER, NULL, STM_PLAY, NULL));
        out("signalled %lu", (Wait((1UL << sigbit) | SIGBREAKF_CTRL_C) >> sigbit) & 1);
        FreeSignal(sigbit);
    }
    probe("trigger pause %lu resume %lu stop %lu", DoMethod(o, DTM_TRIGGER, NULL, STM_PAUSE, NULL),
          DoMethod(o, DTM_TRIGGER, NULL, STM_RESUME, NULL), DoMethod(o, DTM_TRIGGER, NULL, STM_STOP, NULL));
    DisposeObject(o);

    o = new_ram_obj(cls, NULL);
    if (o) {
        SetAttrs(o, SDTA_LeftSample, (ULONG)sample(400), SDTA_RightSample, (ULONG)sample(400), SDTA_SampleLength, 400,
                 SDTA_SamplesPerSec, 11025, SDTA_Volume, 32, TAG_DONE);
        probe("stereo sample %lu", get1(o, SDTA_Sample) == get1(o, SDTA_LeftSample));
        dump_8svx("stereo", o, DTWM_IFF);
        DisposeObject(o);
    }
}

int main(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "PLAY")) play = 1;
    return dtc_main(argc, argv);
}
