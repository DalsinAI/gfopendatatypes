/* GFOpenDatatypes animation.datatype's MP4 writer (adt_mp4_write in
 * mp4_core.c, the H.264 encoder in mp4_h264.c), under qemu-m68k: films of
 * 1 to 8 planes, HAM6, HAM8 and Extra Half-Brite, odd sizes, per-frame
 * colours, frames of different lengths, mono and stereo sound, a frame's
 * own or the film's, are written as MP4. Here: each frame's 4:2:0 YUV
 * against this file's own reading of the planes (bit by bit, HAM by the
 * Hardware Reference Manual) and BT.601's sums; the file's boxes (ftyp,
 * mdat, moov, its tracks and tables against the frames and their sound);
 * and the class's DTM_WRITE (OIA_WriteFormat, OIA_Quality,
 * OIA_Compression, OIA_WriteSound, a format it doesn't know) on a mock
 * object. Each film goes to DIR for test_opendatatypes_mp4write.sh's
 * outside checks (FFmpeg decodes it; the encoder built on the host makes
 * the same H.264): NAME.mp4, NAME.rgb (frames as R, G, B), NAME.yuv (what
 * was encoded), NAME.pcm (the sound as it must decode), NAME.anim (the
 * same frames as an IFF ANIM, when FFmpeg can show them the same way) and
 * a line of films.txt.
 *
 *     mp4write_test DIR
 *     mp4write_test -speed MODE W H FRAMES KIND QUALITY LEVEL
 *
 * -speed makes one film of KIND (box, plasma, ham8, ...) at 25 frames a
 * second and, by MODE, stops there (gen), turns its frames to YUV (yuv) or
 * writes it (write): no checks and no files, for counting instructions
 * (test_opendatatypes_mp4write.sh with COUNT=1).
 *
 * Copyright (c) 2026 AmiCradle Team. MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "animation/animation_core.h"
#include "animation/mp4_core.h"

static int fails, checks;
static unsigned long seed = 2026;
static const char *dir;
static FILE *list;
static int speed_mode;                  /* -speed: 1 gen, 2 yuv, 3 write */

static unsigned rnd(void)
{
    seed = seed * 1103515245UL + 12345UL;
    return (unsigned)(seed >> 8) & 0xFFFFFF;
}

static void check(int ok, const char *what, const char *name)
{
    checks++;
    if (!ok) {
        fails++;
        printf("FAIL %s: %s\n", name, what);
    }
}

/* ---- films ---------------------------------------------------------------------- */

struct film {
    char name[96];
    odt_u32 w, h, depth, frames, bpr, fps, modeid, ncolors;
    odt_u8 *planes;                     /* frames x depth x h x bpr */
    struct adt_ColorRegister cols[256];
    odt_u8 *fcmap;                      /* frames x 768: each frame's own colours, or NULL */
    odt_u32 fncol;
    odt_u32 *dur;                       /* each frame's ticks, or NULL (1) */
    /* sound: a frame's own (fs), or the film's (whole) */
    odt_s8 **fl, **fr;                  /* frames' left and right (fr NULL: mono) */
    odt_u32 *flen, *frate;
    odt_s8 *wl, *wr;
    odt_u32 wlen, wrate;
    struct adt_BitMap bm;
    int out;                            /* frames handed out and not given back */
    odt_u32 loads;
};

enum { NOISE, BOX, STILL, PLASMA, HAM6, HAM8, EHB, KINDS };
static const char *kind_name[] = { "noise", "box", "still", "plasma", "ham6", "ham8", "ehb" };

static odt_u8 *plane_byte(struct film *m, odt_u32 f, odt_u32 p, odt_u32 y, odt_u32 x)
{
    return m->planes + ((f * m->depth + p) * m->h + y) * m->bpr + x;
}

static void set_pixel(struct film *m, odt_u32 f, odt_u32 x, odt_u32 y, odt_u32 c)
{
    odt_u32 p;
    for (p = 0; p < m->depth; p++) {
        odt_u8 *b = plane_byte(m, f, p, y, x >> 3), bit = (odt_u8)(0x80 >> (x & 7));
        if (c >> p & 1) *b |= bit;
        else *b &= (odt_u8)~bit;
    }
}

static void make(struct film *m, int kind)
{
    odt_u32 f, x, y, n = 1u << m->depth;
    for (x = 0; x < m->frames * m->depth * m->h * m->bpr; x++)
        m->planes[x] = (odt_u8)rnd();   /* the bits past the width too */
    for (f = 0; f < m->frames; f++)
        for (y = 0; y < m->h; y++)
            for (x = 0; x < m->w; x++) {
                odt_u32 c;
                switch (kind) {
                case NOISE: c = rnd() & (n - 1); break;
                case BOX: {             /* a box moving over a striped ground */
                    odt_u32 bx = f * 7, by = f * 3;
                    c = (y >> 2) & 1 ? 1 : 0;
                    if (y >= by && y < by + 20 && x >= bx && x < bx + 28) c = (f % (n - 1)) + 1;
                    if (n == 2 && c > 1) c = 1;
                    break;
                }
                case STILL: c = ((x >> 3) + (y >> 3)) & (n - 1); break;
                case PLASMA: case EHB: /* bands of colour sliding along */
                    c = ((x + 2 * y + 3 * f) >> 2) & (n - 1);
                    break;
                default: {              /* HAM: set a base, then modify blue, red, green in turn */
                    odt_u32 bits = kind == HAM6 ? 4 : 6, v = ((x + y + f * 5) >> 1) & ((1u << bits) - 1);
                    odt_u32 ctl = x % 4 == 0 ? 0 : x % 4;
                    if (ctl == 0) v = (y >> 3) & ((1u << bits) - 1);
                    c = ctl << bits | v;
                    if (!(rnd() & 63)) c = rnd() & ((1u << m->depth) - 1);
                    break;
                }
                }
                set_pixel(m, f, x, y, c);
            }
}

