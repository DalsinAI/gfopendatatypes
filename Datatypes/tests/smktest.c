/*
 * smktest: decodes a Smacker film with smacker.datatype's core
 * (smacker/smk_core.c), as tests/test-smacker.sh checks it against FFmpeg.
 *
 *   smktest FILM [VIDEO.rgb [AUDIO.s16]]
 *
 * Prints the header and one line a frame (palette changed, sound bytes, a
 * checksum of the pixels). VIDEO.rgb gets every stored frame as 24-bit RGB,
 * AUDIO.s16 the first playable track as signed 16-bit little-endian (8-bit
 * sound as (s - 128) * 256), which is what FFmpeg writes with
 * -f rawvideo -pix_fmt rgb24 and -f s16le.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smk_core.h"

static long fread_at(void *ctx, uint32_t at, void *buf, uint32_t n)
{
    FILE *f = ctx;
    if (fseek(f, (long)at, SEEK_SET))
        return -1;
    return (long)fread(buf, 1, n, f);
}

static void *halloc(void *ctx, uint32_t n)
{
    (void)ctx;
    return malloc(n ? n : 1);
}

static void hfree(void *ctx, void *p)
{
    (void)ctx;
    free(p);
}

int main(int argc, char **argv)
{
    struct smk_io io = { NULL, fread_at, halloc, hfree };
    struct smk s;
    FILE *in, *vout = NULL, *aout = NULL;
    uint8_t *rgb, *pcm = NULL;
    int track = -1, k, bad = 0;
    uint32_t i;

    if (argc < 2 || !(in = fopen(argv[1], "rb"))) {
        fprintf(stderr, "usage: smktest FILM [VIDEO.rgb [AUDIO.s16]]\n");
        return 2;
    }
    io.ctx = in;
    if (smk_open(&s, &io)) {
        printf("%s: not a film smk_core plays\n", argv[1]);
        smk_close(&s);
        return 1;
    }
    printf("SMK%u %ux%u frames=%u stored=%u flags=%u fps=%u (1/%u s x 100000)\n", s.version, s.width, s.height,
           s.frames, s.stored, s.flags, smk_fps(&s), s.fps_den);
    for (k = 0; k < SMK_TRACKS; k++)
        if (s.track[k].rate) {
            printf("track %d: %u Hz %u-bit %s %s max=%u%s\n", k, s.track[k].rate, s.track[k].bits,
                   s.track[k].channels == 2 ? "stereo" : "mono", s.track[k].packed ? "packed" : "pcm",
                   s.track[k].max_bytes, s.track[k].playable ? "" : " (not played)");
            if (track < 0 && s.track[k].playable)
                track = k;
        }
    if (argc > 2 && !(vout = fopen(argv[2], "wb")))
        return 2;
    if (argc > 3 && track >= 0 && !(aout = fopen(argv[3], "wb")))
        return 2;
    rgb = malloc(s.width * s.height * 3);
    if (track >= 0)
        pcm = malloc(s.track[track].max_bytes + 2);
    for (i = 0; i < s.stored; i++) {
        uint32_t bytes = 0, p, sum = 0;
        if (smk_next(&s, track, pcm, &bytes)) {
            printf("frame %u: FAILED\n", i);
            bad = 1;
            break;
        }
        for (p = 0; p < s.width * s.height; p++) {
            const uint8_t *c = s.palette + s.pixels[p] * 3;
            rgb[p * 3] = c[0];
            rgb[p * 3 + 1] = c[1];
            rgb[p * 3 + 2] = c[2];
            sum = sum * 31 + s.pixels[p];
        }
        printf("frame %u: key=%u pal=%u sound=%u sum=%08x\n", i, s.key[i], s.palette_changed, bytes, sum);
        if (vout)
            fwrite(rgb, 1, s.width * s.height * 3, vout);
        if (aout && bytes) {
            if (s.track[track].bits == 16) {
                const int16_t *a = (const int16_t *)pcm;
                for (p = 0; p < bytes / 2; p++) {
                    uint8_t le[2] = { (uint8_t)a[p], (uint8_t)((uint16_t)a[p] >> 8) };
                    fwrite(le, 1, 2, aout);
                }
            } else
                for (p = 0; p < bytes; p++) {
                    int v = (pcm[p] - 128) * 256;
                    uint8_t le[2] = { (uint8_t)v, (uint8_t)((unsigned)v >> 8) };
                    fwrite(le, 1, 2, aout);
                }
        }
    }
    if (vout)
        fclose(vout);
    if (aout)
        fclose(aout);
    free(rgb);
    free(pcm);
    smk_close(&s);
    fclose(in);
    return bad;
}
