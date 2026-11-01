/* TextClassABITest: OS 3.2's text.datatype and ascii.datatype next to
 * GFOpenDatatypes' (gfopentext.datatype, gfopenascii.datatype), on the same
 * texts, every answer printed side by side.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 *   TextClassABITest          each line where ours answers otherwise
 *   TextClassABITest PROBE    OS 3.2's answers only, for the README's
 *                             "ABI test" rows (paste the PROBE lines back)
 *   TextClassABITest ALL      both, every line
 *
 * It builds only if every number and layout text_core.h has is the NDK
 * 3.2 header's. It writes its texts to T: and reads the clipboard's unit 0
 * (what was there is put back).
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <intuition/gadgetclass.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/textclass.h>
#include <libraries/iffparse.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/datatypes.h>
#include <clib/alib_protos.h>
#include <proto/iffparse.h>
#include <proto/utility.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "text/text_core.h"
#include "ascii/ascii_core.h"

/* ---- text_core.h against the NDK -------------------------------------- */

#define SAME(a, b) _Static_assert((a) == (b), #a)
SAME(TDT_TDTA_Buffer, TDTA_Buffer); SAME(TDT_TDTA_BufferLen, TDTA_BufferLen); SAME(TDT_TDTA_LineList, TDTA_LineList);
SAME(TDT_TDTA_WordSelect, TDTA_WordSelect); SAME(TDT_TDTA_WordDelim, TDTA_WordDelim);
SAME(TDT_TDTA_WordWrap, TDTA_WordWrap);
SAME(sizeof(struct tdt_Line), sizeof(struct Line));
SAME(__builtin_offsetof(struct tdt_Line, ln_Text), __builtin_offsetof(struct Line, ln_Text));
SAME(__builtin_offsetof(struct tdt_Line, ln_Flags), __builtin_offsetof(struct Line, ln_Flags));
SAME(__builtin_offsetof(struct tdt_Line, ln_FgPen), __builtin_offsetof(struct Line, ln_FgPen));
SAME(__builtin_offsetof(struct tdt_Line, ln_Style), __builtin_offsetof(struct Line, ln_Style));
SAME(TDT_LNF_LF, LNF_LF); SAME(TDT_LNF_LINK, LNF_LINK); SAME(TDT_LNF_OBJECT, LNF_OBJECT);
SAME(TDT_LNF_SELECTED, LNF_SELECTED);
SAME(TDT_ID_FTXT, ID_FTXT); SAME(TDT_ID_CHRS, ID_CHRS);
SAME(ODT_DTA_TextAttr, DTA_TextAttr); SAME(ODT_DTA_Domain, DTA_Domain); SAME(ODT_DTM_SELECT, DTM_SELECT);
SAME(ODT_DTM_COPY, DTM_COPY); SAME(ODT_DTM_WRITE, DTM_WRITE); SAME(ODT_DTWM_RAW, DTWM_RAW);

struct Library *DataTypesBase, *IFFParseBase;

static int probe, all;
#define LINE 200
static char out[2][64][LINE];          /* [ours][line] */
static int nout[2];

static void say(int ours, const char *fmt, ...)
{
    va_list ap;
    if (nout[ours] >= 64) return;
    va_start(ap, fmt);
    vsnprintf(out[ours][nout[ours]++], LINE, fmt, ap);
    va_end(ap);
}

static void hexs(char *buf, ULONG size, const UBYTE *d, ULONG n)
{
    ULONG i, k = 0;
    for (i = 0; i < n && k + 3 < size; i++) k += sprintf(buf + k, "%02x", d[i]);
    buf[k] = 0;
}