/* This file's own reading: pixel (x, y) of frame f as R, G, B */
static void ref_rgb(struct film *m, odt_u32 f, odt_u8 *out)
{
    const odt_u8 *pal;
    odt_u8 base[768];
    odt_u32 x, y, p, i, n, ham = 0;
    if (m->fcmap) {
        n = m->fncol;
        memset(base, 0, sizeof base);
        memcpy(base, m->fcmap + f * 768, n * 3);
    } else {
        n = m->ncolors;
        memset(base, 0, sizeof base);
        for (i = 0; i < n && i < 256; i++) {
            base[3 * i] = m->cols[i].red;
            base[3 * i + 1] = m->cols[i].green;
            base[3 * i + 2] = m->cols[i].blue;
        }
    }
    if ((m->modeid & 0x80) && !(m->modeid & 0x800) && m->depth == 6)
        for (i = 0; i < 32; i++) {     /* EHB: 32 to 63 at half brightness */
            base[3 * (32 + i)] = base[3 * i] / 2;
            base[3 * (32 + i) + 1] = base[3 * i + 1] / 2;
            base[3 * (32 + i) + 2] = base[3 * i + 2] / 2;
        }
    pal = base;
    if (m->modeid & 0x800) ham = m->depth <= 6 ? 4 : 6;
    for (y = 0; y < m->h; y++) {
        odt_u32 r = pal[0], g = pal[1], b = pal[2];
        for (x = 0; x < m->w; x++) {
            odt_u32 c = 0;
            for (p = 0; p < m->depth; p++)
                c |= (odt_u32)((*plane_byte(m, f, p, y, x >> 3) >> (7 - (x & 7))) & 1) << p;
            if (!ham) {
                r = pal[3 * c]; g = pal[3 * c + 1]; b = pal[3 * c + 2];
            } else {
                odt_u32 v = c & ((1u << ham) - 1), full = (v << (8 - ham)) | (v >> (2 * ham - 8));
                switch (c >> ham) {
                case 0: r = pal[3 * v]; g = pal[3 * v + 1]; b = pal[3 * v + 2]; break;
                case 1: b = full; break;
                case 2: r = full; break;
                default: g = full; break;
                }
            }
            out[(y * m->w + x) * 3] = (odt_u8)r;
            out[(y * m->w + x) * 3 + 1] = (odt_u8)g;
            out[(y * m->w + x) * 3 + 2] = (odt_u8)b;
        }
    }
}

/* BT.601 studio range from R, G, B (cw x ch, the last column and row
 * repeated when w or h is odd) */
static void ref_yuv(const odt_u8 *rgb, odt_u32 w, odt_u32 h, odt_u8 *yuv)
{
    odt_u32 cw = (w + 1) & ~1u, ch = (h + 1) & ~1u, x, y;
    odt_u8 *u = yuv + cw * ch, *v = u + (cw / 2) * (ch / 2);
#define PX(xx, yy) (rgb + (((yy) < h ? (yy) : h - 1) * w + ((xx) < w ? (xx) : w - 1)) * 3)
    for (y = 0; y < ch; y++)
        for (x = 0; x < cw; x++) {
            const odt_u8 *p = PX(x, y);
            yuv[y * cw + x] = (odt_u8)(16 + (66 * p[0] + 129 * p[1] + 25 * p[2] + 128) / 256);
        }
    for (y = 0; y < ch; y += 2)
        for (x = 0; x < cw; x += 2) {
            long r = 0, g = 0, b = 0;
            int i;
            for (i = 0; i < 4; i++) {
                const odt_u8 *p = PX(x + (i & 1), y + (i >> 1));
                r += p[0]; g += p[1]; b += p[2];
            }
            u[(y / 2) * (cw / 2) + x / 2] = (odt_u8)(128 + (-38 * r - 74 * g + 112 * b + 512 + 131072) / 1024 - 128);
            v[(y / 2) * (cw / 2) + x / 2] = (odt_u8)(128 + (112 * r - 94 * g - 18 * b + 512 + 131072) / 1024 - 128);
        }
#undef PX
}

/* ---- the writer's io ---------------------------------------------------------------- */

struct sink {
    odt_u8 *p;
    odt_u32 n, cap;
    struct film *film;
    odt_u32 allocs;                     /* blocks out */
    odt_u8 *yuv_seen;                   /* this test's YUV for each frame, checked as it goes */
};

static void *s_alloc(void *h, odt_u32 n) { ((struct sink *)h)->allocs++; return calloc(1, n ? n : 1); }
static void s_free(void *h, void *p) { if (p) ((struct sink *)h)->allocs--; free(p); }

static odt_s32 s_put(void *h, const odt_u8 *p, odt_u32 n)
{
    struct sink *s = h;
    if (s->n + n > s->cap) {
        s->cap = (s->n + n) * 2;
        s->p = realloc(s->p, s->cap);
    }
    memcpy(s->p + s->n, p, n);
    s->n += n;
    return 0;
}

static odt_s32 s_patch(void *h, odt_u32 at, odt_u32 v)
{
    struct sink *s = h;
    if (at + 4 > s->n) return 1;
    s->p[at] = (odt_u8)(v >> 24); s->p[at + 1] = (odt_u8)(v >> 16);
    s->p[at + 2] = (odt_u8)(v >> 8); s->p[at + 3] = (odt_u8)v;
    return 0;
}

static const struct adt_BitMap *s_frame(void *h, odt_u32 f)
{
    struct film *m = ((struct sink *)h)->film;
    odt_u32 p;
    m->bm.BytesPerRow = (odt_u16)m->bpr;
    m->bm.Rows = (odt_u16)m->h;
    m->bm.Depth = (odt_u8)m->depth;
    for (p = 0; p < 8; p++) m->bm.Planes[p] = p < m->depth ? plane_byte(m, f, p, 0, 0) : 0;
    m->out++;
    m->loads++;
    return &m->bm;
}

static void s_done(void *h, odt_u32 f) { (void)f; ((struct sink *)h)->film->out--; }

