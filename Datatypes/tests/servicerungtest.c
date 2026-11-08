/*
 * The service rung, pixel for pixel (tests/test-service-rung.sh): what
 * gfopenservice's media.decode/1 gives must be what the datatypes make on
 * the 68k, byte for byte.
 *
 * Built two ways from this one file:
 *   -DREF  the datatypes' own code: webm/webm_demux.c, libvpx and
 *          webm/webm_dither.h as webmclass.c drives them (its decodeRequest:
 *          on from the frame last decoded, afresh from a key frame when going
 *          back or past one, the picture kept between frames); and
 *          pngdecode.c, tgadecode.c, gifdecode.c and libwebp as the picture
 *          classes call them. Built for this computer, and for the 68040
 *          (m68k-linux-gnu-gcc) to run under qemu-m68k.
 *   -DSVC  the service: gfopenservice's host/libmediadecode.so, opened with
 *          dlopen, asked as the datatypes ask it: VOPEN, VINDEX and VFRAME in
 *          MD_PIX_WEBM with the next frame read ahead after each, as
 *          webm.datatype 47.2 does; NDECODE for the pictures.
 * Both print the same lines, a hash (FNV-1a 64) of every frame or picture,
 * so the script compares their output.
 *
 *   servicerungtest webm FILE ORDER     ORDER: "play" (every frame in turn)
 *                                       or "jump" (back, forward, past key
 *                                       frames, again)
 *   servicerungtest pic KIND MODE FRAME FILE...
 *                                       KIND png, tga, gif or webp; MODE for
 *                                       gif: 0 ARGB, 1 frame 0's indexes
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "webm_demux.h"

static int noHash;                         /* NOHASH set: decode only (for counting instructions) */

static unsigned long long fnv(const unsigned char *p, unsigned long n)
{
    unsigned long long h = 1469598103934665603ULL;
    if (noHash)
        return 0;
    while (n--) {
        h ^= *p++;
        h *= 1099511628211ULL;
    }
    return h;
}

static unsigned char *readFile(const char *path, unsigned long *size)
{
    FILE *f = fopen(path, "rb");
    unsigned char *p;
    long n;
    if (!f || fseek(f, 0, SEEK_END) || (n = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET) || !(p = malloc(n))
        || fread(p, 1, n, f) != (size_t)n) {
        fprintf(stderr, "cannot read %s\n", path);
        exit(2);
    }
    fclose(f);
    *size = (unsigned long)n;
    return p;
}

typedef struct {
    const unsigned char *data;
    uint32_t size;
} Mem;

static long memRead(void *handle, uint32_t offset, void *buffer, uint32_t length)
{
    Mem *m = handle;
    if (offset > m->size)
        return 0;
    if (length > m->size - offset)
        length = m->size - offset;
    memcpy(buffer, m->data + offset, length);
    return (long)length;
}

/* The frames asked for: in turn, or jumping about (the same both ways). */
static unsigned long order(const char *how, unsigned long frames, unsigned long *out)
{
    unsigned long n = 0, i;
    uint32_t seed = 12345;                       /* 32 bits on every machine */
    if (!strcmp(how, "play")) {
        for (i = 0; i < frames; i++)
            out[n++] = i;
        return n;
    }
    for (i = 0; i < frames && i < 12; i++)
        out[n++] = i;
    out[n++] = frames - 1;
    out[n++] = 0;
    out[n++] = frames / 2;
    out[n++] = frames / 2;
    out[n++] = frames / 2 + 1;
    out[n++] = frames / 3;
    for (i = 0; i < 40; i++) {
        seed = seed * 1103515245u + 12345u;
        out[n++] = (seed >> 8) % frames;
    }
    out[n++] = frames + 5;                       /* past the end: the last */
    return n;
}

/* ---- the datatypes' own code ------------------------------------------- */
#ifdef REF

#include "vpx/vpx_decoder.h"
#include "vpx/vp8dx.h"
#include "webm_dither.h"
#include "pngdecode.h"
#include "tgadecode.h"
#include "gifdecode.h"
#include "webp/decode.h"
#include "webp/demux.h"

/* webmclass.c's state and its decodeOne / decodeRequest. */
static WebMInfo info;
static Mem mem;
static vpx_codec_ctx_t codec;
static int codecReady;
static long lastDecoded = -1;
static unsigned char *chunky;
static WebMDitherTables tables;

static int openDecoder(void)
{
    if (codecReady)
        vpx_codec_destroy(&codec);
    codecReady = 0;
    lastDecoded = -1;
    if (vpx_codec_dec_init(&codec, info.codec == WEBM_CODEC_VP8 ? vpx_codec_vp8_dx() : vpx_codec_vp9_dx(), NULL, 0))
        return 0;
    codecReady = 1;
    return 1;
}

