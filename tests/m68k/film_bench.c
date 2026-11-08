/* The film path's hot loops, for counting 68k instructions under
 * qemu-m68k (tests/m68k/film_bench.sh): each workload runs `reps` times
 * on data made the same way every run, and its output's checksum is
 * printed, so two builds (the core at two commits) can be compared for
 * both speed and output.
 *   film_bench WORKLOAD REPS
 * Workloads: remap8 (320x256, 8 planes to 8 through a pen table), remap5
 * (5 planes to 8), remapid (8 to 8, the identity table), remap4of8 (4
 * planes to a 4-plane screen, identity: the class skips it now, the call
 * still copies), copyout (anim's frame into a BitMap, 320x256x5).
 * Copyright (c) 2026 AmiCradle Team. MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "animation/animation_core.h"
#include "anim/anim_core.h"

static unsigned long seed = 2026;
static odt_u32 rnd(void)
{
    seed = seed * 1103515245UL + 12345UL;
    return (odt_u32)(seed >> 16) & 0x7fff;
}

static odt_u32 sum_planes(const struct adt_BitMap *bm)
{
    odt_u32 s = 0, p, i;
    for (p = 0; p < bm->Depth && p < 8; p++)
        for (i = 0; i < (odt_u32)bm->BytesPerRow * bm->Rows; i++) s = s * 31 + bm->Planes[p][i];
    return s;
}

static void mk(struct adt_BitMap *bm, odt_u32 w, odt_u32 h, odt_u32 d, int fill)
{
    odt_u32 p, i, bpr = ((w + 15) >> 4) << 1;
    memset(bm, 0, sizeof *bm);
    bm->BytesPerRow = (odt_u16)bpr;
    bm->Rows = (odt_u16)h;
    bm->Depth = (odt_u8)d;
    for (p = 0; p < d; p++) {
        bm->Planes[p] = malloc(bpr * h);
        for (i = 0; i < bpr * h; i++) bm->Planes[p][i] = fill ? (odt_u8)rnd() : 0;
    }
}

int main(int argc, char **argv)
{
    const char *what = argc > 1 ? argv[1] : "remap8";
    long reps = argc > 2 ? atol(argv[2]) : 1, r;
    odt_u8 table[256];
    odt_u32 i;
    struct adt_BitMap s, d;
    for (i = 0; i < 256; i++) table[i] = (odt_u8)rnd();
    if (!strncmp(what, "remap", 5)) {
        odt_u32 sd = 8, dd = 8;
        const odt_u8 *t = table;
        if (!strcmp(what, "remap5")) sd = 5;
        if (!strcmp(what, "remapid") || !strcmp(what, "remap4of8")) {
            for (i = 0; i < 256; i++) table[i] = (odt_u8)i;
            if (what[5] == '4') sd = dd = 4;
        }
        mk(&s, 320, 256, sd, 1);
        mk(&d, 320, 256, dd, 0);
        for (r = 0; r < reps; r++) adt_remap(&s, 320, 256, t, &d);
        printf("%s %08lx\n", what, (unsigned long)sum_planes(&d));
        return 0;
    }
    if (!strcmp(what, "copyout")) {
        struct anm_info info;
        struct anm_dec dec;
        odt_u8 *buf;
        memset(&info, 0, sizeof info);
        memset(&dec, 0, sizeof dec);
        info.bmhd.w = 320;
        info.bmhd.h = 256;
        info.depth = 5;
        info.bpr = 40;
        dec.info = &info;
        dec.size = 5 * 256 * 40;
        buf = malloc(dec.size);
        for (i = 0; i < dec.size; i++) buf[i] = (odt_u8)rnd();
        dec.buf[0] = dec.buf[1] = buf;
        dec.cur = 0;
        mk(&d, 320, 256, 5, 0);
        for (r = 0; r < reps; r++) anm_copy_out(&dec, d.Planes, 5, d.BytesPerRow);
        printf("%s %08lx\n", what, (unsigned long)sum_planes(&d));
        return 0;
    }
    printf("unknown workload %s\n", what);
    return 2;
}
