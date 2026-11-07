/*
 * webp.datatype's libwebp side with no Amiga calls in it (webpcore.h).
 *
 * MIT, Copyright (c) 2026 AmiCradle Team (this file; libwebp keeps its
 * licence).
 * SPDX-License-Identifier: MIT
 */
#include "webpcore.h"
#include "src/webp/decode.h"
#include "src/webp/demux.h"
#include "src/dec/vp8_dec.h"
#include "src/dsp/dsp.h"
#include "src/dsp/lossless.h"
#include "src/utils/utils.h"

int webp_core_init(void)
{
    WebPDecoderConfig config;
    VP8Decoder *dec;

    /* libwebp's own calls of these return at once from then on. */
    VP8DspInit();                      /* lossy: transforms, filters, predictors */
    VP8LDspInit();                     /* lossless: predictors, colour transforms */
    VP8FiltersInit();                  /* alpha planes' unfilters */
    WebPInitAlphaProcessing();
    WebPInitSamplers();
    WebPInitUpsamplers();
    WebPInitYUV444Converters();
    WebPInitConvertARGBToYUV();
    WebPRescalerDspInit();
    VP8LEncDspInit();                  /* the lossless encoder's, for DTM_WRITE */
    /* The lossy decoder's coefficient reader is picked by VP8New(). */
    if ((dec = VP8New()) != NULL)
        VP8Delete(dec);
    return WebPInitDecoderConfig(&config);
}

uint8_t *webp_first_frame(const uint8_t *data, size_t size, int has_alpha,
                          int *x, int *y, int *w, int *h)
{
    WebPData webpData;
    WebPDemuxer *demux;
    WebPIterator iter;
    uint8_t *out = NULL;

    webpData.bytes = data;
    webpData.size = size;
    if (!(demux = WebPDemux(&webpData)))
        return NULL;
    if (WebPDemuxGetFrame(demux, 1, &iter)) {
        const int cw = (int)WebPDemuxGetI(demux, WEBP_FF_CANVAS_WIDTH);
        const int ch = (int)WebPDemuxGetI(demux, WEBP_FF_CANVAS_HEIGHT);
        if (has_alpha || (iter.x_offset == 0 && iter.y_offset == 0 && iter.width == cw && iter.height == ch)) {
            /* the frame alone, at its place */
            out = WebPDecodeARGB(iter.fragment.bytes, iter.fragment.size, w, h);
            *x = iter.x_offset;
            *y = iter.y_offset;
        } else {
            /* the canvas in the background colour (B, G, R, A in the file),
               the frame decoded into it */
            const uint32_t bg = WebPDemuxGetI(demux, WEBP_FF_BACKGROUND_COLOR);
            const size_t n = (size_t)cw * ch;
            uint8_t *canvas = (uint8_t *)WebPSafeMalloc(n, 4);
            if (canvas) {
                const size_t at = ((size_t)iter.y_offset * cw + iter.x_offset) * 4;
                size_t i;
                for (i = 0; i < n; i++) {
                    canvas[4 * i] = 0xff;
                    canvas[4 * i + 1] = (uint8_t)(bg >> 16);
                    canvas[4 * i + 2] = (uint8_t)(bg >> 8);
                    canvas[4 * i + 3] = (uint8_t)bg;
                }
                if (WebPDecodeARGBInto(iter.fragment.bytes, iter.fragment.size, canvas + at, n * 4 - at, cw * 4)) {
                    out = canvas;
                    *x = *y = 0;
                    *w = cw;
                    *h = ch;
                } else
                    WebPFree(canvas);
            }
        }
        WebPDemuxReleaseIterator(&iter);
    }
    WebPDemuxDelete(demux);
    return out;
}