static void s_info(void *h, odt_u32 f, struct adt_mp4_frame *fi)
{
    struct film *m = ((struct sink *)h)->film;
    fi->duration = m->dur ? m->dur[f] : 1;
    if (m->fcmap) {
        fi->rgb = m->fcmap + f * 768;
        fi->ncolors = m->fncol;
    }
    if (m->fl && m->fl[f]) {
        fi->left = m->fl[f];
        fi->right = m->fr ? m->fr[f] : 0;
        fi->sound_len = m->flen[f];
        fi->rate = m->frate[f];
    }
}

/* ---- reading the file back ------------------------------------------------------- */

static odt_u32 be32(const odt_u8 *p) { return (odt_u32)p[0] << 24 | (odt_u32)p[1] << 16 | (odt_u32)p[2] << 8 | p[3]; }
static odt_u32 be16(const odt_u8 *p) { return (odt_u32)p[0] << 8 | p[1]; }

/* The box of type t inside [p, end), its contents; NULL if none */
static const odt_u8 *find(const odt_u8 *p, const odt_u8 *end, const char *t, odt_u32 *len)
{
    while (p + 8 <= end) {
        odt_u32 n = be32(p);
        if (n < 8 || p + n > end) return 0;
        if (!memcmp(p + 4, t, 4)) {
            *len = n - 8;
            return p + 8;
        }
        p += n;
    }
    return 0;
}

static const odt_u8 *path(const odt_u8 *p, odt_u32 n, const char *const *names, odt_u32 *len)
{
    const odt_u8 *end = p + n;
    for (; *names; names++) {
        if (!(p = find(p, end, *names, len))) return 0;
        end = p + *len;
    }
    return p;
}

/* track k (0 video, 1 sound) */
static const odt_u8 *trak(const odt_u8 *moov, odt_u32 n, int k, odt_u32 *len)
{
    const odt_u8 *p = moov, *end = moov + n;
    while ((p = find(p, end, "trak", len))) {
        if (!k--) return p;
        p += *len;
    }
    return 0;
}

static const char *const STBL[] = { "mdia", "minf", "stbl", 0 };

static void verify(struct sink *s, struct film *m, const odt_u8 *pcm, odt_u32 pcm_samples, odt_u32 channels,
                   odt_u32 rate, odt_u32 qp)
{
    const odt_u8 *f = s->p, *moov, *stbl, *b, *tk;
    odt_u32 n = s->n, len, mlen, tlen, slen, i, ticks = 0, sync = 0;
    const char *name = m->name;
    const char *const mdhd[] = { "mdia", "mdhd", 0 };
    (void)qp;
    check(n > 48 && !memcmp(f + 4, "ftyp", 4) && be32(f) == 32 && !memcmp(f + 8, "isom", 4), "ftyp", name);
    check(!memcmp(f + 36, "mdat", 4), "mdat second", name);
    if (n < 48) return;
    moov = find(f + 32 + be32(f + 32), f + n, "moov", &mlen);
    check(moov && moov + mlen == f + n, "moov last, the mdat's size right", name);
    if (!moov) return;
    b = find(moov, moov + mlen, "mvhd", &len);
    check(b && be32(b + 12) == 1000, "mvhd in milliseconds", name);
    for (i = 0; i < m->frames; i++) ticks += m->dur ? m->dur[i] : 1;
    tk = trak(moov, mlen, 0, &tlen);
    check(tk != 0, "a video track", name);
    if (!tk) return;
    b = path(tk, tlen, mdhd, &len);
    check(b && be32(b + 12) == m->fps * 1000 && be32(b + 16) == ticks * 1000, "video mdhd", name);
    b = find(tk, tk + tlen, "tkhd", &len);
    check(b && be32(b + 76) == m->w << 16 && be32(b + 80) == m->h << 16, "tkhd's size", name);
    stbl = path(tk, tlen, STBL, &slen);
    check(stbl != 0, "video stbl", name);
    if (!stbl) return;
    b = find(stbl, stbl + slen, "stsd", &len);
    check(b && be32(b + 4) == 1 && !memcmp(b + 12, "avc1", 4) && be16(b + 8 + 32) == ((m->w + 1) & ~1u) &&
          be16(b + 8 + 34) == ((m->h + 1) & ~1u), "avc1 and its size", name);
    if (b) {
        const odt_u8 *c = find(b + 8 + 86, b + len, "avcC", &len);
        check(c && c[0] == 1 && c[1] == 66 && (c[4] & 3) == 3 && (c[5] & 31) == 1, "avcC: baseline, 4-byte lengths",
              name);
    }
    {
        /* stsz, stco and stts against the mdat: every sample inside it, in order, its NALs' lengths adding up */
        const odt_u8 *sz = find(stbl, stbl + slen, "stsz", &len), *co = find(stbl, stbl + slen, "stco", &tlen);
        odt_u32 tslen;
        const odt_u8 *ts = find(stbl, stbl + slen, "stts", &tslen);
        const odt_u8 *ss;
        odt_u32 ok = sz && co && ts && be32(sz + 8) == m->frames && be32(co + 4) == m->frames, k, e, t;
        if (ok)
            for (i = 0; i < m->frames; i++) {
                odt_u32 at = be32(co + 8 + 4 * i), size = be32(sz + 12 + 4 * i), nal;
                if (at < 40 || at + size > (odt_u32)(moov - 8 - f)) { ok = 0; break; }
                for (nal = 0; nal < size; nal += 4 + be32(f + at + nal)) {
                    odt_u32 type = f[at + nal + 4] & 31;
                    if (type == 7 || type == 8) ok = 0;
                    if (type == 5) sync++;
                }
                if (nal != size) ok = 0;
            }
        check(ok, "every frame's NALs in the mdat, no SPS or PPS among them", name);
        ok = ts != 0;
        for (k = 0, i = 0, t = 0; ok && k < be32(ts + 4); k++) {
            for (e = 0; e < be32(ts + 8 + 8 * k); e++, i++)
                if (i >= m->frames || be32(ts + 12 + 8 * k) != 1000 * (m->dur ? m->dur[i] : 1)) ok = 0;
            t++;
        }
        check(ok && i == m->frames, "stts: each frame's duration", name);
        ss = find(stbl, stbl + slen, "stss", &len);
        check(sync >= 1 && (ss ? be32(ss + 4) == sync : sync == m->frames), "stss lists the key frames", name);
    }
    tk = trak(moov, mlen, 1, &tlen);
    check(!tk == !pcm_samples, pcm_samples ? "a sound track" : "no sound track", name);
    if (!tk || !pcm_samples) return;
    b = path(tk, tlen, mdhd, &len);
    check(b && be32(b + 12) == rate && be32(b + 16) == pcm_samples, "sound mdhd", name);
    stbl = path(tk, tlen, STBL, &slen);
    if (!stbl) {
        check(0, "sound stbl", name);
        return;
    }
    b = find(stbl, stbl + slen, "stsd", &len);
    check(b && !memcmp(b + 12, "twos", 4) && be16(b + 8 + 24) == channels && be16(b + 8 + 26) == 8 &&
          be32(b + 8 + 32) == rate << 16, "twos: channels, 8 bits, rate", name);
    {
        /* the chunks, in order, are the sound */
        const odt_u8 *sc = find(stbl, stbl + slen, "stsc", &len), *co = find(stbl, stbl + slen, "stco", &tlen);
        odt_u32 chunks = co ? be32(co + 4) : 0, e = 0, per = 0, got = 0, ok = sc && co;
        for (i = 0; ok && i < chunks; i++) {
            odt_u32 at = be32(co + 8 + 4 * i);
            if (e < be32(sc + 4) && be32(sc + 8 + 12 * e) == i + 1) per = be32(sc + 12 + 12 * e++);
            if (got + per > pcm_samples || at + per * channels > n ||
                memcmp(f + at, pcm + got * channels, per * channels)) ok = 0;
            got += per;
        }
        check(ok && got == pcm_samples, "the sound chunks, in order, are the sound", name);
    }
}

