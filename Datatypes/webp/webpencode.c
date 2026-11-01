/*
 * webp.datatype's encoder (webpencode.h): libwebp's lossless encoder,
 * called as libwebp's WebPEncode() calls it for a lossless ARGB picture,
 * without the rest of WebPEncode(): that would pull in the lossy encoder,
 * and WebPConfigInit() and WebPValidateConfig() compare floats. The two
 * small calls the lossless encoder needs from webp_enc.c are here, doing
 * what libwebp's do.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team (this file; libwebp keeps its
 * licence).
 * SPDX-License-Identifier: MIT
 */
#include <string.h>
#include "webpencode.h"
#include "src/webp/encode.h"
#include "src/enc/vp8li_enc.h"

/* webp_enc.c's, as libwebp has them: the first error reported stays */
int WebPEncodingSetError(const WebPPicture* const pic, WebPEncodingError error)
{
    if (pic->error_code == VP8_ENC_OK)
        ((WebPPicture*)pic)->error_code = error;
    return 0;
}

int WebPReportProgress(const WebPPicture* const pic, int percent, int* const percent_store)
{
    if (percent_store != NULL && percent != *percent_store) {
        *percent_store = percent;
        if (pic->progress_hook && !pic->progress_hook(percent, pic))
            return WebPEncodingSetError(pic, VP8_ENC_ERROR_USER_ABORT);
    }
    return 1;
}

/* The three picture calls the lossless encoder makes, for an ARGB picture
 * (all this file hands it), written from encode.h's descriptions. libwebp
 * has them in picture_enc.c, picture_csp_enc.c and picture_rescale_enc.c,
 * which also hold the lossy encoder's conversions and would pull it in. */
static int NoWriter(const uint8_t *data, size_t size, const WebPPicture *pic)
{
    (void)data;
    (void)size;
    (void)pic;
    return 1;
}

int WebPPictureInitInternal(WebPPicture *picture, int version)
{
    if (WEBP_ABI_IS_INCOMPATIBLE(version, WEBP_ENCODER_ABI_VERSION))
        return 0;
    if (picture != NULL) {
        memset(picture, 0, sizeof *picture);
        picture->writer = NoWriter;
        picture->error_code = VP8_ENC_OK;
    }
    return 1;
}

int WebPPictureHasTransparency(const WebPPicture *picture)
{
    int x, y;
    if (picture == NULL || !picture->use_argb || picture->argb == NULL)
        return 0;
    for (y = 0; y < picture->height; y++) {
        const uint32_t *row = picture->argb + (size_t)y * picture->argb_stride;
        for (x = 0; x < picture->width; x++)
            if ((row[x] >> 24) != 0xFF)
                return 1;
    }
    return 0;
}

int WebPPictureView(const WebPPicture *src, int left, int top, int width, int height, WebPPicture *dst)
{
    if (src == NULL || dst == NULL || !src->use_argb || left < 0 || top < 0 || width <= 0 || height <= 0
        || left > src->width - width || top > src->height - height)
        return 0;
    if (src != dst) {
        *dst = *src;                      /* a view owns no memory of its own */
        dst->memory_ = NULL;
        dst->memory_argb_ = NULL;
    }
    dst->width = width;
    dst->height = height;
    dst->argb = src->argb + (size_t)top * src->argb_stride + left;
    dst->argb_stride = src->argb_stride;
    return 1;
}

struct sink {
    LONG (*put)(void *h, const UBYTE *p, ULONG n);
    void *h;
    LONG err;
};

static int writer(const uint8_t *data, size_t size, const WebPPicture *pic)
{
    struct sink *s = (struct sink *)pic->custom_ptr;
    if (s->err)
        return 0;
    if (size && (s->err = s->put(s->h, (const UBYTE *)data, (ULONG)size)) != 0)
        return 0;
    return 1;
}

LONG webp_encode_lossless(ULONG *argb, ULONG width, ULONG height, ULONG stride, LONG level,
                          LONG (*put)(void *h, const UBYTE *p, ULONG n), void *h)
{
    /* Effort: libwebp's method (0-6) and its quality (0-100, how hard each
     * method tries), both integers here. The default is a middle effort, so
     * a 68k saves in seconds, not minutes. */
    static const int kMethod[10] = { 0, 0, 1, 1, 2, 3, 4, 4, 5, 6 };
    /* floats as constants: loading one is a 32-bit move, no float maths */
    static const float kQuality[10] = { 0.f, 25.f, 25.f, 50.f, 50.f, 50.f, 75.f, 90.f, 90.f, 100.f };
    struct sink s;
    WebPConfig config;
    WebPPicture pic;
    int ok;

    if (!argb || !width || !height || width > WEBP_MAX_DIMENSION || height > WEBP_MAX_DIMENSION
        || stride < width)
        return WEBPE_ERR_ARGS;
    if (level < 0 || level > 9)
        level = 3;

    /* WebPConfigInit()'s defaults, as integers and flags, for a lossless picture */
    memset(&config, 0, sizeof config);
    config.lossless = 1;
    config.exact = 1;                     /* keep the colour under transparent pixels */
    config.method = kMethod[level];
    config.quality = kQuality[level];
    config.near_lossless = 100;           /* off */
    config.alpha_compression = 1;
    config.alpha_quality = 100;
    config.alpha_filtering = 1;
    config.filter_strength = 60;
    config.sns_strength = 50;
    config.filter_sharpness = 0;
    config.filter_type = 1;
    config.segments = 4;
    config.pass = 1;
    config.qmax = 100;
    config.image_hint = WEBP_HINT_DEFAULT;
    config.thread_level = 0;              /* one task: the datatype's caller */

    if (!WebPPictureInit(&pic))
        return WEBPE_ERR_ENCODE;
    pic.use_argb = 1;
    pic.width = (int)width;
    pic.height = (int)height;
    pic.argb = (uint32_t *)argb;
    pic.argb_stride = (int)stride;
    s.put = put;
    s.h = h;
    s.err = 0;
    pic.writer = writer;
    pic.custom_ptr = &s;

    ok = VP8LEncodeImage(&config, &pic);
    if (s.err)
        return s.err;
    if (!ok)
        return pic.error_code == VP8_ENC_ERROR_OUT_OF_MEMORY ? WEBPE_ERR_MEMORY : WEBPE_ERR_ENCODE;
    return 0;
}
