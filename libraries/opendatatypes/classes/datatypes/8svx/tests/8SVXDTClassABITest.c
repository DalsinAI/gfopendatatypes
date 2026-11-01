/* 8SVXDTClassABITest: OS 3.2's 8svx.datatype and GFOpen8SVX side by side.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * Writes 8SVX files made here (no media of anyone's) to DIR= (T:) and
 * loads each through OS 3.2's 8svx.datatype and through
 * gfopen8svx.datatype (with gfopensound.datatype under it), printing what
 * each answers: the error, the VoiceHeader, the length, rate, volume,
 * cycles and pan it set, and the first samples of each channel. The PROBE
 * lines answer what svx_core.cpp marks "ABI test".
 *   8SVXDTClassABITest [PROBE] [LIB=dir] [DIR=dir]
 */
#define DTC_TEST_CLASS "8svx.datatype"
#include "../../picture/tests/dtc_abitest.h"
#include <datatypes/soundclass.h>

#ifndef SDTA_SamplesPerSec
#define SDTA_SamplesPerSec (SDTA_Dummy + 11)
#define SDTA_LeftSample (SDTA_Dummy + 13)
#define SDTA_RightSample (SDTA_Dummy + 14)
#define SDTA_Pan (SDTA_Dummy + 15)
#endif

struct svx_spec {
    const char *name;
    ULONG n, oneshot, repeat;
    UWORD rate;
    UBYTE cmp;
    ULONG vol;
    LONG chan;                          /* -1: no CHAN */
    int no_vhdr, no_body;
};

static const struct svx_spec specs[] = {
    { "odt_s_plain.8svx", 50, 50, 0, 8363, 0, 0x10000, -1, 0, 0 },
    { "odt_s_odd.8svx", 51, 51, 0, 22050, 0, 0x8000, -1, 0, 0 },
    { "odt_s_stereo.8svx", 40, 40, 0, 8363, 0, 0x10000, 6, 0, 0 },
    { "odt_s_fib.8svx", 64, 64, 0, 8363, 1, 0x10000, -1, 0, 0 },
    { "odt_s_fibst.8svx", 64, 64, 0, 8363, 1, 0x10000, 6, 0, 0 },
    { "odt_s_octave.8svx", 30, 10, 6, 8363, 0, 0x10000, -1, 0, 0 },      /* first octave of a longer BODY */
    { "odt_s_long.8svx", 30, 100, 0, 8363, 0, 0x10000, -1, 0, 0 },       /* VHDR asks for more */
    { "odt_s_zero.8svx", 30, 0, 0, 8363, 0, 0x10000, -1, 0, 0 },         /* oneShot + repeat 0 */
    { "odt_s_left.8svx", 20, 20, 0, 8363, 0, 0x10000, 2, 0, 0 },
    { "odt_s_right.8svx", 20, 20, 0, 0, 0, 0x10000, 4, 0, 0 },           /* and a rate of 0 */
    { "odt_s_loud.8svx", 20, 20, 0, 8363, 0, 0x20000, -1, 0, 0 },        /* volume past 1.0 */
    { "odt_s_empty.8svx", 0, 0, 0, 8363, 0, 0x10000, -1, 0, 0 },
    { "odt_s_novhdr.8svx", 20, 20, 0, 8363, 0, 0x10000, -1, 1, 0 },
    { "odt_s_cmp2.8svx", 20, 20, 0, 8363, 2, 0x10000, -1, 0, 0 },
    { "odt_s_nobody.8svx", 20, 20, 0, 8363, 0, 0x10000, -1, 0, 1 },
};

static void make_8svx(const struct svx_spec *s)
{
    static struct iffw w;
    UBYTE vh[20], body[512], c[4];
    ULONG chans = s->chan == 6 ? 2 : 1, n = s->n * chans, i;
    iff_begin(&w, "8SVX");
    put32(vh, s->oneshot);
    put32(vh + 4, s->repeat);
    put32(vh + 8, 0);
    vh[12] = s->rate >> 8; vh[13] = s->rate;
    vh[14] = 1; vh[15] = s->cmp;
    put32(vh + 16, s->vol);
    if (!s->no_vhdr) iff_chunk(&w, "VHDR", vh, 20);
    if (s->chan >= 0) { put32(c, s->chan); iff_chunk(&w, "CHAN", c, 4); }
    for (i = 0; i < n && i < sizeof body; i++) body[i] = (UBYTE)(i * 29 + 3);
    if (!s->no_body) iff_chunk(&w, "BODY", body, n);
    iff_end(&w);
    write_file(s->name, w.buf, w.len);
}

static void dump(const char *cls, const struct svx_spec *s)
{
    Object *o = new_file_obj(cls, s->name, NULL);
    struct VoiceHeader *v;
    ULONG len, ch;
    if (!o) {
        probe("%s fail %ld", s->name, (long)IoErr());
        return;
    }
    v = (struct VoiceHeader *)get1(o, SDTA_VoiceHeader);
    len = get1(o, SDTA_SampleLength);
    out("%s vh %lu %lu %u %u %lu", s->name, v->vh_OneShotHiSamples, v->vh_RepeatHiSamples, v->vh_SamplesPerSec,
        v->vh_Compression, v->vh_Volume);
    probe("%s len %lu rate %lu period %lu volume %lu cycles %lu pan %lx", s->name, len, get1(o, SDTA_SamplesPerSec),
          get1(o, SDTA_Period), get1(o, SDTA_Volume), get1(o, SDTA_Cycles), get1(o, SDTA_Pan));
    for (ch = 0; ch < 2; ch++) {
        UBYTE *p = (UBYTE *)get1(o, ch ? SDTA_RightSample : SDTA_LeftSample);
        char line[80];
        ULONG i, k = 0;
        if (!p || p == (UBYTE *)0xfeedfaceUL) continue;
        for (i = 0; i < len && i < 32; i++) k += sprintf(line + k, "%02x", p[i]);
        line[k] = 0;
        probe("%s %s %s", s->name, ch ? "right" : "left", line);
    }
    DisposeObject(o);
}

static void run_set(const char *cls)
{
    ULONG i;
    for (i = 0; i < sizeof specs / sizeof specs[0]; i++) make_8svx(&specs[i]);
    for (i = 0; i < sizeof specs / sizeof specs[0]; i++) dump(cls, &specs[i]);
}

int main(int argc, char **argv) { return dtc_main(argc, argv); }