/* ---- one film -------------------------------------------------------------------------- */

static void save(const char *name, const char *ext, const void *p, odt_u32 n)
{
    char fn[512];
    FILE *fp;
    if (!dir) return;
    snprintf(fn, sizeof fn, "%s/%s.%s", dir, name, ext);
    if ((fp = fopen(fn, "wb"))) {
        fwrite(p, 1, n, fp);
        fclose(fp);
    }
}

#define SND_NONE  0
#define SND_FRAME 1     /* each frame its own, mono */
#define SND_FRAME2 2    /* each frame its own, stereo */
#define SND_FILM  3     /* the film's, mono */
#define SND_FILM2 4     /* the film's, stereo */

static void one(odt_u32 w, odt_u32 h, odt_u32 depth, odt_u32 frames, int kind, odt_u32 fps, int sound, int own_cmap,
                int durations, odt_s32 quality, odt_s32 level)
{
    struct film m;
    struct sink s;
    struct adt_mp4_spec spec;
    struct adt_mp4_io io;
    odt_u32 cw = (w + 1) & ~1u, ch = (h + 1) & ~1u, ysz = cw * ch * 3 / 2, i, f;
    odt_u8 *rgb, *yuv, *pcm = 0;
    odt_u32 pcm_n = 0, channels = 0, rate = 0, ticks = 0;
    odt_s32 err;

    memset(&m, 0, sizeof m);
    m.w = w; m.h = h; m.depth = depth; m.frames = frames; m.fps = fps;
    m.bpr = (((w + 15) >> 4) << 1) + 2;
    m.planes = malloc(frames * depth * h * m.bpr);
    m.modeid = kind == HAM6 || kind == HAM8 ? 0x800 : kind == EHB ? 0x80 : 0;
    m.ncolors = 1u << (depth > 8 ? 8 : depth);
    if (kind == HAM6) m.ncolors = 16;
    if (kind == HAM8) m.ncolors = 64;
    if (kind == EHB) m.ncolors = 32;
    for (i = 0; i < 256; i++) {
        /* colours with low bits set: no reader takes them for 4-bit OCS colours */
        m.cols[i].red = (odt_u8)(rnd() | 1); m.cols[i].green = (odt_u8)(rnd() | 2); m.cols[i].blue = (odt_u8)(rnd() | 4);
    }
    make(&m, kind);
    if (own_cmap) {
        m.fncol = m.ncolors;
        m.fcmap = calloc(frames, 768);
        for (i = 0; i < frames * 768; i++) m.fcmap[i] = (odt_u8)rnd();
    }
    if (durations) {
        m.dur = malloc(frames * sizeof *m.dur);
        for (f = 0; f < frames; f++) m.dur[f] = 1 + rnd() % 4;
    }
    for (f = 0; f < frames; f++) ticks += m.dur ? m.dur[f] : 1;
    snprintf(m.name, sizeof m.name, "%s-%lux%lux%lu-%lu%s%s%s%s-q%ld-l%ld", kind_name[kind], (unsigned long)w,
             (unsigned long)h, (unsigned long)depth, (unsigned long)frames, own_cmap ? "-cmap" : "",
             durations ? "-dur" : "",
             sound == SND_FRAME ? "-fsnd" : sound == SND_FRAME2 ? "-fsnd2" : sound == SND_FILM ? "-snd" :
             sound == SND_FILM2 ? "-snd2" : "", fps != 10 ? (fps == 25 ? "-25fps" : "-fps") : "", (long)quality,
             (long)level);

    /* the sound, and what it must decode as */
    if (sound == SND_FRAME || sound == SND_FRAME2) {
        m.fl = calloc(frames, sizeof *m.fl);
        m.fr = sound == SND_FRAME2 ? calloc(frames, sizeof *m.fr) : 0;
        m.flen = calloc(frames, sizeof *m.flen);
        m.frate = calloc(frames, sizeof *m.frate);
        channels = sound == SND_FRAME2 ? 2 : 1;
        rate = 11025;
        pcm = malloc(frames * 2 * 3000);
        for (f = 0; f < frames; f++) {
            if (f == 1) continue;       /* a frame with no sound */
            m.flen[f] = 200 + rnd() % 1500;
            m.frate[f] = f ? 22050 : rate;   /* the first rate given is the track's */
            m.fl[f] = malloc(m.flen[f]);
            if (m.fr) m.fr[f] = malloc(m.flen[f]);
            for (i = 0; i < m.flen[f]; i++) {
                m.fl[f][i] = (odt_s8)rnd();
                if (m.fr) m.fr[f][i] = (odt_s8)rnd();
                pcm[pcm_n * channels] = (odt_u8)m.fl[f][i];
                if (m.fr) pcm[pcm_n * channels + 1] = (odt_u8)m.fr[f][i];
                pcm_n++;
            }
        }
    } else if (sound == SND_FILM || sound == SND_FILM2) {
        channels = sound == SND_FILM2 ? 2 : 1;
        rate = 8363;
        m.wrate = rate;
        m.wlen = rate * ticks / fps + 777;   /* longer than the film: the rest goes with the last frame */
        m.wl = malloc(m.wlen);
        m.wr = channels == 2 ? malloc(m.wlen) : 0;
        pcm = malloc(m.wlen * channels);
        for (i = 0; i < m.wlen; i++) {
            m.wl[i] = (odt_s8)rnd();
            if (m.wr) m.wr[i] = (odt_s8)rnd();
            pcm[i * channels] = (odt_u8)m.wl[i];
            if (m.wr) pcm[i * channels + 1] = (odt_u8)m.wr[i];
        }
        pcm_n = m.wlen;
    }

    memset(&s, 0, sizeof s);
    s.film = &m;
    if (speed_mode == 1) goto out;
    if (speed_mode == 2) {
        struct adt_mp4_pal pal;
        odt_u8 fil[768], *row = malloc(ADT_MP4_ROW(cw)), *mine = malloc(ysz);
        for (i = 0; i < m.ncolors; i++) {
            fil[3 * i] = m.cols[i].red; fil[3 * i + 1] = m.cols[i].green; fil[3 * i + 2] = m.cols[i].blue;
        }
        adt_mp4_palette(fil, m.ncolors, depth, m.modeid, &pal);
        for (f = 0; f < frames; f++) {
            adt_mp4_yuv(s_frame(&s, f), w, h, depth, m.modeid, &pal, mine, mine + cw * ch, mine + cw * ch * 5 / 4, cw,
                        row);
            s_done(&s, f);
        }
        free(row);
        free(mine);
        goto out;
    }
    memset(&spec, 0, sizeof spec);
    spec.width = w; spec.height = h; spec.depth = depth; spec.frames = frames; spec.fps = fps;
    spec.ncolors = m.ncolors; spec.colors = m.cols; spec.modeid = m.modeid;
    spec.qp = adt_mp4_qp(quality);
    spec.speed = adt_mp4_speed(level);
    spec.gop = fps * 5 > 250 ? 250 : fps * 5;
    spec.sound = sound != SND_NONE;
    spec.left = m.wl; spec.right = m.wr; spec.sound_len = m.wlen; spec.rate = m.wrate ? m.wrate : 8000;
    io.aw.h = &s; io.aw.alloc = s_alloc; io.aw.free = s_free; io.aw.put = s_put; io.aw.patch = s_patch;
    io.aw.frame = s_frame; io.aw.done = s_done;
    io.info = s_info;

    err = adt_mp4_write(&spec, &io);
    check(err == 0, "writes", m.name);
    if (err) printf("  (error %ld)\n", (long)err);
    check(m.out == 0 && m.loads == frames, "every frame taken once and given back", m.name);
    check(s.allocs == 0, "every block freed", m.name);
    if (err || speed_mode) goto out;

    /* the frames as this file reads them, and their YUV (adt_mp4_yuv against ref_yuv) */
    rgb = malloc(w * h * 3 * frames);
    yuv = malloc(ysz * frames);
    {
        struct adt_mp4_pal pal;
        odt_u8 fil[768], *row = malloc(ADT_MP4_ROW(cw)), *mine = malloc(ysz);
        odt_u32 bad = 0;
        for (i = 0; i < m.ncolors; i++) {
            fil[3 * i] = m.cols[i].red; fil[3 * i + 1] = m.cols[i].green; fil[3 * i + 2] = m.cols[i].blue;
        }
        for (f = 0; f < frames; f++) {
            ref_rgb(&m, f, rgb + f * w * h * 3);
            ref_yuv(rgb + f * w * h * 3, w, h, yuv + f * ysz);
            adt_mp4_palette(m.fcmap ? m.fcmap + f * 768 : fil, m.fcmap ? m.fncol : m.ncolors, depth, m.modeid, &pal);
            adt_mp4_yuv(s_frame(&s, f), w, h, depth, m.modeid, &pal, mine, mine + cw * ch, mine + cw * ch * 5 / 4, cw,
                        row);
            s_done(&s, f);
            if (memcmp(mine, yuv + f * ysz, ysz)) bad++;
        }
        check(!bad, "each frame's YUV as BT.601 makes it from the planes", m.name);
        free(row);
        free(mine);
    }
    verify(&s, &m, pcm, pcm_n, channels, rate, spec.qp);
    if (dir) {
        save(m.name, "mp4", s.p, s.n);
        save(m.name, "rgb", rgb, w * h * 3 * frames);
        save(m.name, "yuv", yuv, ysz * frames);
        if (pcm_n) save(m.name, "pcm", pcm, pcm_n * channels);
        if (!own_cmap && !durations) {
            /* the same frames as an IFF ANIM, for FFmpeg to show them as it would */
            struct adt_aw_spec as;
            struct sink a;
            memset(&a, 0, sizeof a);
            a.film = &m;
            memset(&as, 0, sizeof as);
            as.width = w; as.height = h; as.depth = depth; as.frames = frames;
            as.ncolors = m.ncolors; as.colors = m.cols; as.modeid = m.modeid; as.has_modeid = 1;
            as.reltime = 6;
            io.aw.h = &a;
            if (!adt_anim_write(&as, &io.aw)) save(m.name, "anim", a.p, a.n);
            free(a.p);
            io.aw.h = &s;
        }
        fprintf(list, "%s %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu\n", m.name, (unsigned long)w, (unsigned long)h,
                (unsigned long)frames, (unsigned long)fps, (unsigned long)ticks, (unsigned long)spec.qp,
                (unsigned long)spec.speed, (unsigned long)spec.gop, (unsigned long)channels, (unsigned long)rate);
    }
    free(rgb);
    free(yuv);
out:
    free(s.p);
    free(m.planes);
    free(m.fcmap);
    free(m.dur);
    if (m.fl)
        for (f = 0; f < frames; f++) {
            free(m.fl[f]);
            if (m.fr) free(m.fr[f]);
        }
    free(m.fl); free(m.fr); free(m.flen); free(m.frate);
    free(m.wl); free(m.wr);
    free(pcm);
}

