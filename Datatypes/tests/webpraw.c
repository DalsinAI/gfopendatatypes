/* webp.datatype check: decode WebP files with libwebp alone (no datatypes)
 * and print the same ARGB checksum as tests/dtpic.c, on a PC or an Amiga.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include <stdlib.h>
#include "webp/decode.h"
#include "webp/demux.h"
int main(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb"); long n; unsigned char *buf, *argb; int w = 0, h = 0;
        unsigned long sum = 0, p; WebPBitstreamFeatures ft;
        if (!f) { printf("%s: cannot open\n", argv[i]); continue; }
        fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
        buf = malloc(n); if (fread(buf, 1, n, f) != (size_t)n) { fclose(f); continue; } fclose(f);
        if (WebPGetFeatures(buf, n, &ft) != VP8_STATUS_OK) { printf("%s: BAD\n", argv[i]); continue; }
        if (ft.has_animation) {
            WebPData d = { buf, (size_t)n }; WebPDemuxer *dm = WebPDemux(&d); WebPIterator it;
            argb = NULL;
            if (dm && WebPDemuxGetFrame(dm, 1, &it)) { argb = WebPDecodeARGB(it.fragment.bytes, it.fragment.size, &w, &h); WebPDemuxReleaseIterator(&it); }
            if (dm) WebPDemuxDelete(dm);
        } else
            argb = WebPDecodeARGB(buf, n, &w, &h);
        if (!argb) { printf("%s: DECODE_FAIL\n", argv[i]); continue; }
        for (p = 0; p < (unsigned long)w * h; p++) {
            unsigned char *q = argb + p * 4; unsigned long a = ft.has_alpha ? q[0] : 0xff;
            sum = (sum * 31 + ((a << 24) | ((unsigned long)q[1] << 16) | ((unsigned long)q[2] << 8) | q[3])) & 0xffffffffUL;
        }
        printf("%s %d %d alpha=%d %08lx\n", argv[i], w, h, ft.has_alpha ? 1 : 0, sum);
        WebPFree(argb); free(buf);
    }
    return 0;
}
