/*
 * gfopensound.datatype's decoders on this CPU: Ogg Vorbis (stb_vorbis),
 * MP3 (dr_mp3) and FLAC (dr_flac), from a file in memory, a piece at a
 * time, to 16-bit samples. Plain C with no Amiga headers
 * (tests/test-sound.sh runs it on x86 or ARM64 cores).
 *
 * MIT, Copyright (c) 2026 AmiCradle Team. stb_vorbis and dr_libs are
 * public domain (or MIT, MIT-0), fetched at build time (../../SOURCES).
 */
#ifndef SNDDEC_H
#define SNDDEC_H

typedef struct snddec snddec;

/* Which decoder takes these bytes, by their first bytes only: 1 Ogg (Vorbis),
 * 2 MP3, 3 FLAC, 0 none. In the header so a datatype built without the
 * decoders (SNDDEC=library) can tell these formats apart too. */
static inline int snddec_magic(const unsigned char *m, unsigned long n)
{
    if (n >= 4 && m[0] == 'O' && m[1] == 'g' && m[2] == 'g' && m[3] == 'S') return 1;
    if (n >= 4 && m[0] == 'f' && m[1] == 'L' && m[2] == 'a' && m[3] == 'C') return 3;
    if (n >= 3 && m[0] == 'I' && m[1] == 'D' && m[2] == '3') return 2;
    if (n >= 2 && m[0] == 0xff && (m[1] & 0xe0) == 0xe0 && (m[1] & 0x06)) return 2;
    return 0;
}

/* Which decoder takes these bytes: "Ogg Vorbis", "MP3", "FLAC", or NULL */
const char *snddec_kind(const unsigned char *data, unsigned long size);

/* NULL when none takes it, or it is broken. frames 0: not known. */
snddec *snddec_open(const unsigned char *data, unsigned long size,
                    int *channels, unsigned long *rate, unsigned long *frames);

/* Up to n frames, interleaved, at out; 0 at the end. */
unsigned long snddec_read(snddec *d, short *out, unsigned long n);

int snddec_rewind(snddec *d);
void snddec_close(snddec *d);

#endif