/* ---- the class: DTM_WRITE on a mock object ---------------------------------------------- */

static struct film *cls_film;
static odt_u8 cls_sound[2][4096];
static struct adt_BitMap cls_bm;
static odt_s32 last_err;
static struct sink cls_out;
static odt_u32 cls_outstanding;

/* "colour maps": a pointer into the film's fcmap */
static odt_u32 c_cmap_rgb(const struct odt_cenv *e, void *cmap, odt_u8 *rgb, odt_u32 max)
{
    (void)e;
    memcpy(rgb, cmap, (max < cls_film->fncol ? max : cls_film->fncol) * 3);
    return max < cls_film->fncol ? max : cls_film->fncol;
}

static odt_u32 c_clock(const struct odt_cenv *e) { (void)e; return 3546895UL; }
static odt_u32 stack_calls;
static odt_u32 c_call_on_stack(const struct odt_cenv *e, odt_u32 bytes, odt_u32 (*fn)(void *), void *arg)
{
    (void)e;
    if (bytes >= 16384) stack_calls++;
    return fn(arg);
}

static const struct adt_ext cls_ext = {
    .clock = c_clock, .cmap_rgb = c_cmap_rgb, .call_on_stack = c_call_on_stack
};

static odt_u32 c_do_method(const struct odt_cenv *e, void *obj, void *msg)
{
    struct adt_Frame *a = msg;
    struct film *m = cls_film;
    odt_u32 p, f = a->alf_TimeStamp;
    (void)e; (void)obj;
    switch (a->MethodID) {
    case ADT_ADTM_LOADNEWFORMATFRAME:
    case ADT_ADTM_LOADFRAME:
        if (f >= m->frames) return 0;
        cls_bm.BytesPerRow = (odt_u16)m->bpr;
        cls_bm.Rows = (odt_u16)m->h;
        cls_bm.Depth = (odt_u8)m->depth;
        for (p = 0; p < 8; p++) cls_bm.Planes[p] = p < m->depth ? plane_byte(m, f, p, 0, 0) : 0;
        a->alf_BitMap = &cls_bm;
        a->alf_Duration = m->dur ? m->dur[f] : 1;
        a->alf_CMap = m->fcmap ? m->fcmap + f * 768 : 0;
        for (p = 0; p < 2; p++) memset(cls_sound[p], (int)(f * 2 + p + 1), 400);
        a->alf_Sample = (odt_s8 *)cls_sound[0];
        a->alf_SampleLength = 400;
        a->alf_Period = 322;            /* 3546895 / 322 = 11015 Hz */
        if (a->MethodID == ADT_ADTM_LOADNEWFORMATFRAME) {
            a->alf_LeftSample = (odt_s8 *)cls_sound[0];
            a->alf_RightSample = (odt_s8 *)cls_sound[1];
            a->alf_SamplesPerSec = 16000;
        }
        cls_outstanding++;
        return 1;
    case ADT_ADTM_UNLOADFRAME:
    case ADT_ADTM_UNLOADNEWFORMATFRAME:
        cls_outstanding--;
        a->alf_BitMap = 0;
        return 0;
    }
    return 0;
}

