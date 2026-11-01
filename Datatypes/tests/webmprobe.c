/*
 * webm.datatype check: read a WebM file with the datatype's own reader,
 * decode every frame with libvpx and print a checksum of each picture, so
 * the Amiga's results can be compared with a PC's.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vpx/vpx_decoder.h"
#include "vpx/vp8dx.h"
#include "webm_demux.h"
#include "webm_dither.h"

static long readAt(void *handle, uint32_t offset, void *buffer, uint32_t length)
{
    FILE *f = handle;
    if (fseek(f, offset, SEEK_SET))
        return -1;
    return (long)fread(buffer, 1, length, f);
}

int main(int argc, char **argv)
{
    FILE *f;
    long size;
    WebMInfo info;
    vpx_codec_ctx_t codec;
    uint32_t i;
    int rc;

    int chunkyMode = argc > 2 && !strcmp(argv[1], "-chunky");
    WebMDitherTables tables;
    unsigned char *chunky = NULL;
    if (chunkyMode) {
        argv++;
        argc--;
    }
    if (argc < 2 || !(f = fopen(argv[1], "rb"))) {
        printf("usage: webmprobe [-chunky] file.webm\n");
        return 10;
    }
    webm_dither_init(&tables);
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    rc = webm_parse(readAt, f, (uint32_t)size, &info);
    if (rc) {
        printf("WEBM_FAIL %d\n", rc);
        return 20;
    }
    printf("WEBM %s %ux%u frames=%u duration=%ums default=%uns\n", info.codec == WEBM_CODEC_VP8 ? "VP8" : "VP9",
        (unsigned)info.width, (unsigned)info.height, (unsigned)info.frameCount, (unsigned)info.durationMs,
        (unsigned)info.frameDurationNs);
    if (vpx_codec_dec_init(&codec, info.codec == WEBM_CODEC_VP8 ? vpx_codec_vp8_dx() : vpx_codec_vp9_dx(), NULL, 0)) {
        printf("VPX_INIT_FAIL\n");
        return 20;
    }
    for (i = 0; i < info.frameCount; i++) {
        WebMFrame *fr = &info.frames[i];
        unsigned char *data = malloc(fr->size);
        vpx_codec_iter_t iter = NULL;
        vpx_image_t *img;
        unsigned long sum = 0;
        int shown = 0;
        if (!data || readAt(f, fr->offset, data, fr->size) != (long)fr->size) {
            printf("READ_FAIL %u\n", (unsigned)i);
            return 20;
        }
        if (vpx_codec_decode(&codec, data, fr->size, NULL, 0)) {
            printf("frame %u DECODE_FAIL %s\n", (unsigned)i, vpx_codec_error(&codec));
            free(data);
            continue;
        }
        while ((img = vpx_codec_get_frame(&codec, &iter))) {
            int plane, y, x;
            if (chunkyMode) {
                unsigned long p;
                if (!chunky)
                    chunky = calloc(info.width, info.height);
                webm_dither(&tables, img, chunky, (unsigned long)info.width * info.height, info.width, info.height,
                    info.width);
                for (p = 0; p < (unsigned long)info.width * info.height; p++)
                    sum = (sum * 31 + chunky[p]) & 0xffffffffUL;
                shown++;
                continue;
            }
            for (plane = 0; plane < 3; plane++) {
                int w = plane ? (img->d_w + 1) / 2 : img->d_w, h = plane ? (img->d_h + 1) / 2 : img->d_h;
                for (y = 0; y < h; y++) {
                    const unsigned char *row = img->planes[plane] + y * img->stride[plane];
                    for (x = 0; x < w; x++)
                        sum = (sum * 31 + row[x]) & 0xffffffffUL;
                }
            }
            shown++;
        }
        if (chunkyMode)
            printf("frame %u shown=%u sum=%08lx\n", (unsigned)i, (unsigned)i, sum);
        else
            printf("frame %u t=%ums key=%u bytes=%u shown=%d sum=%08lx\n", (unsigned)i, (unsigned)fr->timeMs,
                (unsigned)fr->keyFrame, (unsigned)fr->size, shown, sum);
        free(data);
    }
    vpx_codec_destroy(&codec);
    webm_free(&info);
    fclose(f);
    printf("WEBM_DONE\n");
    return 0;
}
