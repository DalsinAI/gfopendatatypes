/*
 * webp.datatype's webp/webpcore.c on a PC or under qemu-m68k
 * (tests/test-webpcore.sh).
 *
 *   webpcoretest init [old] FILE...
 *       webp_core_init() (or, with "old", only what dt_init() did before
 *       47.4), then a copy of all of libwebp's writable data (the
 *       webp_data and webp_bss sections test-webpcore.sh makes of its
 *       .data and .bss); then each FILE decoded as the datatype decodes
 *       it, a picture encoded at three levels and decoded back: none of it
 *       may write libwebp's data, or two tasks decoding at once could meet
 *       half-made tables
 *   webpcoretest media DIR
 *       writes the WebP files the init test decodes: lossless, lossless
 *       with alpha, animated (opaque and with alpha), all made here with
 *       webp.datatype's encoder, and lossy, lossy with alpha and a lossy
 *       animation (webpcore-media.h); prints how many
 *   webpcoretest anim
 *       animated WebPs made here: an opaque one whose first frame doesn't
 *       cover the canvas comes on the ANIM background colour; one with
 *       alpha, and one whose frame covers the canvas, come as the frame
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "webpcore.h"
#include "webpencode.h"
#include "src/webp/decode.h"
#include "src/dsp/lossless.h"
#include "webpcore-media.h"

extern unsigned char __start_webp_data[], __stop_webp_data[], __start_webp_bss[], __stop_webp_bss[];

struct buf { unsigned char *p; ULONG n, cap; };
static LONG put(void *h, const UBYTE *p, ULONG n)
{
    struct buf *b = h;
    if (b->n + n > b->cap) {
        b->cap = (b->n + n) * 2 + 64;
        b->p = realloc(b->p, b->cap);
    }
    memcpy(b->p + b->n, p, n);
    b->n += n;
    return 0;
}

static unsigned char *readfile(const char *name, size_t *n)
{
    FILE *f = fopen(name, "rb");
    unsigned char *p;
    long len;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    p = malloc(len ? len : 1);
    if (fread(p, 1, len, f) != (size_t)len) {
        free(p);
        p = NULL;
    }
    fclose(f);
    *n = (size_t)len;
    return p;
}

/* As webp.datatype's loadWebP() decodes: 1 when it gave a picture. */
static int decode_as_datatype(const unsigned char *data, size_t size)
{
    WebPBitstreamFeatures ft;
    int x = 0, y = 0, w = 0, h = 0;
    uint8_t *px;
    if (WebPGetFeatures(data, size, &ft) != VP8_STATUS_OK)
        return 0;
    px = ft.has_animation ? webp_first_frame(data, size, ft.has_alpha, &x, &y, &w, &h)
                          : WebPDecodeARGB(data, size, &w, &h);
    WebPFree(px);
    return px != NULL;
}

static int init_test(int old, int nfiles, char **files)
{
    const size_t nd = (size_t)(__stop_webp_data - __start_webp_data), nb = (size_t)(__stop_webp_bss - __start_webp_bss);
    unsigned char *sd, *sb;
    ULONG px[24 * 16], i;
    int k, decoded = 0, changed = 0;
    static const LONG levels[] = { 0, -1, 9 };

    if (old) {                         /* dt_init() before 47.4 */
        WebPDecoderConfig config;
        VP8LEncDspInit();
        WebPInitDecoderConfig(&config);
    } else if (!webp_core_init()) {
        printf("FAIL webp_core_init() failed\n");
        return 1;
    }
    sd = malloc(nd + 1);
    sb = malloc(nb + 1);
    memcpy(sd, __start_webp_data, nd);
    memcpy(sb, __start_webp_bss, nb);

    for (k = 0; k < nfiles; k++) {
        size_t n;
        unsigned char *d = readfile(files[k], &n);
        if (d && decode_as_datatype(d, n))
            decoded++;
        else
            printf("FAIL %s: not decoded\n", files[k]);
        free(d);
    }
    for (i = 0; i < 24 * 16; i++)
        px[i] = 0xff000000UL | (i * 0x010305UL) | ((i & 7) ? 0 : 0x00800000UL);
    for (k = 0; k < 3; k++) {
        struct buf b = { 0, 0, 0 };
        ULONG work[24 * 16];
        memcpy(work, px, sizeof work);
        if (webp_encode_lossless(work, 24, 16, 24, levels[k], put, &b) == 0 && decode_as_datatype(b.p, b.n))
            decoded++;
        else
            printf("FAIL encode at level %ld\n", (long)levels[k]);
        free(b.p);
    }
    for (i = 0; i < nd; i++)
        changed += sd[i] != __start_webp_data[i];
    for (i = 0; i < nb; i++)
        changed += sb[i] != __start_webp_bss[i];
    printf("%s: %d pictures decoded and encoded; %d of libwebp's %lu bytes of data written after it\n",
           old ? "dt_init() before 47.4" : "webp_core_init()", decoded, changed, (unsigned long)(nd + nb));
    free(sd);
    free(sb);
    return decoded != nfiles + 3 ? 1 : changed ? 3 : 0;
}