static odt_u32 c_do_super(const struct odt_cenv *e, struct odt_IClass *cl, void *obj, void *msg)
{
    (void)e; (void)cl; (void)obj;
    if (*(odt_u32 *)msg == ODT_DTM_WRITE) return 77;     /* the superclass's raw write */
    return 0;
}

static void *c_alloc(const struct odt_cenv *e, odt_u32 n) { (void)e; return calloc(1, n ? n : 1); }
static void c_free(const struct odt_cenv *e, void *p) { (void)e; free(p); }
static void c_set_io_err(const struct odt_cenv *e, odt_s32 c) { (void)e; last_err = c; }
static odt_s32 c_io_err(const struct odt_cenv *e) { (void)e; return last_err; }
/* a file: written at its position, which Seek moves */
static odt_u32 cls_at;
static odt_s32 c_write(const struct odt_cenv *e, odt_u32 fh, const void *b, odt_s32 n)
{
    (void)e; (void)fh;
    if (cls_at + (odt_u32)n > cls_out.n) {
        odt_u32 more = cls_at + (odt_u32)n - cls_out.n;
        odt_u8 *z = calloc(1, more);
        s_put(&cls_out, z, more);
        free(z);
    }
    memcpy(cls_out.p + cls_at, b, (size_t)n);
    cls_at += (odt_u32)n;
    return n;
}
static odt_s32 c_seek(const struct odt_cenv *e, odt_u32 fh, odt_s32 off, odt_s32 mode)
{
    odt_s32 was = (odt_s32)cls_at;
    (void)e; (void)fh;
    if (mode == -1) cls_at = (odt_u32)off;
    else if (mode == 1) cls_at = cls_out.n + (odt_u32)off;
    else cls_at += (odt_u32)off;
    return was;
}

