/* AnimationClassABITest: OS 3.2's animation.datatype and anim.datatype
 * next to GFOpenDatatypes' (gfopenanimation.datatype, gfopenanim.datatype), on
 * ANIMs the test makes itself, every answer printed side by side.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 *   AnimationClassABITest         each line where ours answers otherwise
 *   AnimationClassABITest PROBE   OS 3.2's answers only, for the README's
 *                                 "ABI test" rows
 *   AnimationClassABITest ALL     both, every line
 *
 * It builds only if every number and layout animation_core.h and
 * anim_core.h have is the NDK 3.2 header's. Its ANIMs go to T:.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <graphics/gfx.h>
#include <libraries/iffparse.h>
#include <intuition/intuition.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <intuition/gadgetclass.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <datatypes/soundclass.h>
#include <datatypes/animationclass.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/datatypes.h>
#include <clib/alib_protos.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "animation/animation_core.h"
#include "anim/anim_core.h"

/* ---- the cores' numbers against the NDK --------------------------------- */

#define SAME(a, b) _Static_assert((a) == (b), #a)
SAME(ADT_ADTA_Width, ADTA_Width); SAME(ADT_ADTA_Height, ADTA_Height); SAME(ADT_ADTA_Depth, ADTA_Depth);
SAME(ADT_ADTA_Frames, ADTA_Frames); SAME(ADT_ADTA_Frame, ADTA_Frame); SAME(ADT_ADTA_FramesPerSecond, ADTA_FramesPerSecond);
SAME(ADT_ADTA_FrameIncrement, ADTA_FrameIncrement); SAME(ADT_ADTA_PreloadFrameCount, ADTA_PreloadFrameCount);
SAME(ADT_ADTA_ModeID, ADTA_ModeID); SAME(ADT_ADTA_KeyFrame, ADTA_KeyFrame); SAME(ADT_ADTA_ColorRegisters, ADTA_ColorRegisters);
SAME(ADT_ADTA_CRegs, ADTA_CRegs); SAME(ADT_ADTA_GRegs, ADTA_GRegs); SAME(ADT_ADTA_ColorTable, ADTA_ColorTable);
SAME(ADT_ADTA_ColorTable2, ADTA_ColorTable2); SAME(ADT_ADTA_Allocated, ADTA_Allocated);
SAME(ADT_ADTA_NumColors, ADTA_NumColors); SAME(ADT_ADTA_NumAlloc, ADTA_NumAlloc); SAME(ADT_ADTA_Remap, ADTA_Remap);
SAME(ADT_ADTA_Screen, ADTA_Screen); SAME(ADT_ADTA_NumSparse, ADTA_NumSparse); SAME(ADT_ADTA_SparseTable, ADTA_SparseTable);
SAME(ADT_ADTA_Sample, ADTA_Sample); SAME(ADT_ADTA_SampleLength, ADTA_SampleLength); SAME(ADT_ADTA_Period, ADTA_Period);
SAME(ADT_ADTA_Volume, ADTA_Volume); SAME(ADT_ADTA_Cycles, ADTA_Cycles);
SAME(ADT_ADTA_LeftSample, ADTA_LeftSample); SAME(ADT_ADTA_RightSample, ADTA_RightSample);
SAME(ADT_ADTA_SamplesPerSec, ADTA_SamplesPerSec);
SAME(ADT_SDTA_SamplesPerSec, SDTA_SamplesPerSec); SAME(ADT_PDTA_NumColors, PDTA_NumColors);
SAME(ADT_ADTM_LOADFRAME, ADTM_LOADFRAME); SAME(ADT_ADTM_UNLOADFRAME, ADTM_UNLOADFRAME); SAME(ADT_ADTM_START, ADTM_START);
SAME(ADT_ADTM_PAUSE, ADTM_PAUSE); SAME(ADT_ADTM_STOP, ADTM_STOP); SAME(ADT_ADTM_LOCATE, ADTM_LOCATE);
SAME(ADT_ADTM_LOADNEWFORMATFRAME, ADTM_LOADNEWFORMATFRAME);
SAME(ADT_ADTM_UNLOADNEWFORMATFRAME, ADTM_UNLOADNEWFORMATFRAME);
SAME(sizeof(struct adt_Frame), sizeof(struct adtNewFormatFrame));
SAME(__builtin_offsetof(struct adt_Frame, alf_BitMap), __builtin_offsetof(struct adtFrame, alf_BitMap));
SAME(__builtin_offsetof(struct adt_Frame, alf_Sample), __builtin_offsetof(struct adtFrame, alf_Sample));
SAME(__builtin_offsetof(struct adt_Frame, alf_UserData), __builtin_offsetof(struct adtFrame, alf_UserData));
SAME(sizeof(struct adt_BitMap), sizeof(struct BitMap));
SAME(sizeof(struct adt_ColorRegister), sizeof(struct ColorRegister));
SAME(ANM_ANHD_SIZE, sizeof(struct AnimHeader));
SAME(ANM_ID_ANHD, ID_ANHD); SAME(ANM_ID_DLTA, ID_DLTA); SAME(ANM_ID_ANIM, ID_ANIM);

