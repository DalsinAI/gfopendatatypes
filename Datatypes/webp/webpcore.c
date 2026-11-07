/*
 * webp.datatype's libwebp side with no Amiga calls in it (webpcore.h).
 *
 * MIT, Copyright (c) 2026 AmiCradle Team (this file; libwebp keeps its
 * licence).
 * SPDX-License-Identifier: MIT
 */
#include "webpcore.h"
#include "src/webp/decode.h"
#include "src/dec/vp8_dec.h"
#include "src/dsp/dsp.h"
#include "src/dsp/lossless.h"

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