static odt_u32 cls_write(struct film *m, int newformat, struct odt_TagItem *tags, odt_u32 mode)
{
    struct odt_cenv env;
    struct odt_IClass cl;
    struct {
        struct odt_Gadget g;
        odt_u8 pad[64];
        struct adt_inst in;
    } obj;
    struct odt_dtWrite w;
    odt_u32 r;
    memset(&env, 0, sizeof env);
    env.do_method = c_do_method;
    env.do_super = c_do_super;
    env.alloc = c_alloc;
    env.free = c_free;
    env.set_io_err = c_set_io_err;
    env.io_err = c_io_err;
    env.write = c_write;
    env.seek = c_seek;
    env.class_ext = (void *)&cls_ext;
    memset(&cl, 0, sizeof cl);
    memset(&obj, 0, sizeof obj);
    cl.cl_InstOffset = (odt_u16)((odt_u8 *)&obj.in - (odt_u8 *)&obj);
    obj.in.cl = &cl;
    obj.in.width = m->w; obj.in.height = m->h; obj.in.depth = m->depth; obj.in.frames = m->frames;
    obj.in.fps = m->fps;
    obj.in.mode_id = m->modeid;
    obj.in.num_colors = m->ncolors;
    obj.in.colregs = m->cols;
    obj.in.newformat = (odt_u8)(newformat ? 1 : 2);   /* NF_NEW, NF_OLD */
    cls_film = m;
    free(cls_out.p);
    memset(&cls_out, 0, sizeof cls_out);
    cls_at = 0;
    cls_outstanding = 0;
    last_err = 0;
    w.MethodID = ODT_DTM_WRITE;
    w.dtw_GInfo = 0;
    w.dtw_FileHandle = 1234;
    w.dtw_Mode = mode;
    w.dtw_AttrList = tags;
    r = adt_dispatch(&env, &cl, &obj, &w);
    return r;
}

static odt_u32 tracks(const struct sink *s)
{
    odt_u32 n = 0, len, mlen;
    const odt_u8 *moov;
    if (s->n < 48) return 0;
    moov = find(s->p + 32 + be32(s->p + 32), s->p + s->n, "moov", &mlen);
    while (moov && trak(moov, mlen, (int)n, &len)) n++;
    return n;
}

static void class_checks(void)
{
    struct film m;
    struct odt_TagItem mp4[] = { { ADT_OIA_WriteFormat, ADT_OIWF_MP4 }, { ODT_TAG_DONE, 0 } };
    struct odt_TagItem quiet[] = { { ADT_OIA_WriteFormat, ADT_OIWF_MP4 }, { ADT_OIA_WriteSound, 0 },
                                   { ADT_OIA_Quality, 100 }, { ADT_OIA_Compression, 9 }, { ODT_TAG_DONE, 0 } };
    struct odt_TagItem other[] = { { ADT_OIA_WriteFormat, 0x57454D42 }, { ODT_TAG_DONE, 0 } };
    const char *name = "class";
    odt_u32 r, i, f;

    memset(&m, 0, sizeof m);
    m.w = 37; m.h = 23; m.depth = 4; m.frames = 5; m.fps = 12; m.ncolors = 16;
    m.bpr = 8;
    m.planes = malloc(m.frames * m.depth * m.h * m.bpr);
    for (i = 0; i < 16; i++) { m.cols[i].red = (odt_u8)(i * 17); m.cols[i].green = (odt_u8)(255 - i * 9); m.cols[i].blue = 77; }
    make(&m, BOX);
    m.fncol = 16;
    m.fcmap = calloc(m.frames, 768);
    for (i = 0; i < m.frames * 768; i++) m.fcmap[i] = (odt_u8)rnd();
    m.dur = malloc(m.frames * sizeof *m.dur);
    for (f = 0; f < m.frames; f++) m.dur[f] = f + 1;
    snprintf(m.name, sizeof m.name, "class");

    /* raw with no format: the superclass's (the file as it came) */
    r = cls_write(&m, 0, 0, ODT_DTWM_RAW);
    check(r == 77, "raw with no OIA_WriteFormat goes to the superclass", name);
    /* raw with a format it doesn't know: refused, not the file as it came */
    r = cls_write(&m, 0, other, ODT_DTWM_RAW);
    check(r == 0 && last_err == ADT_ERROR_NOT_IMPLEMENTED && cls_out.n == 0, "another format: ERROR_NOT_IMPLEMENTED",
          name);
    /* MP4, frames from ADTM_LOADFRAME: mono sound at clock / period */
    stack_calls = 0;
    r = cls_write(&m, 0, mp4, ODT_DTWM_RAW);
    check(r == 1 && cls_outstanding == 0 && stack_calls == 1, "MP4 through ADTM_LOADFRAME, on a stack of its own",
          name);
    if (r) {
        struct sink s = cls_out;
        odt_u8 pcm[5 * 400];
        for (f = 0; f < 5; f++) memset(pcm + f * 400, (int)(f * 2 + 1), 400);
        m.ncolors = 16;
        snprintf(m.name, sizeof m.name, "class-loadframe");
        verify(&s, &m, pcm, 5 * 400, 1, 3546895UL / 322, 0);
        if (dir) {
            save("class-loadframe", "mp4", s.p, s.n);
            fprintf(list, "#class-loadframe %lu %lu %lu %lu\n", (unsigned long)m.w, (unsigned long)m.h,
                    (unsigned long)m.frames, (unsigned long)m.fps);
        }
    }
    /* MP4, V44 frames: stereo at alf_SamplesPerSec */
    r = cls_write(&m, 1, mp4, ODT_DTWM_RAW);
    check(r == 1 && cls_outstanding == 0, "MP4 through ADTM_LOADNEWFORMATFRAME", name);
    if (r) {
        struct sink s = cls_out;
        odt_u8 pcm[5 * 800];
        for (f = 0; f < 5; f++)
            for (i = 0; i < 400; i++) {
                pcm[f * 800 + 2 * i] = (odt_u8)(f * 2 + 1);
                pcm[f * 800 + 2 * i + 1] = (odt_u8)(f * 2 + 2);
            }
        snprintf(m.name, sizeof m.name, "class-newformat");
        verify(&s, &m, pcm, 5 * 400, 2, 16000, 0);
        if (dir) {
            save("class-newformat", "mp4", s.p, s.n);
            fprintf(list, "#class-newformat %lu %lu %lu %lu\n", (unsigned long)m.w, (unsigned long)m.h,
                    (unsigned long)m.frames, (unsigned long)m.fps);
        }
    }
    /* OIA_WriteSound FALSE, OIA_Quality 100, OIA_Compression 9: no sound, bigger */
    {
        odt_u32 before = cls_out.n;
        r = cls_write(&m, 1, quiet, ODT_DTWM_RAW);
        check(r == 1 && tracks(&cls_out) == 1, "OIA_WriteSound FALSE leaves the sound out", name);
        check(cls_out.n > before - 5 * 800, "OIA_Quality 100 makes a bigger file", name);
    }
    /* OIA_Quality 0 is the default, as no tag */
    {
        struct odt_TagItem q0[] = { { ADT_OIA_WriteFormat, ADT_OIWF_MP4 }, { ADT_OIA_Quality, 0 }, { ODT_TAG_DONE, 0 } };
        odt_u8 *a;
        odt_u32 an;
        r = cls_write(&m, 1, mp4, ODT_DTWM_RAW);
        an = cls_out.n;
        a = malloc(an);
        memcpy(a, cls_out.p, an);
        r += cls_write(&m, 1, q0, ODT_DTWM_RAW);
        check(r == 2 && an == cls_out.n && !memcmp(a, cls_out.p, an), "OIA_Quality 0 is the default", name);
        free(a);
    }
    /* IFF is still an ANIM */
    r = cls_write(&m, 0, mp4, ODT_DTWM_IFF);
    check(r == 1 && cls_out.n > 12 && !memcmp(cls_out.p, "FORM", 4) && !memcmp(cls_out.p + 8, "ANIM", 4),
          "DTWM_IFF with OIA_WriteFormat is still an IFF ANIM", name);
    free(cls_out.p);
    memset(&cls_out, 0, sizeof cls_out);
    free(m.planes);
    free(m.fcmap);
    free(m.dur);
}

