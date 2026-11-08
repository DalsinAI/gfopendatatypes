/* tests/test-pcm.sh's driver for pcm/pcmcore.c on x86 or ARM64 cores.
 *   pcmtest FILE EXPECT.s16 CHANNELS FRAMES RATE OUTDIR
 * Parses FILE, converts every frame, compares with EXPECT; then writes
 * AIFF and WAV, 16 and 8 bits, reads each back and compares again.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pcmcore.h"

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

struct mem { unsigned char *b; unsigned long n; };
static unsigned long rd(void *h, unsigned long off, void *buf, unsigned long n)
{
    struct mem *m = h;
    if (off >= m->n) return 0;
    if (n > m->n - off) n = m->n - off;
    memcpy(buf, m->b + off, n);
    return n;
}

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

/* parse and convert; the samples, or NULL */
static short *decode(struct mem *m, pcm_info *pi)
{
    short *s;
    if (pcm_parse(rd, m, m->n, pi)) return NULL;
    s = malloc((pi->frames * pi->channels + 1) * sizeof *s);
    pcm_to16(pi, m->b + pi->data, s, pi->frames);
    return s;
}

int main(int argc, char **argv)
{
    struct mem m;
    unsigned long en, i;
    short *exp, *got;
    pcm_info pi;
    int ch = atoi(argv[3]), c, bits;
    unsigned long frames = strtoul(argv[4], 0, 10), rate = strtoul(argv[5], 0, 10);

    m.b = load(argv[1], &m.n);
    exp = (short *)load(argv[2], &en);
    got = decode(&m, &pi);
    CHECK(got, "%s: not parsed", argv[1]);
    if (!got) { free(exp); free(m.b); return 1; }
    CHECK(pi.channels == ch && pi.frames == frames && pi.rate == rate, "%s: %d ch %lu frames %lu Hz",
          argv[1], pi.channels, pi.frames, pi.rate);
    for (i = 0; i < frames * ch; i++)
        if (got[i] != exp[i]) { CHECK(0, "%s: sample %lu is %d, not %d", argv[1], i, got[i], exp[i]); break; }

    for (c = PCM_AIFF; c <= PCM_WAVE; c++)
        for (bits = 8; bits <= 16; bits += 8) {
            unsigned char *f = malloc(PCM_HEADER_MAX + frames * ch * 2 + 1);
            unsigned long n = (unsigned long)pcm_header(c, ch, rate, frames, bits, f);
            short *back;
            pcm_info p2;
            struct mem m2;
            n += pcm_encode(c, ch, bits, got, frames, f + n);
            if (pcm_pad(ch, frames, bits)) f[n++] = 0;
            m2.b = f; m2.n = n;
            back = decode(&m2, &p2);
            CHECK(back && p2.channels == ch && p2.frames == frames && p2.rate == rate && p2.bits == bits,
                  "%s: written as %s %d-bit doesn't read back", argv[1], c == PCM_AIFF ? "AIFF" : "WAV", bits);
            if (back)
                for (i = 0; i < frames * ch; i++) {
                    short want = bits == 16 ? got[i] : (short)(got[i] & 0xff00);
                    if (back[i] != want) {
                        CHECK(0, "%s: %s %d-bit sample %lu is %d, not %d", argv[1],
                              c == PCM_AIFF ? "AIFF" : "WAV", bits, i, back[i], want);
                        break;
                    }
                }
            if (argv[6][0] && bits == 16) {
                char path[512];
                FILE *o;
                snprintf(path, sizeof path, "%s/%s.%s", argv[6], strrchr(argv[1], '/') ? strrchr(argv[1], '/') + 1 : argv[1],
                         c == PCM_AIFF ? "out.aiff" : "out.wav");
                if ((o = fopen(path, "wb"))) { fwrite(f, 1, n, o); fclose(o); }
            }
            free(back);
            free(f);
        }
    printf("%s %s\n", fails ? "FAIL" : "ok  ", argv[1]);
    free(got);
    free(exp);
    free(m.b);
    return fails != 0;
}
