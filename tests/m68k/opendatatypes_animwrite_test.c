/* GFOpenDatatypes animation.datatype's IFF ANIM writer (adt_anim_write in
 * animation_core.c), checked under qemu-m68k against anim.datatype's own
 * reader (anim_core.c): films of 1 to 8 planes and 1 to 12 frames, made
 * here (noise, a moving box, still frames, runs, a whole frame changed,
 * rows enough that a column needs more than 255 ops), are written, then
 * indexed with anm_scan and every frame decoded with anm_goto, forward,
 * backward and at random, and compared with what went in (the bits past
 * the width cleared); the size, depth, colours, CAMG and fps too. Each
 * file also goes to DIR (when given) for an outside reader.
 *
 *     animwrite_test [DIR]
 *
 * Copyright (c) 2026 AmiCradle Team. MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "animation/animation_core.h"
#include "anim/anim_core.h"

static int fails, checks;
static unsigned long seed = 2026;

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

/* the frames as a subclass would give them: BitMaps with a wider row */
struct film {
    odt_u32 w, h, depth, frames, bpr;
    odt_u8 *planes;                     /* frames x depth x h x bpr */
    struct adt_BitMap bm;
    int out;                            /* frames handed out and not given back */
};

struct sink {
    odt_u8 *p;
    odt_u32 n, cap;
    struct film *film;
};

static void *s_alloc(void *h, odt_u32 n) { (void)h; return calloc(1, n); }
static void s_free(void *h, void *p) { (void)h; free(p); }

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
    struct sink *s = h;
    struct film *m = s->film;
    odt_u32 p;
    m->bm.BytesPerRow = (odt_u16)m->bpr;
    m->bm.Rows = (odt_u16)m->h;
    m->bm.Depth = (odt_u8)m->depth;
    for (p = 0; p < 8; p++)
        m->bm.Planes[p] = p < m->depth ? m->planes + ((f * m->depth + p) * m->h) * m->bpr : 0;
    m->out++;
    return &m->bm;
}

static void s_done(void *h, odt_u32 f) { (void)f; ((struct sink *)h)->film->out--; }

static void *a_alloc(void *ctx, odt_u32 n) { (void)ctx; return calloc(1, n); }
static void a_free(void *ctx, void *p) { (void)ctx; free(p); }

enum { NOISE, BOX, STILL, RUNS, FLIP, TALL, KINDS };
static const char *kind_name[] = { "noise", "box", "still", "runs", "flip", "tall" };

static void make(struct film *m, int kind)
{
    odt_u32 f, p, y, x;
    for (f = 0; f < m->frames; f++)
        for (p = 0; p < m->depth; p++)
            for (y = 0; y < m->h; y++)
                for (x = 0; x < m->bpr; x++) {
                    odt_u8 *b = m->planes + ((f * m->depth + p) * m->h + y) * m->bpr + x, v;
                    odt_u32 px = x * 8;
                    switch (kind) {
                    case NOISE: v = (odt_u8)rnd(); break;
                    case BOX: {         /* a box of colour f+1 moving over a striped ground */
                        odt_u32 bx = f * 8, by = f * 3, c = (f + 1) >> p & 1;
                        v = (odt_u8)(y & 1 ? 0x55 : 0);
                        if (y >= by && y < by + 12 && px + 8 > bx && px < bx + 24) v = c ? 0xFF : 0x00;
                        break;
                    }
                    case STILL: v = (odt_u8)((x * 37 + y * 11 + p) & 0xFF); break;
                    case RUNS: v = (odt_u8)(((y / 9) + f + p) & 1 ? 0xF0 : 0x0F); break;
                    case FLIP: v = (odt_u8)(f & 1 ? 0xAA : 0x55); break;
                    default:            /* every other row changes: many ops a column */
                        v = (odt_u8)((y % 3 == 0 && f) ? rnd() : 0x33);
                        break;
                    }
                    *b = v;
                }
}

