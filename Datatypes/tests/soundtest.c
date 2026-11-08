/* tests/test-sound.sh's driver for opensound/snddec.c on x86 or ARM64 cores.
 *   soundtest FILE REF.s16 CHANNELS FRAMES RATE TOLERANCE   compare with libsndfile's decode
 *   soundtest -damage FILE                                   cut and changed copies: no crash
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snddec.h"

static unsigned char *load(const char *p, unsigned long *n)
{
    FILE *f = fopen(p, "rb");
    unsigned char *b;
    if (!f) { perror(p); exit(2); }
    fseek(f, 0, SEEK_END); *n = (unsigned long)ftell(f); rewind(f);
    b = malloc(*n + 1);
    if (fread(b, 1, *n, f) != *n) exit(2);
    fclose(f);
    return b;
}

/* everything it decodes, in pieces of 1000 frames; frames out */
static short *decodeAll(const unsigned char *d, unsigned long n, int *ch, unsigned long *rate, unsigned long *frames)
{
    unsigned long told, have = 0, cap = 4096, k;
    snddec *s = snddec_open(d, n, ch, rate, &told);
    short *pcm;
    if (!s) return NULL;
    pcm = malloc(cap * *ch * sizeof *pcm);
    for (;;) {
        if (have + 1000 > cap) { cap *= 2; pcm = realloc(pcm, cap * *ch * sizeof *pcm); }
        if (!(k = snddec_read(s, pcm + have * *ch, 1000))) break;
        have += k;
    }
    /* rewind and read a little again: the same first frames */
    if (have && !snddec_rewind(s)) {
        short again[64 * 2];
        k = snddec_read(s, again, 64);
        if (k && memcmp(again, pcm, k * *ch * sizeof *pcm)) { printf("FAIL rewind gives other samples\n"); exit(1); }
    }
    snddec_close(s);
    *frames = have;
    return pcm;
}

int main(int argc, char **argv)
{
    unsigned long n, rn, frames, rate, i, worst = 0;
    unsigned char *d;
    int ch;
    short *pcm, *ref;
    if (argc == 3 && !strcmp(argv[1], "-damage")) {
        unsigned long cut;
        d = load(argv[2], &n);
        for (cut = 0; cut < n; cut += n / 37 + 1) {      /* cut short */
            pcm = decodeAll(d, cut, &ch, &rate, &frames);
            free(pcm);
        }
        for (i = 0; i < 64; i++) {                       /* bytes changed */
            unsigned char *e = malloc(n);
            unsigned long j;
            memcpy(e, d, n);
            srand((unsigned)i);
            for (j = 0; j < 8; j++) e[(unsigned long)rand() % n] ^= (unsigned char)(1 + rand() % 255);
            pcm = decodeAll(e, n, &ch, &rate, &frames);
            free(pcm);
            free(e);
        }
        free(d);
        printf("ok   %s: damaged copies decode or fail cleanly\n", argv[2]);
        return 0;
    }
    d = load(argv[1], &n);
    ref = (short *)load(argv[2], &rn);
    pcm = decodeAll(d, n, &ch, &rate, &frames);
    if (!pcm) { printf("FAIL %s: not decoded\n", argv[1]); return 1; }
    if (ch != atoi(argv[3]) || rate != strtoul(argv[5], 0, 10) || frames != strtoul(argv[4], 0, 10)) {
        printf("FAIL %s: %d ch %lu Hz %lu frames, libsndfile %s ch %s Hz %s frames\n",
               argv[1], ch, rate, frames, argv[3], argv[5], argv[4]);
        return 1;
    }
    for (i = 0; i < frames * ch; i++) {
        long e = labs((long)pcm[i] - ref[i]);
        if ((unsigned long)e > worst) worst = (unsigned long)e;
    }
    if (worst > strtoul(argv[6], 0, 10)) {
        printf("FAIL %s: %lu LSB from libsndfile's decode (at most %s)\n", argv[1], worst, argv[6]);
        return 1;
    }
    printf("ok   %s: %d ch %lu Hz %lu frames, within %lu LSB of libsndfile's decode\n", argv[1], ch, rate, frames, worst);
    free(pcm); free(ref); free(d);
    return 0;
}