static int decodeOne(unsigned long index)
{
    WebMFrame *frame = &info.frames[index];
    vpx_codec_iter_t iter = NULL;
    vpx_image_t *img, *last = NULL;
    if (vpx_codec_decode(&codec, mem.data + frame->offset, frame->size, NULL, 0)) {
        const char *detail = vpx_codec_error_detail(&codec);
        fprintf(stderr, "frame %lu: %s%s%s\n", index, vpx_codec_error(&codec), detail ? ": " : "", detail ? detail : "");
        return 0;
    }
    while ((img = vpx_codec_get_frame(&codec, &iter)))
        last = img;
    if (last)
        webm_dither(&tables, last, chunky, info.width * info.height, info.width, info.height, info.width);
    lastDecoded = (long)index;
    return 1;
}

static int loadFrame(unsigned long want)
{
    long start, i;
    if (want >= info.frameCount)
        want = info.frameCount - 1;
    if (codecReady && lastDecoded == (long)want)
        return 1;
    start = (long)want;
    while (start > 0 && !info.frames[start].keyFrame)
        start--;
    if (!codecReady || lastDecoded < 0 || (long)want <= lastDecoded || start > lastDecoded) {
        if (!openDecoder())
            return 0;
    } else
        start = lastDecoded + 1;
    for (i = start; i <= (long)want; i++)
        if (!decodeOne((unsigned long)i))
            return 0;
    return 1;
}

static int webm(const char *path, const char *how)
{
    unsigned long size, n, k, *list;
    mem.data = readFile(path, &size);
    mem.size = (uint32_t)size;
    if (webm_parse(memRead, &mem, mem.size, &info)) {
        printf("not read\n");
        return 1;
    }
    webm_dither_init(&tables);
    chunky = calloc(1, (size_t)info.width * info.height);
    list = malloc((info.frameCount + 64) * sizeof *list);
    printf("%s: %lu frames, %lux%lu\n", path, (unsigned long)info.frameCount, (unsigned long)info.width,
           (unsigned long)info.height);
    n = order(how, info.frameCount, list);
    for (k = 0; k < n; k++) {
        unsigned long f = list[k] < info.frameCount ? list[k] : info.frameCount - 1;
        if (!loadFrame(f))
            printf("frame %lu failed\n", f);
        else
            printf("frame %lu %016llx\n", f, fnv(chunky, (unsigned long)info.width * info.height));
    }
    return 0;
}

/* 0xAARRGGBB words, as the 68k's memory holds them. */
static void bigEndian(ULONG *w, unsigned long n)
{
    unsigned long i;
    for (i = 0; i < n; i++) {
        ULONG v = w[i];
        unsigned char *b = (unsigned char *)&w[i];
        b[0] = (unsigned char)(v >> 24); b[1] = (unsigned char)(v >> 16); b[2] = (unsigned char)(v >> 8); b[3] = (unsigned char)v;
    }
}

