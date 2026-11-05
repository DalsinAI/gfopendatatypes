/*
 * gfopengif.datatype's decoder (gif/gifdecode.c) on x86 or ARM64 cores: reads
 * each file named and prints, for tests/test-gif.sh to compare with
 * tests/make-gif.py's expected.txt, one line a file and one a frame:
 *
 *     NAME WIDTH HEIGHT frames=N loops=L          (or NAME invalid)
 *     NAME#I delay=D alpha=0|1 fnv=HASH
 *
 * HASH is an FNV-1a hash of the composited screen as big-endian ARGB
 * bytes. The frames are shown in order, then the last one again from the
 * start and frame 0 again, which must give the same hashes (checked here).
 *
 * With -cuts first, it also decodes every file cut short at every length
 * and with every byte of its first 64 changed, and prints only a count:
 * run under AddressSanitizer, a read or write outside the file or the
 * screen stops it.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gifdecode.h"

static UBYTE *slurp(const char *path, ULONG *len)
{
    FILE *f = fopen(path, "rb");
    UBYTE *buf = NULL;
    long n;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0
        && (buf = malloc(n ? (size_t)n : 1)) && fread(buf, 1, (size_t)n, f) == (size_t)n)
        *len = (ULONG)n;
    else {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    return buf;
}

static ULONG fnv(const ULONG *argb, ULONG count)
{
    ULONG h = 2166136261UL, i;
    int b;
    for (i = 0; i < count; i++)
        for (b = 24; b >= 0; b -= 8) {
            h ^= (argb[i] >> b) & 0xff;
            h *= 16777619UL;
        }
    return h;
}

struct decoded {
    struct gif_info g;
    struct gif_frame *frame;
    struct gif_canvas c;
};

static int open_gif(const UBYTE *file, ULONG len, struct decoded *d)
{
    LONG n;
    memset(d, 0, sizeof *d);
    if ((n = gif_scan(file, len, &d->g, NULL, 0)) < 0)
        return -1;
    d->frame = malloc(sizeof(struct gif_frame) * (size_t)n);
    d->c.argb = malloc((size_t)d->g.width * d->g.height * 4);
    d->c.saved = d->g.restores ? malloc((size_t)d->g.width * d->g.height * 4) : NULL;
    d->c.work = malloc(GIF_WORKSIZE);
    d->c.shown = -1;
    if (!d->frame || !d->c.argb || (d->g.restores && !d->c.saved) || !d->c.work
        || gif_scan(file, len, &d->g, d->frame, (ULONG)n) != n)
        return -1;
    return 0;
}

static void close_gif(struct decoded *d)
{
    free(d->frame);
    free(d->c.argb);
    free(d->c.saved);
    free(d->c.work);
}

/* gif_frame_lut8() on frame 0, mapped through its table, against
 * gif_show()'s frame 0 on the screen now (gfopengif.datatype gives
 * picture.datatype the indexes when it can: no holes, or a transparent
 * index to fill them with). 1 when they differ. */
static int lut8_differs(const UBYTE *file, ULONG len, struct decoded *d)
{
    const struct gif_frame *f = &d->frame[0];
    ULONG count = d->g.width * d->g.height, holes, n;
    UBYTE *pixels = malloc(count ? count : 1), fill = f->transparent ? f->trans : 0;
    int differs = 0;
    if (!pixels)
        return 1;
    gif_frame_lut8(file, len, &d->g, f, pixels, d->c.work, fill, &holes);
    if (holes && !f->transparent) {
        free(pixels);
        return 0;                      /* not given as indexes: nothing to compare */
    }
    for (n = 0; n < count && !differs; n++) {
        UBYTE i = pixels[n];
        ULONG want = 0;
        if (i < f->colours && !(f->transparent && i == f->trans)) {
            const UBYTE *c = file + f->palette + 3 * i;
            want = 0xff000000UL | (ULONG)c[0] << 16 | (ULONG)c[1] << 8 | c[2];
        }
        if (want != d->c.argb[n])
            differs = 1;
    }
    free(pixels);
    return differs;
}

static int run(const char *name, const UBYTE *file, ULONG len, int quiet)
{
    struct decoded d;
    ULONG i, count, *hash;
    int bad = 0;

    if (open_gif(file, len, &d)) {
        if (!quiet)
            printf("%s invalid\n", name);
        close_gif(&d);
        return 0;
    }
    count = d.g.width * d.g.height;
    hash = malloc(sizeof(ULONG) * d.g.frames);
    if (!quiet)
        printf("%s %lu %lu frames=%lu loops=%ld\n", name, (unsigned long)d.g.width, (unsigned long)d.g.height,
            (unsigned long)d.g.frames, (long)d.g.loops);
    for (i = 0; i < d.g.frames; i++) {
        gif_show(file, len, &d.g, d.frame, &d.c, i);
        hash[i] = fnv(d.c.argb, count);
        if (!quiet)
            printf("%s#%lu delay=%u alpha=%ld fnv=%08lx\n", name, (unsigned long)i, d.frame[i].delay,
                (long)gif_has_alpha(&d.g, &d.c), (unsigned long)hash[i]);
    }
    /* Going back: frame 0, then the last again from the start. */
    gif_show(file, len, &d.g, d.frame, &d.c, 0);
    if (fnv(d.c.argb, count) != hash[0])
        bad = 1;
    if (lut8_differs(file, len, &d))
        bad |= 2;
    gif_show(file, len, &d.g, d.frame, &d.c, d.g.frames - 1);
    if (fnv(d.c.argb, count) != hash[d.g.frames - 1])
        bad = 1;
    if ((bad & 1) && !quiet)
        printf("%s going back gives other pixels\n", name);
    if ((bad & 2) && !quiet)
        printf("%s frame 0 as colour indexes gives other pixels\n", name);
    free(hash);
    close_gif(&d);
    return bad;
}

int main(int argc, char **argv)
{
    int i, cuts = 0, bad = 0;
    unsigned long tries = 0;

    if (argc > 1 && !strcmp(argv[1], "-cuts")) {
        cuts = 1;
        argc--;
        argv++;
    }
    for (i = 1; i < argc; i++) {
        ULONG len = 0, n, at;
        UBYTE *file = slurp(argv[i], &len);
        if (!file) {
            printf("%s unreadable\n", argv[i]);
            bad = 1;
            continue;
        }
        if (!cuts) {
            bad |= run(argv[i], file, len, 0);
            free(file);
            continue;
        }
        for (n = 0; n < len; n++) {            /* cut short: exactly n bytes, in a buffer of n */
            UBYTE *cut = malloc(n ? n : 1);
            memcpy(cut, file, n);
            run(argv[i], cut, n, 1);
            free(cut);
            tries++;
        }
        for (at = 0; at < len && at < 64; at++) {
            static const UBYTE values[] = { 0x00, 0x01, 0x07, 0x2c, 0x3b, 0x80, 0xff };
            ULONG v;
            for (v = 0; v < sizeof values; v++) {
                UBYTE *changed = malloc(len);
                memcpy(changed, file, len);
                changed[at] = values[v];
                run(argv[i], changed, len, 1);
                free(changed);
                tries++;
            }
        }
        free(file);
    }
    if (cuts)
        printf("giftest: %lu damaged files decoded without a fault\n", tries);
    return bad;
}
