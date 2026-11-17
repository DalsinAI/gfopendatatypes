/* gfopenmidi.datatype's portable part: is this a Standard MIDI File, and
 * what kind. tests/miditest.c checks it on a PC.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef MIDIFILE_H
#define MIDIFILE_H

struct midi_head {
    unsigned format;        /* 0 one track, 1 tracks played together, 2 separate songs */
    unsigned tracks;        /* as the header says */
    unsigned found;         /* MTrk chunks really there, whole */
    unsigned division;      /* ticks a quarter note, or SMPTE (bit 15) */
    unsigned long start;    /* where MThd is: 0, or past a RIFF RMID wrapper */
};

/* 1 and *h filled when data is a MIDI file with at least one whole track;
 * a .rmi file (RIFF RMID) is looked into. */
int midi_file_head(const unsigned char *data, unsigned long len, struct midi_head *h);

/* "MIDI file, format 1, 5 tracks" into buf (at least 48 bytes) */
void midi_file_kind(const struct midi_head *h, char *buf);

/* frames of a sound ms milliseconds long at rate, without overflow */
unsigned long midi_frames(unsigned long ms, unsigned long rate);

#endif
