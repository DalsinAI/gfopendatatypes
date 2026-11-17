/* gfopensynth.library (GFOpenSynth): MT-32 and General MIDI music for
 * programs. A synth takes MIDI (short messages, SysEx, or a whole MIDI
 * file it plays itself) and sounds it: on AkikoPlus's MIDI port, where the
 * host plays it as a real synth plugged into that port would, or here on
 * the 68k from a SoundFont, to ahi.device or into the caller's own buffer.
 * docs/gfopensynth.md is the design. MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef LIBRARIES_GFOPENSYNTH_H
#define LIBRARIES_GFOPENSYNTH_H

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif
#ifndef UTILITY_TAGITEM_H
#include <utility/tagitem.h>
#endif

#define GFOPENSYNTHNAME    "gfopensynth.library"
#define GFOPENSYNTH_VERSION 1

/* A synth: made by GSOpenSynthA, ended by GSCloseSynth. Its fields are private. */
struct GSSynth;

#define GSA_Dummy     (TAG_USER + 0x0DA16100)

/* GSOpenSynthA's tags */
#define GSA_Output    (GSA_Dummy + 1)  /* (ULONG) GSOUT_ (default GSOUT_AUTO, or ENV:GFOpenSynth/Output) */
#define GSA_Engine    (GSA_Dummy + 2)  /* (ULONG) GSE_ (default GSE_GM) */
#define GSA_Rate      (GSA_Dummy + 3)  /* (ULONG) frames a second on the 68k (default 22050, 8000 to 48000) */
#define GSA_Voices    (GSA_Dummy + 4)  /* (ULONG) voices on the 68k, 1 to 64 (default 24, or ENV:GFOpenSynth/Voices) */
#define GSA_SoundFont (GSA_Dummy + 5)  /* (STRPTR) the SoundFont on the 68k (default ENV:GFOpenSynth/SoundFont, else DEVS:SoundFonts/default.sf2, else our own DEVS:SoundFonts/basic.sf2) */
#define GSA_Unit      (GSA_Dummy + 6)  /* (ULONG) ahi.device unit for GSOUT_AHI (default 0) */
#define GSA_Volume    (GSA_Dummy + 7)  /* (ULONG) also GSGetAttr: 0 to GS_VOLUME_FULL (default full) */
#define GSA_Loop      (GSA_Dummy + 8)  /* (BOOL) GSPlayFileA, GSPlayMemA: play it again at its end (default FALSE) */

/* GSGetAttr's */
#define GSA_Playing   (GSA_Dummy + 16) /* TRUE while a file plays */
#define GSA_Loops     (GSA_Dummy + 17) /* times the file has started again */
#define GSA_Active    (GSA_Dummy + 18) /* voices sounding on the 68k (0 on the port) */
#define GSA_Error     (GSA_Dummy + 19) /* 0, or why the last call failed (an AmigaDOS error) */
#define GSA_Length    (GSA_Dummy + 20) /* the file's length in milliseconds */
#define GSA_Position  (GSA_Dummy + 21) /* milliseconds of the file played (this time round, when looped) */
#define GSA_Beat      (GSA_Dummy + 22) /* the file's beat (quarter notes from its start) */
#define GSA_Paused    (GSA_Dummy + 23) /* TRUE while GSPauseFile holds the file */

/* GSA_Output */
#define GSOUT_AUTO   0                 /* the port, else the 68k to AHI */
#define GSOUT_PORT   1                 /* AkikoPlus's MIDI port only */
#define GSOUT_AHI    2                 /* the 68k engine to ahi.device */
#define GSOUT_CALLER 3                 /* the 68k engine; the caller takes the sound with GSRenderS16 */

/* GSA_Engine: what the synth plays as */
#define GSE_GM       0                 /* General MIDI (and GS bank select) */
#define GSE_MT32     1                 /* an MT-32 (on the 68k: through GM, channels 2-10) */

#define GS_VOLUME_FULL 256

/* GSShortMsg's message: status in bits 0-7, the first data byte in 8-15,
 * the second in 16-23 (as Windows' midiOutShortMsg packs it) */
#define GS_MSG(status, d1, d2) ((ULONG)(status) | ((ULONG)(d1) << 8) | ((ULONG)(d2) << 16))

#endif