/* --- animated WebPs, put together here from lossless frames ------------ */

static void le(unsigned char *p, ULONG v, int n)
{
    while (n--) {
        *p++ = (unsigned char)v;
        v >>= 8;
    }
}

/* A lossless WebP's VP8L chunk (header and payload) for w x h pixels. */
static struct buf vp8l_chunk(const ULONG *argb, ULONG w, ULONG h)
{
    struct buf b = { 0, 0, 0 }, c = { 0, 0, 0 };
    ULONG *work = malloc(w * h * 4), size;
    memcpy(work, argb, w * h * 4);
    webp_encode_lossless(work, w, h, w, 0, put, &b);
    free(work);
    /* RIFF xxxx WEBP VP8L size payload */
    size = b.p[16] | (b.p[17] << 8) | (b.p[18] << 16) | ((ULONG)b.p[19] << 24);
    put(&c, b.p + 12, 8 + size + (size & 1));
    free(b.p);
    return c;
}

/* An animation of one frame: canvas cw x ch, the frame w x h at (x, y). */
static struct buf animation(int alpha, ULONG bg, ULONG cw, ULONG ch, ULONG x, ULONG y,
                            const ULONG *argb, ULONG w, ULONG h)
{
    struct buf f = vp8l_chunk(argb, w, h), out = { 0, 0, 0 };
    unsigned char hd[12 + 18 + 14 + 24];
    memcpy(hd, "RIFF", 4);
    memcpy(hd + 8, "WEBP", 4);
    memcpy(hd + 12, "VP8X", 4);
    le(hd + 16, 10, 4);
    hd[20] = (unsigned char)(0x02 | (alpha ? 0x10 : 0));   /* animation, alpha */
    hd[21] = hd[22] = hd[23] = 0;
    le(hd + 24, cw - 1, 3);
    le(hd + 27, ch - 1, 3);
    memcpy(hd + 30, "ANIM", 4);
    le(hd + 34, 6, 4);
    le(hd + 38, bg, 4);                                  /* B, G, R, A */
    le(hd + 42, 0, 2);
    memcpy(hd + 44, "ANMF", 4);
    le(hd + 48, 16 + f.n, 4);
    le(hd + 52, x / 2, 3);
    le(hd + 55, y / 2, 3);
    le(hd + 58, w - 1, 3);
    le(hd + 61, h - 1, 3);
    le(hd + 64, 100, 3);
    hd[67] = 0;
    le(hd + 4, sizeof hd - 8 + f.n, 4);
    put(&out, hd, sizeof hd);
    put(&out, f.p, f.n);
    free(f.p);
    return out;
}

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)

static void anim_case(const char *name, int alpha, ULONG cw, ULONG ch, ULONG fx, ULONG fy, ULONG fw, ULONG fh)
{
    const ULONG bg = 0xc0336699UL;   /* A 0xc0 (ignored), R 0x33, G 0x66, B 0x99 */
    ULONG *frame = malloc(fw * fh * 4), i;
    struct buf a;
    int x = -1, y = -1, w = 0, h = 0;
    uint8_t *px;
    const int covers = fx == 0 && fy == 0 && fw == cw && fh == ch;

    for (i = 0; i < fw * fh; i++)
        frame[i] = (alpha ? ((i * 37) & 0xff) << 24 : 0xff000000UL) | (0x102030UL + i * 0x030507UL) % 0x1000000UL;
    a = animation(alpha, bg, cw, ch, fx, fy, frame, fw, fh);
    px = webp_first_frame(a.p, a.n, alpha, &x, &y, &w, &h);
    CHECK(px != NULL, "%s: no picture", name);
    if (!px)
        goto done;
    if (alpha || covers) {
        CHECK(x == (int)fx && y == (int)fy && w == (int)fw && h == (int)fh, "%s: %dx%d at (%d,%d), not the frame", name, w, h, x, y);
        for (i = 0; i < fw * fh && w == (int)fw && h == (int)fh; i++) {
            const ULONG v = ((ULONG)px[4 * i] << 24) | (px[4 * i + 1] << 16) | (px[4 * i + 2] << 8) | px[4 * i + 3];
            if (v != frame[i]) {
                CHECK(0, "%s: pixel %lu is %08lx, not %08lx", name, (unsigned long)i, (unsigned long)v, (unsigned long)frame[i]);
                break;
            }
        }
    } else {
        int bad = 0;
        ULONG cx, cy;
        CHECK(x == 0 && y == 0 && w == (int)cw && h == (int)ch, "%s: %dx%d at (%d,%d), not the %lux%lu canvas", name, w, h, x, y,
              (unsigned long)cw, (unsigned long)ch);
        for (cy = 0; cy < ch && w == (int)cw && h == (int)ch && !bad; cy++)
            for (cx = 0; cx < cw && !bad; cx++) {
                const uint8_t *q = px + 4 * (cy * cw + cx);
                const ULONG v = ((ULONG)q[0] << 24) | (q[1] << 16) | (q[2] << 8) | q[3];
                const int in = cx >= fx && cx < fx + fw && cy >= fy && cy < fy + fh;
                const ULONG want = in ? frame[(cy - fy) * fw + (cx - fx)] : 0xff336699UL;
                if (v != want) {
                    CHECK(0, "%s: (%lu,%lu) is %08lx, not %08lx", name, (unsigned long)cx, (unsigned long)cy,
                          (unsigned long)v, (unsigned long)want);
                    bad = 1;
                }
            }
        checks++;
    }
    WebPFree(px);
done:
    free(a.p);
    free(frame);
}