static void one(const char *dir, odt_u32 w, odt_u32 h, odt_u32 depth, odt_u32 frames, int kind, odt_u32 fps)
{
    char name[96];
    struct film m;
    struct sink s;
    struct adt_aw_spec spec;
    struct adt_aw_io io;
    struct adt_ColorRegister cols[256];
    struct anm_env aenv = { 0, a_alloc, a_free };
    struct anm_info info;
    struct anm_dec dec;
    odt_u32 rb = ((w + 15) >> 4) << 1, i, f, ncol = 1u << depth;
    odt_s32 err;
    odt_u8 rgb[768];
    int ok = 1;

    snprintf(name, sizeof name, "%s-%lux%lux%lu-%lu", kind_name[kind], (unsigned long)w, (unsigned long)h,
             (unsigned long)depth, (unsigned long)frames);
    memset(&m, 0, sizeof m);
    m.w = w; m.h = h; m.depth = depth; m.frames = frames;
    m.bpr = rb + 4;                     /* a wider BitMap than the file's rows */
    m.planes = malloc(frames * depth * h * m.bpr);
    make(&m, kind);
    for (i = 0; i < 256; i++) {
        cols[i].red = (odt_u8)rnd(); cols[i].green = (odt_u8)rnd(); cols[i].blue = (odt_u8)rnd();
    }
    memset(&s, 0, sizeof s);
    s.film = &m;
    spec.width = w; spec.height = h; spec.depth = depth; spec.frames = frames;
    spec.ncolors = ncol; spec.colors = cols;
    spec.modeid = 0x8004; spec.has_modeid = 1;
    spec.xaspect = 10; spec.yaspect = 11; spec.pad = 0;
    spec.reltime = (60 + fps / 2) / fps;
    io.h = &s; io.alloc = s_alloc; io.free = s_free; io.put = s_put; io.patch = s_patch;
    io.frame = s_frame; io.done = s_done;

    err = adt_anim_write(&spec, &io);
    check(err == 0, "writes", name);
    check(m.out == 0, "every frame given back", name);
    if (err) goto out;
    check(s.n >= 12 && !memcmp(s.p, "FORM", 4) && !memcmp(s.p + 8, "ANIM", 4)
          && (odt_u32)((s.p[4] << 24) | (s.p[5] << 16) | (s.p[6] << 8) | s.p[7]) == s.n - 8,
          "FORM ANIM and its size", name);
    if (dir) {
        char path[512];
        FILE *fp;
        snprintf(path, sizeof path, "%s/%s.anim", dir, name);
        if ((fp = fopen(path, "wb"))) {
            fwrite(s.p, 1, s.n, fp);
            fclose(fp);
        }
    }
    memset(&info, 0, sizeof info);
    check(anm_scan(&aenv, s.p, s.n, &info) == ANM_OK, "our reader indexes it", name);
    check(info.bmhd.w == w && info.bmhd.h == h && info.depth == depth && info.nframes == frames,
          "its size, depth and frames", name);
    check(info.has_camg && info.camg == 0x8004, "the CAMG", name);
    check(anm_colors(&info, s.p, 0, rgb) == ncol, "the CMAP's colours", name);
    for (i = 0; i < ncol; i++)
        if (rgb[3 * i] != cols[i].red || rgb[3 * i + 1] != cols[i].green || rgb[3 * i + 2] != cols[i].blue)
            ok = 0;
    check(ok, "every colour", name);
    if (frames > 1)
        check(info.fps == (60 + spec.reltime / 2) / spec.reltime, "fps", name);
    if (anm_dec_init(&aenv, &dec, &info, s.p, s.n) != ANM_OK) {
        check(0, "the decoder starts", name);
        anm_free_info(&aenv, &info);
        goto out;
    }
    /* forward, backward, at random */
    for (i = 0; i < frames * 3; i++) {
        odt_u32 p, y, x, bad = 0;
        const odt_u8 *got;
        f = i < frames ? i : i < 2 * frames ? 2 * frames - 1 - i : rnd() % frames;
        check(anm_goto(&dec, f) == ANM_OK, "decodes the frame", name);
        got = anm_planes(&dec);
        for (p = 0; p < depth && !bad; p++)
            for (y = 0; y < h && !bad; y++)
                for (x = 0; x < rb; x++) {
                    odt_u32 used = (w + 7) >> 3;
                    odt_u8 want = x < used ? m.planes[((f * depth + p) * h + y) * m.bpr + x] : 0;
                    if (x + 1 == used && (w & 7)) want &= (odt_u8)(0xFF00 >> (w & 7));
                    if (got[(p * h + y) * rb + x] != want) {
                        printf("  %s: frame %lu plane %lu row %lu byte %lu is %02x, not %02x\n", name,
                               (unsigned long)f, (unsigned long)p, (unsigned long)y, (unsigned long)x,
                               got[(p * h + y) * rb + x], want);
                        bad = 1;
                        break;
                    }
                }
        if (bad) ok = 0;
    }
    check(ok, "every frame as written", name);
    anm_dec_free(&aenv, &dec);
    anm_free_info(&aenv, &info);
out:
    free(s.p);
    free(m.planes);
}

int main(int argc, char **argv)
{
    static const odt_u32 sizes[][2] = { {1, 1}, {7, 3}, {16, 16}, {33, 20}, {320, 64}, {17, 300} };
    const char *dir = argc > 1 ? argv[1] : 0;
    struct adt_aw_spec bad;
    struct adt_aw_io io;
    odt_u32 s, d;
    int k;

    for (s = 0; s < sizeof sizes / sizeof sizes[0]; s++)
        for (d = 1; d <= 8; d++)
            for (k = 0; k < TALL; k++)
                one(dir, sizes[s][0], sizes[s][1], d, k == STILL ? 4 : 1 + (s + d + (odt_u32)k) % 12, k, 5 + d);
    /* a column of more than 255 ops, and one that needs the fewest */
    one(dir, 8, 1200, 2, 5, TALL, 25);
    one(dir, 40, 3000, 1, 3, TALL, 50);
    one(dir, 640, 512, 8, 3, NOISE, 10);

    memset(&bad, 0, sizeof bad);
    memset(&io, 0, sizeof io);
    check(adt_anim_write(&bad, &io) == DTC_ERROR_REQUIRED_ARG_MISSING, "an empty spec", "refuse");
    bad.width = 1; bad.height = 1; bad.depth = 9; bad.frames = 1;
    check(adt_anim_write(&bad, &io) == DTC_ERROR_REQUIRED_ARG_MISSING, "9 planes", "refuse");

    printf("animwrite: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
