/* The MP4 writer's H.264 encoder (animation/mp4_h264.c) built on the host
 * (the plain C code, MINIH264_NO_SIMD) or for the m68k: encodes a raw
 * 4:2:0 file of W x H frames (W and H even, rows W apart) to an Annex B
 * stream, as adt_mp4_write encodes a film. test_opendatatypes_mp4write.sh
 * compares its output, byte for byte, with the m68k build's in the MP4s.
 *
 *     mp4h_host W H QP SPEED GOP IN.yuv OUT.264
 *
 * Copyright (c) 2026 AmiCradle Team. MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "animation/mp4_h264.h"

int main(int argc, char **argv)
{
    unsigned long w, h, pw, ph, n;
    unsigned char *mem, *frame, *y, *u, *v, *out;
    void *enc;
    FILE *in, *fo;
    unsigned long r;
    if (argc != 8) return 2;
    w = strtoul(argv[1], 0, 10);
    h = strtoul(argv[2], 0, 10);
    pw = MP4H_PAD(w);
    ph = MP4H_PAD(h);
    if (!(n = mp4h_size(w, h)) || !(mem = calloc(1, n))) return 3;
    if (!(enc = mp4h_init(mem, w, h, strtoul(argv[5], 0, 10)))) return 4;
    frame = malloc(w * h * 3 / 2);
    y = calloc(1, pw * ph * 3 / 2);
    u = y + pw * ph;
    v = u + pw * ph / 4;
    if (!(in = fopen(argv[6], "rb")) || !(fo = fopen(argv[7], "wb"))) return 5;
    while (fread(frame, 1, w * h * 3 / 2, in) == w * h * 3 / 2) {
        for (r = 0; r < h; r++) memcpy(y + r * pw, frame + r * w, w);
        for (r = 0; r < h / 2; r++) {
            memcpy(u + r * (pw / 2), frame + w * h + r * (w / 2), w / 2);
            memcpy(v + r * (pw / 2), frame + w * h * 5 / 4 + r * (w / 2), w / 2);
        }
        if (mp4h_encode(enc, y, u, v, strtoul(argv[3], 0, 10), strtoul(argv[4], 0, 10), &out, &n)) return 6;
        fwrite(out, 1, n, fo);
    }
    fclose(in);
    fclose(fo);
    return 0;
}