static int writefile(const char *dir, const char *name, const unsigned char *p, unsigned long n)
{
    char path[600];
    FILE *f;
    int ok;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (!(f = fopen(path, "wb")))
        return 0;
    ok = fwrite(p, 1, n, f) == n;
    return fclose(f) == 0 && ok;
}

static int media(const char *dir)
{
    ULONG px[24 * 16], ax[24 * 16], i;
    struct buf b = { 0, 0, 0 };
    int made = 0;
    unsigned k;

    webp_core_init();
    for (i = 0; i < 24 * 16; i++) {
        px[i] = 0xff000000UL | ((i % 24) * 10 << 16) | ((i / 24) * 15 << 8) | ((i * 7) & 255);
        ax[i] = (px[i] & 0x00ffffffUL) | (((i * 37) & 255) << 24);
    }
    {
        ULONG work[24 * 16];
        memcpy(work, px, sizeof work);
        if (webp_encode_lossless(work, 24, 16, 24, -1, put, &b) == 0)
            made += writefile(dir, "lossless.webp", b.p, b.n);
        free(b.p);
        b.p = NULL;
        b.n = b.cap = 0;
        memcpy(work, ax, sizeof work);
        if (webp_encode_lossless(work, 24, 16, 24, -1, put, &b) == 0)
            made += writefile(dir, "lossless-alpha.webp", b.p, b.n);
        free(b.p);
    }
    b = animation(0, 0xff102030UL, 40, 30, 6, 4, px, 24, 16);
    made += writefile(dir, "anim.webp", b.p, b.n);
    free(b.p);
    b = animation(1, 0x00000000UL, 40, 30, 6, 4, ax, 24, 16);
    made += writefile(dir, "anim-alpha.webp", b.p, b.n);
    free(b.p);
    for (k = 0; k < sizeof lossy_media / sizeof lossy_media[0]; k++)
        made += writefile(dir, lossy_media[k].name, lossy_media[k].data, lossy_media[k].size);
    printf("%d\n", made);
    return made == 7 ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "init")) {
        const int old = argc >= 3 && !strcmp(argv[2], "old");
        return init_test(old, argc - 2 - old, argv + 2 + old);
    }
    if (argc == 3 && !strcmp(argv[1], "media"))
        return media(argv[2]);
    if (argc == 2 && !strcmp(argv[1], "anim")) {
        webp_core_init();
        anim_case("opaque, frame inside the canvas", 0, 20, 12, 4, 2, 8, 6);
        anim_case("opaque, frame at the corner", 0, 31, 17, 0, 0, 30, 17);
        anim_case("opaque, frame at the far edge", 0, 64, 40, 34, 20, 30, 20);
        anim_case("opaque, frame covering the canvas", 0, 16, 9, 0, 0, 16, 9);
        anim_case("alpha, frame inside the canvas", 1, 20, 12, 4, 2, 8, 6);
        anim_case("alpha, frame covering the canvas", 1, 16, 9, 0, 0, 16, 9);
        printf("webpcoretest anim: %d checks, %d failed\n", checks, fails);
        return fails != 0;
    }
    fprintf(stderr, "usage: webpcoretest init [old] FILE... | media DIR | anim\n");
    return 2;
}
