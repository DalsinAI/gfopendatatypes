/*
 * cdxl.datatype's core on the host (tests/test-cdxl.sh): walks a CDXL film
 * frame by frame as the datatype does, and writes what each frame shows
 * (NAME.got.rgb) and its sound as 8-bit mono (NAME.got.pcm), with one line
 * a film: NAME width height frames fps rate. A film cut short or with a bad
 * header plays the frames before it, as the datatype does. The frames are
 * found and read as the datatype does (cdxl_index_open, cdxl_read_frame);
 * with CDXL_IOLOG set, how many reads and seeks that took goes there.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cdxl_core.h"

/* The film through the core's own index and reader (cdxl_index_open,
 * cdxl_read_frame), as the datatype reads it; each read counted, and each
 * seek (a read not where the last one ended) */
struct film {
    FILE *f;
    long pos;
    unsigned long reads, seeks, sizes;
};

static long f_read(void *ctx, uint32_t at, void *buf, uint32_t n)
{
    struct film *m = ctx;
    size_t got;
    m->reads++;
    if ((long)at != m->pos) {
        m->seeks++;
        if (fseek(m->f, (long)at, SEEK_SET))
            return -1;
    }
    got = fread(buf, 1, n, m->f);
    m->pos = (long)at + (long)got;
    return (long)got;
}

static long f_size(void *ctx)
{
    struct film *m = ctx;
    long n;
    m->sizes++;
    if (fseek(m->f, 0, SEEK_END))
        return -1;
    n = ftell(m->f);
    m->pos = n;
    return n;
}

static void *f_alloc(void *ctx, uint32_t n)
{
    (void)ctx;
    return malloc(n);
}

static void f_free(void *ctx, void *p)
{
    (void)ctx;
    free(p);
}

static FILE *iolog;

static int play(const char *path, const char *name)
{
    struct film m;
    struct cdxl_io io = { &m, f_read, f_size, f_alloc, f_free };
    struct cdxl_index ix;
    FILE *rgb, *pcm;
    char out[512];
    uint8_t *frame, *pix;
    struct cdxl_frame first, fr;
    unsigned frames = 0, i;
    unsigned long open_reads, open_seeks;

    memset(&m, 0, sizeof m);
    if (!(m.f = fopen(path, "rb")))
        return 1;
    snprintf(out, sizeof out, "%s.got.rgb", name);
    rgb = fopen(out, "wb");
    snprintf(out, sizeof out, "%s.got.pcm", name);
    pcm = fopen(out, "wb");
    memset(&first, 0, sizeof first);
    if (!cdxl_index_open(&io, &ix)) {
        open_reads = m.reads;
        open_seeks = m.seeks;
        first = ix.first;
        fprintf(iolog, "%s: %s, %lu reads and %lu length to open,", name, ix.fixed ? "constant time" : "frame by frame",
                open_reads, m.sizes);
        m.reads = m.seeks = 0;
        for (i = 0; i < ix.frames; i++) {
            if (!(frame = malloc(ix.max_size)))
                break;
            if (cdxl_read_frame(&io, &ix, i, frame, &fr)) {
                free(frame);
                break;
            }
            if (!(pix = malloc((size_t)fr.width * fr.height * 3))) {
                free(frame);
                break;
            }
            cdxl_show(frame, &fr, pix);
            fwrite(pix, 1, (size_t)fr.width * fr.height * 3, rgb);
            if (fr.layout == CDXL_CHUNKY) {          /* the datatype's 256 colours: every index in the cube */
                unsigned k;
                cdxl_rgb_to_cube(frame, &fr, pix);
                for (k = 0; k < (unsigned)fr.width * fr.height; k++)
                    if (pix[k] >= 216)
                        return 2;
            }
            if (fr.audio) {
                int8_t *mono = malloc(fr.audio);
                if (fr.audio > ix.max_audio)
                    return 3;                        /* the sound ring would be too small */
                if (mono) {
                    cdxl_mono(frame, &fr, mono);
                    fwrite(mono, 1, fr.audio, pcm);
                    free(mono);
                }
            }
            free(pix);
            free(frame);
            frames++;
        }
        fprintf(iolog, " %lu reads and %lu seeks for %u frames in order\n", m.reads, m.seeks, frames);
        (void)open_seeks;
        /* backwards: a seek and a read a frame, and the same frames */
        m.reads = m.seeks = 0;
        if (frames > 1 && (frame = malloc(ix.max_size)) != NULL) {
            for (i = frames; i-- > 0;)
                if (cdxl_read_frame(&io, &ix, i, frame, &fr))
                    return 4;
            free(frame);
            fprintf(iolog, "%s: backwards %lu reads, %lu seeks\n", name, m.reads, m.seeks);
        }
        cdxl_index_free(&io, &ix);
    }
    fclose(m.f);
    fclose(rgb);
    fclose(pcm);
    printf("%s %u %u %u %u %u\n", name, first.width, first.height, frames, frames ? cdxl_fps(&first) : 0,
           frames ? cdxl_rate(&first) : 0);
    return 0;
}

int main(int argc, char **argv)
{
    int i, bad = 0;
    char *log = getenv("CDXL_IOLOG");
    iolog = log ? fopen(log, "w") : NULL;
    if (!iolog)
        iolog = fopen("/dev/null", "w");

    for (i = 1; i + 1 < argc; i += 2)
        bad |= play(argv[i], argv[i + 1]);
    return bad;
}