static ULONG clip_read(UBYTE *buf, ULONG size)
{
    struct IFFHandle *iff = AllocIFF();
    ULONG n = 0;
    if (!iff) return 0;
    if ((iff->iff_Stream = (ULONG)OpenClipboard(0)) != 0) {
        InitIFFasClip(iff);
        if (OpenIFF(iff, IFFF_READ) == 0) {
            StopChunk(iff, ID_FTXT, ID_CHRS);
            while (ParseIFF(iff, IFFPARSE_SCAN) == 0) {
                LONG got = ReadChunkBytes(iff, buf + n, size - n);
                if (got > 0) n += got;
            }
            CloseIFF(iff);
        }
        CloseClipboard((struct ClipboardHandle *)iff->iff_Stream);
    }
    FreeIFF(iff);
    return n;
}

static ULONG file_read(const char *name, UBYTE *buf, ULONG size)
{
    BPTR fh = Open((STRPTR)name, MODE_OLDFILE);
    LONG n;
    if (!fh) return 0;
    n = Read(fh, buf, size);
    Close(fh);
    return n < 0 ? 0 : n;
}

/* One text through one class: base "ascii" (OS 3.2's) or "gfopenascii" */
static void run(int ours, const char *what, const UBYTE *text, ULONG len, ULONG wrap)
{
    static UBYTE buf[4096];
    char hx[LINE];
    BPTR fh = Open((STRPTR) "T:odt_text.txt", MODE_NEWFILE), lock;
    struct DataType *dtn = NULL;
    Object *o;
    ULONG v = 0, tv = 0, th = 0, vu = 0, hu = 0, vv = 0, n;
    STRPTR delim = NULL;
    struct List *lines = NULL;
    struct Node *nd;
    if (fh) { Write(fh, (APTR)text, len); Close(fh); }
    if ((lock = Lock((STRPTR) "T:odt_text.txt", ACCESS_READ)) != 0) {
        dtn = ObtainDataTypeA(DTST_FILE, (APTR)lock, NULL);
        UnLock(lock);
    }
    o = NewObject(NULL, (STRPTR)(ours ? "gfopenascii.datatype" : "ascii.datatype"), DTA_Name, (ULONG) "T:odt_text.txt",
                  DTA_SourceType, DTST_FILE, DTA_DataType, (ULONG)dtn, GA_Width, 160, GA_Height, 60,
                  TDTA_WordWrap, wrap, TAG_DONE);
    say(ours, "%s new %s ioerr %ld", what, o ? "ok" : "fail", o ? 0L : (long)IoErr());
    if (!o) { if (dtn) ReleaseDataType(dtn); return; }
    GetDTAttrs(o, TDTA_BufferLen, (ULONG)&v, TDTA_WordDelim, (ULONG)&delim, TAG_DONE);
    hexs(hx, sizeof hx, (const UBYTE *)(delim ? (char *)delim : ""), delim ? strlen((char *)delim) : 0);
    say(ours, "%s buflen %lu delim %s", what, v, hx);                          /* ABI test: the default delimiters */
    v = DoMethod(o, DTM_PROCLAYOUT, NULL, 1);
    GetDTAttrs(o, DTA_TotalVert, (ULONG)&tv, DTA_TotalHoriz, (ULONG)&th, DTA_VertUnit, (ULONG)&vu,
               DTA_HorizUnit, (ULONG)&hu, DTA_VisibleVert, (ULONG)&vv, TDTA_LineList, (ULONG)&lines, TAG_DONE);
    say(ours, "%s layout %lu total %lu x %lu units %lu x %lu visible %lu", what, v, tv, th, vu, hu, vv);
    n = 0;
    if (lines)
        for (nd = lines->lh_Head; nd->ln_Succ && n < 24; nd = nd->ln_Succ, n++) {
            struct Line *l = (struct Line *)nd;
            hexs(hx, 64, (const UBYTE *)l->ln_Text, l->ln_TextLen);
            say(ours, "%s line %lu x %u y %u w %u h %u f %u pen %d/%d style %lu %s", what, n, l->ln_XOffset,
                l->ln_YOffset, l->ln_Width, l->ln_Height, l->ln_Flags, l->ln_FgPen, l->ln_BgPen, l->ln_Style, hx);
        }
    v = DoMethod(o, DTM_COPY, NULL);
    n = clip_read(buf, sizeof buf);
    hexs(hx, sizeof hx, buf, n);
    say(ours, "%s copy %lu %s", what, v, hx);                                  /* ABI test: nothing selected copies all */
    if ((fh = Open((STRPTR) "T:odt_out", MODE_NEWFILE)) != 0) {
        v = DoMethod(o, DTM_WRITE, NULL, fh, DTWM_RAW, NULL);
        Close(fh);
        n = file_read("T:odt_out", buf, sizeof buf);
        hexs(hx, sizeof hx, buf, n);
        say(ours, "%s write raw %lu %s", what, v, hx);
    }
    if ((fh = Open((STRPTR) "T:odt_out", MODE_NEWFILE)) != 0) {
        v = DoMethod(o, DTM_WRITE, NULL, fh, DTWM_IFF, NULL);
        Close(fh);
        n = file_read("T:odt_out", buf, sizeof buf);
        hexs(hx, sizeof hx, buf, n);
        say(ours, "%s write iff %lu %s", what, v, hx);
    }
    DisposeObject(o);
    if (dtn) ReleaseDataType(dtn);
}

