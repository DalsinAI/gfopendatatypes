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
#include "src/webp/demux.h"
#include "src/dsp/lossless.h"

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
    int w = 0, h = 0;
    uint8_t *px = NULL;
    if (WebPGetFeatures(data, size, &ft) != VP8_STATUS_OK)
        return 0;
    if (ft.has_animation) {
        WebPData d;
        WebPDemuxer *dm;
        WebPIterator it;
        d.bytes = data;
        d.size = size;
        if ((dm = WebPDemux(&d)) != NULL) {
            if (WebPDemuxGetFrame(dm, 1, &it)) {
                px = WebPDecodeARGB(it.fragment.bytes, it.fragment.size, &w, &h);
                WebPDemuxReleaseIterator(&it);
            }
            WebPDemuxDelete(dm);
        }
    } else
        px = WebPDecodeARGB(data, size, &w, &h);
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

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "init")) {
        const int old = argc >= 3 && !strcmp(argv[2], "old");
        return init_test(old, argc - 2 - old, argv + 2 + old);
    }
    fprintf(stderr, "usage: webpcoretest init [old] FILE...\n");
    return 2;
}