static void picture(const char *path, const char *kind, unsigned long mode, unsigned long which)
{
    unsigned long size;
    unsigned char *d = readFile(path, &size);
    const char *name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
    unsigned long w = 0, h = 0, flags = 0, more = 0, bytes = 0;
    unsigned char *px = NULL;
    int ok = 0;

    if (!strcmp(kind, "png")) {
        struct png_head *hd = malloc(sizeof *hd);
        if (!png_read_head(d, size, hd)) {
            unsigned char *idat = malloc(hd->idat_len + 1), *raw = malloc(hd->raw_len + 1);
            void *work = malloc(PNG_WORKSIZE);
            px = malloc((size_t)hd->width * hd->height * 4);
            png_gather(d, size, idat);
            if (!png_decode(hd, idat, raw, (ULONG *)px, work)) {
                w = hd->width; h = hd->height; flags = hd->alpha; bytes = w * h * 4; ok = 1;
                bigEndian((ULONG *)px, w * h);
            }
            free(idat); free(raw); free(work);
        }
        free(hd);
    } else if (!strcmp(kind, "tga")) {
        struct tga_head hd;
        if (!tga_read_head(d, size, &hd)) {
            ULONG *pal = hd.type == 1 ? malloc((size_t)hd.map_len * 4 + 4) : NULL;
            UBYTE alpha = 0;
            px = malloc((size_t)hd.width * hd.height * 4 + 4);
            if (!tga_decode(&hd, d, size, (ULONG *)px, pal, &alpha)) {
                w = hd.width; h = hd.height; flags = alpha ? 1 : 0; bytes = w * h * 4; ok = 1;
                bigEndian((ULONG *)px, w * h);
            }
            free(pal);
        }
    } else if (!strcmp(kind, "gif")) {
        struct gif_info g;
        LONG count = gif_scan(d, size, &g, NULL, 0);
        if (count >= 1) {
            struct gif_frame *frames = malloc(sizeof *frames * (size_t)count);
            struct gif_canvas c;
            memset(&c, 0, sizeof c);
            c.work = malloc(GIF_WORKSIZE);
            gif_scan(d, size, &g, frames, (ULONG)count);
            w = g.width; h = g.height;
            if (mode == 1) {
                ULONG holes = 0;
                if (frames[0].colours) {
                    px = malloc((size_t)w * h);
                    gif_frame_lut8(d, size, &g, &frames[0], px, c.work,
                                   frames[0].transparent ? frames[0].trans : 0, &holes);
                    more = holes; bytes = w * h; ok = 1;
                }
            } else {
                unsigned long at = which < g.frames ? which : g.frames - 1;   /* past the end: the last */
                c.argb = malloc((size_t)w * h * 4 + 4);
                c.saved = g.restores ? malloc((size_t)w * h * 4 + 4) : NULL;
                c.shown = -1;
                gif_show(d, size, &g, frames, &c, at);
                flags = gif_has_alpha(&g, &c) ? 1 : 0;
                px = (unsigned char *)c.argb;
                bigEndian(c.argb, w * h);
                bytes = w * h * 4; ok = 1;
                free(c.saved);
            }
            free(c.work);
            free(frames);
        }
    } else if (!strcmp(kind, "webp")) {
        WebPBitstreamFeatures f;
        if (WebPGetFeatures(d, size, &f) == VP8_STATUS_OK && f.width > 0 && f.height > 0) {
            int fw = 0, fh = 0;
            w = f.width; h = f.height; flags = f.has_alpha ? 1 : 0;
            if (f.has_animation) {
                WebPData data = { d, size };
                WebPDemuxer *demux = WebPDemux(&data);
                WebPIterator it;
                if (demux && WebPDemuxGetFrame(demux, 1, &it)) {
                    px = WebPDecodeARGB(it.fragment.bytes, it.fragment.size, &fw, &fh);
                    more = (unsigned long)it.x_offset << 16 | (unsigned long)it.y_offset;
                    WebPDemuxReleaseIterator(&it);
                }
                WebPDemuxDelete(demux);
            } else
                px = WebPDecodeARGB(d, size, &fw, &fh);
            if (px) {
                bytes = (unsigned long)fw * fh * 4;
                ok = 1;
            }
        }
    }
    if (ok)
        printf("%s %s %lu %lux%lu flags %lu more %lx %016llx\n", kind, name, which, w, h, flags, more, fnv(px, bytes));
    else
        printf("%s %s %lu refused\n", kind, name, which);
    free(d);
}

#endif

/* ---- the service ------------------------------------------------------- */
#ifdef SVC

#include <dlfcn.h>

struct md_buffer {
    const uint8_t *in;
    uint8_t *out;
    uint32_t length;
    uint32_t written;
};
typedef int (*md_call_fn)(uint16_t op, uint32_t arg, const uint32_t extra[4], struct md_buffer buf[4],
                          uint32_t *result, uint32_t *aux);
static md_call_fn md_call;
static uint32_t lastWritten;               /* buf1's bytes written by the last call */

static int call(uint16_t op, uint32_t arg, uint32_t e0, uint32_t e1, uint32_t e2, uint32_t e3,
                const void *in, uint32_t inLen, void *out, uint32_t outLen, void *out2, uint32_t out2Len,
                uint32_t *result, uint32_t *aux)
{
    struct md_buffer b[4];
    uint32_t extra[4] = { e0, e1, e2, e3 }, r = 0, a = 0;
    int st;
    memset(b, 0, sizeof b);
    b[0].in = in;
    b[0].length = inLen;
    b[1].out = out;
    b[1].length = outLen;
    b[2].out = out2;
    b[2].length = out2Len;
    st = md_call(op, arg, extra, b, &r, &a);
    lastWritten = b[1].written;
    if (result)
        *result = r;
    if (aux)
        *aux = a;
    return st;
}