static const struct { const char *what; const char *text; ULONG wrap; } texts[] = {
    { "plain", "hello world\nsecond line\n", 0 },
    { "tabs", "a\tb\t\tc\nend", 0 },                                       /* ABI test: tab stops */
    { "crlf", "one\r\ntwo\rthree\n\nfive", 0 },                            /* ABI test: CR alone */
    { "ansi", "\033[1mbold\033[0m \033[31;42mred\033[0m \2334munder\23324m", 0 },  /* ABI test: escapes */
    { "wrap", "the quick brown fox jumps over the lazy dog and keeps on running far away", 1 },
    { "longword", "averyveryveryveryveryverylongwordwithoutanyspaces end", 1 },
    { "empty", "", 0 },
};

int main(int argc, char **argv)
{
    int i, k;
    struct Library *ours = NULL;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "PROBE")) probe = 1;
        if (!strcmp(argv[i], "ALL")) all = 1;
    }
    DataTypesBase = OpenLibrary((STRPTR) "datatypes.library", 39);
    IFFParseBase = OpenLibrary((STRPTR) "iffparse.library", 39);
    if (!DataTypesBase || !IFFParseBase) { printf("no datatypes.library or iffparse.library\n"); return 20; }
    if (!probe && !(ours = OpenLibrary((STRPTR) "datatypes/gfopenascii.datatype", 0))) {
        printf("no datatypes/gfopenascii.datatype (copy build-amiga's datatypes/ to LIBS:datatypes/)\n");
        return 20;
    }
    for (i = 0; i < (int)(sizeof texts / sizeof texts[0]); i++) {
        nout[0] = nout[1] = 0;
        run(0, texts[i].what, (const UBYTE *)texts[i].text, strlen(texts[i].text), texts[i].wrap);
        if (!probe) run(1, texts[i].what, (const UBYTE *)texts[i].text, strlen(texts[i].text), texts[i].wrap);
        for (k = 0; k < nout[0] || k < nout[1]; k++) {
            const char *a = k < nout[0] ? out[0][k] : "(nothing)", *b = k < nout[1] ? out[1][k] : "(nothing)";
            if (probe) printf("PROBE %s\n", a);
            else if (all || strcmp(a, b)) printf("OS 3.2: %s\nours:   %s\n", a, b);
        }
    }
    DeleteFile((STRPTR) "T:odt_text.txt");
    DeleteFile((STRPTR) "T:odt_out");
    if (ours) CloseLibrary(ours);
    CloseLibrary(IFFParseBase);
    CloseLibrary(DataTypesBase);
    return 0;
}
