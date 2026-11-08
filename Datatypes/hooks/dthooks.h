/*
 * Recognition code for DEVS:DataTypes descriptors (DTCD): what each hook
 * decides from the first bytes of the data, in plain C with no Amiga
 * calls, so the same code runs in the hook (hooks/<name>.c, built by
 * hooks/build.sh) and in the recognition test on the host
 * (tests/recognition/). datatypes.library runs a hook only once the
 * descriptor's mask and pattern fit; FALSE sends the data on down the list.
 *
 * Each returns 1 for "this descriptor's", 0 for not. b is the start of the
 * data, n how much of it datatypes.library read (at most 1084 bytes).
 * Big- and little-endian hosts alike: bytes only.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef DTHOOKS_H
#define DTHOOKS_H

/* ---- Matroska (not WebM) ------------------------------------------------------ */

/* An EBML variable-length number at b (n bytes there): its length (1 to 8),
 * 0 when it doesn't fit or is longer than 8. With keep the length marker
 * stays in the value (element IDs), else it goes (sizes); *unknown is set
 * for a size of all ones. */
static __inline__ unsigned dth_vint(const unsigned char *b, unsigned long n, int keep, unsigned long *v, int *unknown)
{
    unsigned len = 1, i, ones;
    unsigned char m = 0x80;
    unsigned long x;
    if (!n) return 0;
    while (len <= 8 && !(b[0] & m)) { len++; m >>= 1; }
    if (len > 8 || len > n) return 0;
    x = keep ? b[0] : (unsigned long)(b[0] & (m - 1));
    ones = (b[0] & (m - 1)) == (unsigned char)(m - 1);
    for (i = 1; i < len; i++) {
        /* a size past 32 bits: as unknown (no header is that long) */
        if (x > 0xffffffUL) { x = 0xffffffffUL; ones = ones && b[i] == 0xff; continue; }
        x = (x << 8) | b[i];
        ones = ones && b[i] == 0xff;
    }
    *v = x;
    if (unknown) *unknown = !keep && ones;
    return len;
}

/* The EBML header's DocType is "matroska" (WebM's is "webm"; webm.datatype
 * takes those, and Matroska too when gfopenvideo isn't installed). */
static __inline__ int dth_matroska(const unsigned char *b, unsigned long n)
{
    static const char want[] = "matroska";
    unsigned long id, size, end, at, i;
    int unknown;
    unsigned k;

    if (n < 4 || b[0] != 0x1a || b[1] != 0x45 || b[2] != 0xdf || b[3] != 0xa3) return 0;
    at = 4;
    if (!(k = dth_vint(b + at, n - at, 0, &size, &unknown)) || unknown) return 0;
    at += k;
    end = size > n - at ? n : at + size;   /* what of the header was read */
    while (at < end) {
        if (!(k = dth_vint(b + at, end - at, 1, &id, 0))) return 0;
        at += k;
        if (!(k = dth_vint(b + at, end - at, 0, &size, &unknown)) || unknown) return 0;
        at += k;
        if (id == 0x4282) {
            if (size != sizeof want - 1 || size > end - at) return 0;
            for (i = 0; i < size; i++)
                if (b[at + i] != (unsigned char)want[i]) return 0;
            return 1;
        }
        if (size > end - at) return 0;
        at += size;
    }
    return 0;
}

/* ---- MPEG audio (MP3 and MP2) without an ID3v2 tag ------------------------------ */

/* A Layer II or III frame header at b (four bytes): the frame's length in
 * bytes, 0 for no header. Layer I is left out: FF FF and FF FE (filler, a
 * UTF-16 mark) would read as one. */
static __inline__ unsigned long dth_mpa_frame(const unsigned char *b)
{
    static const unsigned short br1l3[16] = { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0 };
    static const unsigned short br1l2[16] = { 0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0 };
    static const unsigned short br2[16] = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0 };
    static const unsigned short sr1[4] = { 44100, 48000, 32000, 0 };
    unsigned ver, layer, bri, sri;
    unsigned long kbps, rate;

    if (b[0] != 0xff || (b[1] & 0xe0) != 0xe0) return 0;
    ver = (b[1] >> 3) & 3;                  /* 0 MPEG 2.5, 1 reserved, 2 MPEG 2, 3 MPEG 1 */
    layer = (b[1] >> 1) & 3;                /* 1 III, 2 II, 3 I, 0 reserved */
    bri = b[2] >> 4;
    sri = (b[2] >> 2) & 3;
    if (ver == 1 || layer == 0 || layer == 3 || bri == 0 || bri == 15 || sri == 3 || (b[3] & 3) == 2)
        return 0;
    kbps = ver == 3 ? (layer == 1 ? br1l3[bri] : br1l2[bri]) : br2[bri];
    rate = sr1[sri] >> (ver == 3 ? 0 : ver == 2 ? 1 : 2);
    /* samples per frame / 8: 144 (Layer II; Layer III at MPEG 1), 72 (Layer III at MPEG 2 and 2.5) */
    return (layer == 1 && ver != 3 ? 72000UL : 144000UL) * kbps / rate + ((b[2] >> 1) & 1);
}

/* A frame header at the start, and the next frame's header where it ends
 * (the same version, layer and sample rate) when that is in what was read;
 * a frame longer than that (a high bit rate at 32 kHz) on its header alone,
 * when a whole block was read (the file is longer than the frame). */
static __inline__ int dth_mpegaudio(const unsigned char *b, unsigned long n)
{
    unsigned long len;
    if (n < 4 || !(len = dth_mpa_frame(b))) return 0;
    if (len + 4 <= n)
        return dth_mpa_frame(b + len) && b[len + 1] == b[1] && ((b[len + 2] ^ b[2]) & 0x0c) == 0;
    return n >= 1024;
}

/* ---- Ogg Theora --------------------------------------------------------------- */

/* One of the first (BOS) pages of an Ogg file starts a Theora stream: its
 * first packet is the identification header, 0x80 "theora". The BOS pages
 * all come first (Ogg Skeleton's, the sound's, the film's, in any order). */
static __inline__ int dth_oggtheora(const unsigned char *b, unsigned long n)
{
    static const unsigned char id[7] = { 0x80, 't', 'h', 'e', 'o', 'r', 'a' };
    unsigned long at = 0, body, i;
    unsigned segs;

    while (at + 27 <= n) {
        if (b[at] != 'O' || b[at + 1] != 'g' || b[at + 2] != 'g' || b[at + 3] != 'S' || b[at + 4] != 0)
            return 0;
        if (!(b[at + 5] & 2)) return 0;     /* past the BOS pages */
        segs = b[at + 26];
        if (at + 27 + segs > n) return 0;
        body = at + 27 + segs;
        if (body + sizeof id <= n) {
            for (i = 0; i < sizeof id && b[body + i] == id[i]; i++)
                ;
            if (i == sizeof id) return 1;
        }
        for (i = 0; i < segs; i++)
            body += b[at + 27 + i];
        at = body;
    }
    return 0;
}

/* ---- documents the service lays out (Markdown, CSV) --------------------------- */

/* The service can be there: gfopenservice.device is installed, and a
 * services card is in the machine or a Cradle has been paired. Without
 * them gfopendoc.datatype can't open the file, and the ascii datatype
 * shows it as text, as before there was a descriptor for it. */
static __inline__ int dth_docservice(int have_device, int have_card, int paired)
{
    return have_device && (have_card || paired);
}

#endif