struct Library *DataTypesBase;

static int probe, all;
#define LINE 200
static char out[2][96][LINE];
static int nout[2];

static void say(int ours, const char *fmt, ...)
{
    va_list ap;
    if (nout[ours] >= 96) return;
    va_start(ap, fmt);
    vsnprintf(out[ours][nout[ours]++], LINE, fmt, ap);
    va_end(ap);
}

/* ---- ANIMs, made here ---------------------------------------------------- */

static UBYTE file[4096];
static ULONG flen;

static void put32(ULONG v) { file[flen++] = v >> 24; file[flen++] = v >> 16; file[flen++] = v >> 8; file[flen++] = v; }
static void put16(UWORD v) { file[flen++] = v >> 8; file[flen++] = v; }
static void putb(const void *d, ULONG n) { memcpy(file + flen, d, n); flen += n; }
static ULONG open_chunk(ULONG id) { put32(id); put32(0); return flen; }
static void close_chunk(ULONG at)
{
    ULONG n = flen - at;
    file[at - 4] = n >> 24; file[at - 3] = n >> 16; file[at - 2] = n >> 8; file[at - 1] = n;
    if (n & 1) file[flen++] = 0;
}

/* 16 x 4, 2 planes, 3 frames: frame 0 a BODY, frames 1 and 2 op 5
 * deltas (frame 2 in XOR mode when xor), interleave il, reltime rel */
static void make_anim(int xor, int il, ULONG rel, int camg)
{
    ULONG form, ilbm, c, f;
    static const UBYTE cmap[12] = { 0, 0, 0, 255, 255, 255, 255, 0, 0, 0, 0, 255 };
    flen = 0;
    form = open_chunk(ID_FORM);
    put32(ID_ANIM);
    ilbm = open_chunk(ID_FORM);
    put32(ID_ILBM);
    c = open_chunk(ID_BMHD);
    put16(16); put16(4); put16(0); put16(0);
    file[flen++] = 2; file[flen++] = 0; file[flen++] = 0; file[flen++] = 0;
    put16(0); file[flen++] = 10; file[flen++] = 11; put16(16); put16(4);
    close_chunk(c);
    if (camg) { c = open_chunk(ID_CAMG); put32(camg); close_chunk(c); }
    c = open_chunk(ID_CMAP); putb(cmap, 12); close_chunk(c);
    c = open_chunk(ID_BODY);
    { int y; for (y = 0; y < 4; y++) { put16(0xf0f0 >> y); put16(0x0ff0 << y); } }
    close_chunk(c);
    close_chunk(ilbm);
    for (f = 1; f < 3; f++) {
        ULONG d, i;
        ilbm = open_chunk(ID_FORM);
        put32(ID_ILBM);
        c = open_chunk(ID_ANHD);
        file[flen++] = 5; file[flen++] = 0;
        put16(16); put16(4); put16(0); put16(0); put32(0); put32(rel * f);
        file[flen++] = il; file[flen++] = 0;
        put32(f == 2 && xor ? 2 : 0);
        for (i = 0; i < 16; i++) file[flen++] = 0;
        close_chunk(c);
        c = open_chunk(ID_DLTA);
        d = flen;
        put32(64); for (i = 1; i < 16; i++) put32(0);           /* plane 0 only */
        /* column 0: uniq 4; column 1: skip 1, same 2 of 0x55 */
        file[flen++] = 1; file[flen++] = 0x84;
        for (i = 0; i < 4; i++) file[flen++] = (UBYTE)(0x11 * (i + f));
        file[flen++] = 2; file[flen++] = 1; file[flen++] = 0; file[flen++] = 2; file[flen++] = 0x55;
        (void)d;
        close_chunk(c);
        close_chunk(ilbm);
    }
    close_chunk(form);
}

static void bitmap_line(int ours, const char *what, ULONG f, struct BitMap *bm)
{
    char hx[LINE];
    ULONG x, y, p, k = 0;
    if (!bm) { say(ours, "%s frame %lu no bitmap", what, f); return; }
    for (y = 0; y < 4 && y < bm->Rows; y++)
        for (x = 0; x < 16; x++) {
            ULONG v = 0;
            for (p = 0; p < bm->Depth && p < 8; p++)
                if (bm->Planes[p][y * bm->BytesPerRow + (x >> 3)] & (0x80 >> (x & 7))) v |= 1 << p;
            k += sprintf(hx + k, "%lx", v);
        }
    say(ours, "%s frame %lu depth %u %s", what, f, bm->Depth, hx);
}

