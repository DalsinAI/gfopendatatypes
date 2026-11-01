/* GFOpenSound: sound.datatype's class (V44 attributes and up) as a core with
 * no Amiga headers, over struct odt_cenv and struct dtc_ext.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * Clean-room, from the sound_dtc autodoc, datatypes/soundclass.h's
 * published layouts (NDK 3.2), the audio.device autodoc and OS 3.2's
 * answers measured by tests/SoundDTClassABITest; never from Kickstart,
 * Hyperion or AROS code.
 */
#ifndef OPENDATATYPES_SOUND_CORE_H
#define OPENDATATYPES_SOUND_CORE_H

#include "../picture/dtc_env.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- datatypes/soundclass.h ----------------------------------------------- */

#define SND_SDTA_Dummy            (ODT_DTA_Dummy + 500)
#define SND_SDTA_VoiceHeader      (SND_SDTA_Dummy + 1)
#define SND_SDTA_Sample           (SND_SDTA_Dummy + 2)
#define SND_SDTA_SampleLength     (SND_SDTA_Dummy + 3)
#define SND_SDTA_Period           (SND_SDTA_Dummy + 4)
#define SND_SDTA_Volume           (SND_SDTA_Dummy + 5)
#define SND_SDTA_Cycles           (SND_SDTA_Dummy + 6)
#define SND_SDTA_SignalTask       (SND_SDTA_Dummy + 7)
#define SND_SDTA_SignalBit        (SND_SDTA_Dummy + 8)  /* V44: SDTA_SignalBitMask, a mask */
#define SND_SDTA_Continuous       (SND_SDTA_Dummy + 9)
#define SND_SDTA_SignalBitNumber  (SND_SDTA_Dummy + 10)
#define SND_SDTA_SamplesPerSec    (SND_SDTA_Dummy + 11)
#define SND_SDTA_ReplayPeriod     (SND_SDTA_Dummy + 12)
#define SND_SDTA_LeftSample       (SND_SDTA_Dummy + 13)
#define SND_SDTA_RightSample      (SND_SDTA_Dummy + 14)
#define SND_SDTA_Pan              (SND_SDTA_Dummy + 15)
#define SND_SDTA_FreeSampleData   (SND_SDTA_Dummy + 16)
#define SND_SDTA_SyncSampleChange (SND_SDTA_Dummy + 17)
/* The sample-type extra: ABI test whether OS 3.2's sound.datatype knows it */
#define SND_SDTA_SampleType       (SND_SDTA_Dummy + 30)
#define SND_SDTST_M8S  0                /* mono, 8 bits signed */
#define SND_SDTST_S8S  1                /* stereo interleaved, 8 bits */
#define SND_SDTST_M16S 2                /* mono, 16 bits big-endian */
#define SND_SDTST_S16S 3                /* stereo interleaved, 16 bits */

#define SND_CMP_NONE   0
#define SND_CMP_FIBDELTA 1
#define SND_UNITY      0x10000UL        /* Fixed 1.0: VHDR volume and SDTA_Pan's right */

struct snd_VoiceHeader {
    odt_u32 vh_OneShotHiSamples, vh_RepeatHiSamples, vh_SamplesPerHiCycle;
    odt_u16 vh_SamplesPerSec;
    odt_u8 vh_Octaves, vh_Compression;
    odt_u32 vh_Volume;
};
ODT_STATIC_ASSERT(sizeof(struct snd_VoiceHeader) == 20, "VoiceHeader");

struct snd_timeval { odt_u32 tv_secs, tv_micro; };

#define SND_PANEL_HEIGHT 14             /* ABI test: OS 3.2's control panel height */
#define SND_PERIOD_MIN   124            /* the DMA's shortest period */

struct snd_inst {
    struct snd_VoiceHeader vh;
    odt_s8 *left, *right;               /* SDTA_Sample is left */
    odt_u32 length;                     /* samples a channel */
    odt_u32 period, volume, cycles;
    odt_u32 samples_per_sec;
    odt_u32 pan;
    odt_u32 sample_type;
    void *signal_task;
    odt_u32 signal_mask;
    odt_u16 flags;                      /* SND_F_ */
    odt_u16 active;                     /* the panel button held: 0, 1 play, 2 stop */
    odt_s8 *chip[2];                    /* what plays, 8 bits in chip memory */
    void *player;                       /* the shell's player, while one plays */
    struct snd_timeval replay;
    struct dtc_audio_plan plan;
};
#define SND_F_FREE       0x0001
#define SND_F_CONTINUOUS 0x0002
#define SND_F_SYNC       0x0004
#define SND_F_PLAYING    0x0008
#define SND_F_PANSET     0x0010
#define SND_F_PLAYED     0x0020         /* DTA_Immediate's play happened */

odt_u32 snd_inst_size(void);
odt_u32 snd_dispatch(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, void *msg);

/* The sample i of channel ch (0 left, 1 right) as 8 bits */
odt_s8 snd_sample8(const struct snd_inst *p, odt_u32 ch, odt_u32 i);
/* Makes p->plan for what is set now; 0 when there is nothing to play */
int snd_make_plan(const struct odt_cenv *env, struct snd_inst *p, odt_u32 cycles);
/* 8SVX of the sound to a file handle: 1 written, 0 not (IoErr set) */
int snd_write_8svx(const struct odt_cenv *env, struct snd_inst *p, odt_u32 fh);

#ifdef __cplusplus
}
#endif
#endif