static int webm(const char *path, const char *how)
{
    WebMInfo info;
    Mem mem;
    unsigned long size, n, k, *list, pixels;
    uint32_t handle, fps, count, wh, got;
    unsigned char vinfo[24], *frame, *ahead;
    long aheadIndex = -1;
    int st;

    mem.data = readFile(path, &size);
    mem.size = (uint32_t)size;
    if (webm_parse(memRead, &mem, mem.size, &info)) {
        printf("not read\n");
        return 1;
    }
    if ((st = call(3, 0, 16, 16, 0, 0, mem.data, mem.size, vinfo, 24, NULL, 0, &handle, &fps))) {
        printf("VOPEN %d\n", st);
        return 1;
    }
    if ((st = call(32, handle, 0, 0, 0, 0, NULL, 0, NULL, 0, NULL, 0, &count, &wh)) || count != info.frameCount
        || wh != (info.width << 16 | info.height)) {
        printf("VINDEX %d: %lu frames %lux%lu, the reader %lu frames %lux%lu\n", st, (unsigned long)count,
               (unsigned long)(wh >> 16), (unsigned long)(wh & 0xffff), (unsigned long)info.frameCount,
               (unsigned long)info.width, (unsigned long)info.height);
        return 1;
    }
    pixels = (unsigned long)info.width * info.height;
    frame = malloc(pixels);
    ahead = malloc(pixels);
    list = malloc((info.frameCount + 64) * sizeof *list);
    printf("%s: %lu frames, %lux%lu\n", path, (unsigned long)info.frameCount, (unsigned long)info.width,
           (unsigned long)info.height);
    n = order(how, info.frameCount, list);
    for (k = 0; k < n; k++) {
        unsigned long f = list[k] < info.frameCount ? list[k] : info.frameCount - 1;
        /* webm.datatype's serviceFrame: the read-ahead when it is this frame */
        if (aheadIndex == (long)f) {
            unsigned char *t = frame;
            frame = ahead;
            ahead = t;
            st = 0;
            got = f;
        } else
            st = call(4, handle, f, 3, info.width, info.height, NULL, 0, frame, pixels, NULL, 0, &got, NULL);
        if (st || got != f)
            printf("frame %lu failed\n", f);
        else
            printf("frame %lu %016llx\n", f, fnv(frame, pixels));
        aheadIndex = -1;
        if (f + 1 < info.frameCount
            && !call(4, handle, f + 1, 3, info.width, info.height, NULL, 0, ahead, pixels, NULL, 0, &got, NULL))
            aheadIndex = (long)(f + 1);
    }
    call(5, handle, 0, 0, 0, 0, NULL, 0, NULL, 0, NULL, 0, NULL, NULL);
    return 0;
}

static void picture(const char *path, const char *kind, unsigned long mode, unsigned long which)
{
    unsigned long size;
    unsigned char *d = readFile(path, &size), info[16];
    const char *name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
    uint32_t fourcc = !strcmp(kind, "png") ? 0x504e4720u : !strcmp(kind, "tga") ? 0x54474120u
                    : !strcmp(kind, "gif") ? 0x47494620u : 0x57454250u, w = 0, h = 0;
    uint32_t room = 64u << 20;
    unsigned char *px = malloc(room);
    int st = call(33, (uint32_t)which, fourcc, (uint32_t)mode, 0, 0, d, (uint32_t)size, px, room, info, 16, &w, &h);
    if (st == 0) {
        unsigned long iw = (unsigned long)info[0] << 24 | info[1] << 16 | info[2] << 8 | info[3];
        unsigned long ih = (unsigned long)info[4] << 24 | info[5] << 16 | info[6] << 8 | info[7];
        unsigned long fl = (unsigned long)info[8] << 24 | info[9] << 16 | info[10] << 8 | info[11];
        unsigned long more = (unsigned long)info[12] << 24 | info[13] << 16 | info[14] << 8 | info[15];
        unsigned long bytes;
        if (!strcmp(kind, "gif") && mode == 1)
            bytes = iw * ih;
        else if (!strcmp(kind, "webp"))
            bytes = lastWritten;           /* the first frame's own size: what the service wrote */
        else
            bytes = iw * ih * 4;
        printf("%s %s %lu %lux%lu flags %lu more %lx %016llx\n", kind, name, which, iw, ih, fl & 1, more, fnv(px, bytes));
    } else
        printf("%s %s %lu refused\n", kind, name, which);
    free(px);
    free(d);
}

#endif

int main(int argc, char **argv)
{
    int a;
    noHash = getenv("NOHASH") != NULL;
#ifdef SVC
    const char *lib = getenv("MEDIADECODE");
    void *h = dlopen(lib ? lib : "libmediadecode.so", RTLD_NOW | RTLD_LOCAL);
    if (!h || !(md_call = (md_call_fn)dlsym(h, "md_call"))) {
        fprintf(stderr, "no media.decode/1 library: %s\n", dlerror());
        return 2;
    }
#endif
    if (argc >= 4 && !strcmp(argv[1], "webm"))
        return webm(argv[2], argv[3]);
    if (argc >= 6 && !strcmp(argv[1], "pic")) {
        for (a = 5; a < argc; a++)
            picture(argv[a], argv[2], strtoul(argv[3], NULL, 0), strtoul(argv[4], NULL, 0));
        return 0;
    }
    fprintf(stderr, "usage: servicerungtest webm FILE play|jump | pic KIND MODE FRAME FILE...\n");
    return 2;
}
