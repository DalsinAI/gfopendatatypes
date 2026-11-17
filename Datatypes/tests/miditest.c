/* gfopenmidi.datatype's portable part (openmidi/midifile.c) on a PC.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include <string.h>
#include "midifile.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static unsigned char f[256];
static unsigned long put(unsigned long at, const char *t, unsigned long n, int le)
{
    memcpy(f + at, t, 4);
    if (le) { f[at + 4] = (unsigned char)n; f[at + 5] = (unsigned char)(n >> 8); f[at + 6] = f[at + 7] = 0; }
    else { f[at + 4] = f[at + 5] = 0; f[at + 6] = (unsigned char)(n >> 8); f[at + 7] = (unsigned char)n; }
    return at + 8;
}
/* a file of `tracks` tracks of 4 bytes each, the header saying `says` */
static unsigned long smf(unsigned long at, unsigned format, unsigned says, unsigned tracks)
{
    unsigned i;
    at = put(at, "MThd", 6, 0);
    f[at] = 0; f[at + 1] = (unsigned char)format; f[at + 2] = 0; f[at + 3] = (unsigned char)says; f[at + 4] = 1; f[at + 5] = 0xe0;
    at += 6;
    for (i = 0; i < tracks; i++) {
        at = put(at, "MTrk", 4, 0);
        f[at] = 0; f[at + 1] = 0xff; f[at + 2] = 0x2f; f[at + 3] = 0;
        at += 4;
    }
    return at;
}

int main(void)
{
    struct midi_head h;
    char kind[48];
    unsigned long n;

    n = smf(0, 1, 2, 2);
    CHECK(midi_file_head(f, n, &h) && h.format == 1 && h.tracks == 2 && h.found == 2 && h.division == 0x1e0 && h.start == 0);
    midi_file_kind(&h, kind);
    CHECK(!strcmp(kind, "MIDI file, format 1, 2 tracks"));
    n = smf(0, 0, 1, 1);
    CHECK(midi_file_head(f, n, &h) && h.found == 1);
    midi_file_kind(&h, kind);
    CHECK(!strcmp(kind, "MIDI file, format 0, 1 track"));
    /* not MIDI, too short, a format there is none of, no tracks, no division */
    CHECK(!midi_file_head((const unsigned char *)"FORM\0\0\0\x10" "8SVXVHDR", 16, &h));
    CHECK(!midi_file_head(f, 13, &h) && !midi_file_head(f, 0, &h) && !midi_file_head(NULL, 99, &h));
    n = smf(0, 3, 1, 1);
    CHECK(!midi_file_head(f, n, &h));
    n = smf(0, 1, 0, 1);
    CHECK(!midi_file_head(f, n, &h));
    n = smf(0, 1, 1, 0);
    CHECK(!midi_file_head(f, n, &h));
    n = smf(0, 1, 1, 1); f[12] = f[13] = 0;
    CHECK(!midi_file_head(f, n, &h));
    /* a header length past the file, and a longer header than 6 */
    n = smf(0, 1, 1, 1); f[7] = 200;
    CHECK(!midi_file_head(f, n, &h));
    /* the last track cut short: the whole ones still count */
    n = smf(0, 1, 3, 3);
    CHECK(midi_file_head(f, n - 2, &h) && h.tracks == 3 && h.found == 2);
    CHECK(!midi_file_head(f, 14 + 6, &h));           /* no whole track at all */
    /* a chunk that is not a track is stepped over */
    n = smf(0, 1, 1, 0);
    n = put(n, "XFIH", 2, 0); f[n] = f[n + 1] = 0; n += 2;
    n = put(n, "MTrk", 4, 0); f[n] = 0; f[n + 1] = 0xff; f[n + 2] = 0x2f; f[n + 3] = 0; n += 4;
    CHECK(midi_file_head(f, n, &h) && h.found == 1);
    /* .rmi: RIFF RMID, the MIDI file in its data chunk */
    memcpy(f, "RIFF", 4); memcpy(f + 8, "RMID", 4);
    n = put(12, "DISP", 4, 1); n += 4;
    {
        unsigned long d = put(n, "data", 0, 1), e = smf(d, 1, 1, 1);
        f[n + 4] = (unsigned char)(e - d);
        CHECK(midi_file_head(f, e, &h) && h.start == d && h.found == 1);
        f[n + 4] = 250;                              /* a data chunk longer than the file: to the file's end */
        CHECK(midi_file_head(f, e, &h) && h.found == 1);
        memcpy(f + n, "list", 4);                    /* no data chunk */
        f[n + 4] = (unsigned char)(e - d);
        CHECK(!midi_file_head(f, e, &h));
    }
    /* lengths */
    CHECK(midi_frames(0, 22050) == 0 && midi_frames(1000, 22050) == 22050 && midi_frames(1500, 44100) == 66150);
    CHECK(midi_frames(34500, 22050) == 760725);
    CHECK(midi_frames(0xFFFFFFFFUL, 48000) == 0xFFFFFFFFUL);
    CHECK(midi_frames(3600000UL, 48000) == 172800000UL);
    printf("%s: %d gfopenmidi cases, %d failed\n", fails ? "FAIL" : "PASS", checks, fails);
    return fails != 0;
}