static void run(int ours, const char *what, int xor, int il, ULONG rel, int camg)
{
    BPTR fh, lock;
    struct DataType *dtn = NULL;
    Object *o;
    ULONG w = 0, h = 0, d = 0, n = 0, fps = 0, mode = 0, nc = 0, pre = 0, fr = 0, panel = 0, key = 0, f, r;
    struct ColorRegister *cr = NULL;
    make_anim(xor, il, rel, camg);
    if ((fh = Open((STRPTR) "T:odt_anim", MODE_NEWFILE)) != 0) { Write(fh, file, flen); Close(fh); }
    if ((lock = Lock((STRPTR) "T:odt_anim", ACCESS_READ)) != 0) {
        dtn = ObtainDataTypeA(DTST_FILE, (APTR)lock, NULL);
        UnLock(lock);
    }
    o = NewObject(NULL, (STRPTR)(ours ? "gfopenanim.datatype" : "anim.datatype"), DTA_Name, (ULONG) "T:odt_anim",
                  DTA_SourceType, DTST_FILE, DTA_DataType, (ULONG)dtn, GA_Width, 100, GA_Height, 60, TAG_DONE);
    say(ours, "%s new %s ioerr %ld", what, o ? "ok" : "fail", o ? 0L : (long)IoErr());
    if (!o) { if (dtn) ReleaseDataType(dtn); return; }
    GetDTAttrs(o, ADTA_Width, (ULONG)&w, ADTA_Height, (ULONG)&h, ADTA_Depth, (ULONG)&d, ADTA_Frames, (ULONG)&n,
               ADTA_FramesPerSecond, (ULONG)&fps, ADTA_ModeID, (ULONG)&mode, ADTA_NumColors, (ULONG)&nc,
               ADTA_ColorRegisters, (ULONG)&cr, TAG_DONE);
    say(ours, "%s size %lu x %lu x %lu frames %lu fps %lu mode %08lx colours %lu", what, w, h, d, n, fps, mode, nc);
    if (cr && nc) say(ours, "%s colour 1 %02x%02x%02x", what, cr[1].red, cr[1].green, cr[1].blue);
    GetDTAttrs(o, ADTA_PreloadFrameCount, (ULONG)&pre, ADTA_Frame, (ULONG)&fr, DTA_ControlPanel, (ULONG)&panel,
               ADTA_KeyFrame, (ULONG)&key, TAG_DONE);
    say(ours, "%s preload %lu frame %lu panel %lu keyframe %s", what, pre, fr, panel, key ? "yes" : "no"); /* ABI test */
    if (key) bitmap_line(ours, "key", 0, (struct BitMap *)key);
    for (f = 0; f < n + 1 && f < 4; f++) {
        struct adtFrame m;
        memset(&m, 0, sizeof m);
        m.MethodID = ADTM_LOADFRAME;
        m.alf_TimeStamp = f;
        m.alf_Frame = f;
        r = DoMethodA(o, (Msg)&m);
        say(ours, "%s load %lu -> %lu duration %lu cmap %s", what, f, r, m.alf_Duration, m.alf_CMap ? "yes" : "no");
        bitmap_line(ours, what, f, m.alf_BitMap);
        m.MethodID = ADTM_UNLOADFRAME;
        DoMethodA(o, (Msg)&m);
    }
    DisposeObject(o);
    if (dtn) ReleaseDataType(dtn);
}

int main(int argc, char **argv)
{
    struct Library *ours = NULL;
    static const struct { const char *what; int xor, il; ULONG rel; int camg; } cases[] = {
        { "store", 0, 0, 6, 0 }, { "xor", 1, 0, 6, 0 }, { "il1", 0, 1, 2, 0x8004 }, { "il2", 1, 2, 0, 0 },
    };
    int i, k;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "PROBE")) probe = 1;
        if (!strcmp(argv[i], "ALL")) all = 1;
    }
    if (!(DataTypesBase = OpenLibrary((STRPTR) "datatypes.library", 39))) { printf("no datatypes.library\n"); return 20; }
    if (!probe && !(ours = OpenLibrary((STRPTR) "datatypes/gfopenanim.datatype", 0))) {
        printf("no datatypes/gfopenanim.datatype (copy build-amiga's datatypes/ to LIBS:datatypes/)\n");
        return 20;
    }
    for (i = 0; i < (int)(sizeof cases / sizeof cases[0]); i++) {
        nout[0] = nout[1] = 0;
        run(0, cases[i].what, cases[i].xor, cases[i].il, cases[i].rel, cases[i].camg);
        if (!probe) run(1, cases[i].what, cases[i].xor, cases[i].il, cases[i].rel, cases[i].camg);
        for (k = 0; k < nout[0] || k < nout[1]; k++) {
            const char *a = k < nout[0] ? out[0][k] : "(nothing)", *b = k < nout[1] ? out[1][k] : "(nothing)";
            if (probe) printf("PROBE %s\n", a);
            else if (all || strcmp(a, b)) printf("OS 3.2: %s\nours:   %s\n", a, b);
        }
    }
    DeleteFile((STRPTR) "T:odt_anim");
    if (ours) CloseLibrary(ours);
    CloseLibrary(DataTypesBase);
    return 0;
}