int main(int argc, char **argv)
{
    static const odt_s32 qs[] = { -1, 100, 1 };
    if (argc == 9 && !strcmp(argv[1], "-speed")) {
        /* one film, for counting instructions: no checks, no files */
        int kind;
        speed_mode = !strcmp(argv[2], "gen") ? 1 : !strcmp(argv[2], "yuv") ? 2 : !strcmp(argv[2], "write") ? 3 : 0;
        for (kind = 0; kind < KINDS && strcmp(argv[6], kind_name[kind]); kind++) {}
        if (kind == KINDS || !speed_mode) return 2;
        one((odt_u32)atol(argv[3]), (odt_u32)atol(argv[4]), kind == HAM6 ? 6 : kind == HAM8 ? 8 : kind == EHB ? 6 : 5,
            (odt_u32)atol(argv[5]), kind, 25, SND_NONE, 0, 0, atol(argv[7]), atol(argv[8]));
        return fails != 0;
    }
    dir = argc > 1 ? argv[1] : 0;
    if (dir) {
        char fn[512];
        snprintf(fn, sizeof fn, "%s/films.txt", dir);
        if (!(list = fopen(fn, "w"))) return 2;
    }
    {
        /* sizes: the smallest, odd ones, a typical PAL lowres and one past a macroblock's edge */
        static const odt_u32 sizes[][2] = { { 1, 1 }, { 2, 2 }, { 17, 9 }, { 33, 31 }, { 64, 48 }, { 320, 256 } };
        odt_u32 si, d;
        for (si = 0; si < sizeof sizes / sizeof sizes[0]; si++)
            one(sizes[si][0], sizes[si][1], 3, sizes[si][0] >= 320 ? 3 : 4, BOX, 10, SND_NONE, 0, 0, -1, -1);
        for (d = 1; d <= 8; d++) one(48 + d, 32, d, 3, d == 1 ? BOX : PLASMA, 10, SND_NONE, 0, 0, -1, -1);
    }
    one(160, 128, 5, 4, NOISE, 10, SND_NONE, 0, 0, -1, -1);
    one(96, 64, 4, 6, STILL, 25, SND_NONE, 0, 0, -1, -1);
    one(160, 100, 6, 4, HAM6, 10, SND_NONE, 0, 0, -1, -1);
    one(161, 101, 8, 4, HAM8, 10, SND_NONE, 0, 0, -1, -1);
    one(120, 80, 6, 4, EHB, 10, SND_NONE, 0, 0, -1, -1);
    one(80, 60, 5, 6, PLASMA, 10, SND_NONE, 1, 1, -1, -1);
    one(64, 48, 4, 5, BOX, 12, SND_FRAME, 0, 0, -1, -1);
    one(64, 48, 4, 5, BOX, 12, SND_FRAME2, 0, 1, -1, -1);
    one(64, 48, 4, 7, BOX, 25, SND_FILM, 0, 0, -1, -1);
    one(64, 48, 4, 7, BOX, 7, SND_FILM2, 0, 1, -1, -1);
    {
        odt_u32 q;
        for (q = 0; q < sizeof qs / sizeof qs[0]; q++) one(96, 64, 5, 4, PLASMA, 10, SND_NONE, 0, 0, qs[q], 9);
        for (q = 0; q <= 9; q += 3) one(96, 64, 5, 3, BOX, 10, SND_NONE, 0, 0, -1, (odt_s32)q);
    }
    {
        /* PAL lowres films at each speed (their sizes and PSNR go in the log) */
        static const odt_s32 lv[] = { 0, 1, 2, 5, 9 };
        odt_u32 k;
        for (k = 0; k < sizeof lv / sizeof lv[0]; k++) {
            one(320, 256, 5, 4, PLASMA, 25, SND_NONE, 0, 0, -1, lv[k]);
            one(320, 256, 5, 4, BOX, 25, SND_NONE, 0, 0, -1, lv[k]);
        }
        one(320, 256, 8, 4, HAM8, 25, SND_NONE, 0, 0, -1, -1);
    }
    /* key frames: 30 frames at 5 a second, a key frame every 25 */
    one(32, 32, 2, 30, BOX, 5, SND_NONE, 0, 0, -1, -1);
    class_checks();
    if (list) fclose(list);
    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
