/* gfopenmidi.datatype's portable part (midifile.h).
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "midifile.h"

static unsigned long be32(const unsigned char *p) { return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3]; }
static unsigned long le32(const unsigned char *p) { return (unsigned long)p[3] << 24 | (unsigned long)p[2] << 16 | (unsigned long)p[1] << 8 | p[0]; }
static int tag(const unsigned char *p, const char *t) { return p[0] == (unsigned char)t[0] && p[1] == (unsigned char)t[1] && p[2] == (unsigned char)t[2] && p[3] == (unsigned char)t[3]; }

int midi_file_head(const unsigned char *data, unsigned long len, struct midi_head *h)
{
    unsigned long at = 0, hl, end = len;
    if (!data || !h) return 0;
    h->format = h->tracks = h->found = h->division = 0;
    h->start = 0;
    if (len >= 20 && tag(data, "RIFF") && tag(data + 8, "RMID")) {   /* .rmi: the file is its "data" chunk */
        unsigned long p = 12;
        at = 0;
        while (p + 8 <= len) {
            unsigned long n = le32(data + p + 4);
            if (tag(data + p, "data")) { at = p + 8; end = n <= len - at ? at + n : len; break; }
            if (n > len - p - 8) return 0;
            p += 8 + n + (n & 1);
        }
        if (!at) return 0;
    }
    if (end - at < 14 || !tag(data + at, "MThd")) return 0;
    hl = be32(data + at + 4);
    if (hl < 6 || hl > end - at - 8) return 0;
    h->start = at;
    h->format = (unsigned)data[at + 8] << 8 | data[at + 9];
    h->tracks = (unsigned)data[at + 10] << 8 | data[at + 11];
    h->division = (unsigned)data[at + 12] << 8 | data[at + 13];
    if (h->format > 2 || !h->tracks || !h->division) return 0;
    at += 8 + hl;
    while (end - at >= 8) {
        unsigned long n = be32(data + at + 4);
        if (n > end - at - 8) break;                 /* cut short: what is whole still plays */
        if (tag(data + at, "MTrk")) h->found++;
        at += 8 + n;
    }
    return h->found > 0;
}

static char *num(char *b, unsigned long v)
{
    char t[12];
    int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *b++ = t[--n];
    return b;
}

void midi_file_kind(const struct midi_head *h, char *buf)
{
    const char *s = "MIDI file, format ";
    while (*s) *buf++ = *s++;
    buf = num(buf, h->format);
    *buf++ = ','; *buf++ = ' ';
    buf = num(buf, h->found);
    s = h->found == 1 ? " track" : " tracks";
    while (*s) *buf++ = *s++;
    *buf = 0;
}

unsigned long midi_frames(unsigned long ms, unsigned long rate)
{
    unsigned long s = ms / 1000, r = ms % 1000;
    if (rate && s > (0xFFFFFFFFUL - rate) / rate) return 0xFFFFFFFFUL;
    return s * rate + r * rate / 1000;
}
