/*
 * gfopensound-decoders.library: gfopensound.datatype's decoders on this CPU
 * (opensound/snddec.c: stb_vorbis, dr_mp3, dr_flac) as a plain library on
 * disk, for the datatype built without them (SNDDEC=library, the ROM's).
 * include/libraries/gfopensounddecoders.h has the calls.
 *
 * Bare: no start-up code; snddeclib's RomTag and a safe return lead the
 * hunk (common/dtstart.c is linked first). malloc and friends are
 * common/dtclib.c's, task-safe. libnix's soft floating point calls the ROM
 * IEEE maths libraries through the bases below, opened while the library
 * is loaded.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <dos/dos.h>
#include <proto/exec.h>

#include <libraries/gfopensounddecoders.h>
#include "snddec.h"

#ifndef REG
#define REG(r, x) x __asm(#r)
#endif

#define LIB_VERSION  47
#define LIB_REVISION 1

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
struct Library *MathIeeeDoubBasBase, *MathIeeeDoubTransBase, *MathIeeeSingBasBase;
static BPTR segList;

static const char libName[] = SNDDECNAME;
static const char libId[] = "gfopensound-decoders.library 47.1 (9.11.2026) AmiCradle Team, Ogg Vorbis (stb_vorbis), MP3 (dr_mp3), FLAC (dr_flac)";
static const char version[] __attribute__((used)) = "$VER: gfopensound-decoders.library 47.1 (9.11.2026)";

/* malloc, calloc, free, realloc, qsort, vsnprintf */
#include "dtclib.c"

static void closeAll(void)
{
    if (MathIeeeSingBasBase) CloseLibrary(MathIeeeSingBasBase);
    if (MathIeeeDoubTransBase) CloseLibrary(MathIeeeDoubTransBase);
    if (MathIeeeDoubBasBase) CloseLibrary(MathIeeeDoubBasBase);
    if (DOSBase) CloseLibrary((struct Library *)DOSBase);
    MathIeeeSingBasBase = MathIeeeDoubTransBase = MathIeeeDoubBasBase = NULL;
    DOSBase = NULL;
}

static struct Library *libInit(REG(d0, struct Library *lib), REG(a0, BPTR seg), REG(a6, struct ExecBase *sysBase))
{
    SysBase = sysBase;
    segList = seg;
    lib->lib_Node.ln_Type = NT_LIBRARY;
    lib->lib_Node.ln_Name = (char *)libName;
    lib->lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    lib->lib_Version = LIB_VERSION;
    lib->lib_Revision = LIB_REVISION;
    lib->lib_IdString = (APTR)libId;
    DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 37);
    MathIeeeDoubBasBase = OpenLibrary((CONST_STRPTR)"mathieeedoubbas.library", 34);
    MathIeeeDoubTransBase = OpenLibrary((CONST_STRPTR)"mathieeedoubtrans.library", 34);
    MathIeeeSingBasBase = OpenLibrary((CONST_STRPTR)"mathieeesingbas.library", 34);
    if (!DOSBase || !MathIeeeDoubBasBase || !MathIeeeDoubTransBase || !MathIeeeSingBasBase) {
        closeAll();
        FreeMem((UBYTE *)lib - lib->lib_NegSize, lib->lib_NegSize + lib->lib_PosSize);
        return NULL;
    }
    return lib;
}

static struct Library *libOpen(REG(a6, struct Library *lib))
{
    lib->lib_OpenCnt++;
    lib->lib_Flags &= ~LIBF_DELEXP;
    return lib;
}

static BPTR libExpunge(REG(a6, struct Library *lib))
{
    BPTR seg;
    if (lib->lib_OpenCnt) {
        lib->lib_Flags |= LIBF_DELEXP;
        return 0;
    }
    Remove(&lib->lib_Node);
    closeAll();
    seg = segList;
    FreeMem((UBYTE *)lib - lib->lib_NegSize, lib->lib_NegSize + lib->lib_PosSize);
    return seg;
}

static BPTR libClose(REG(a6, struct Library *lib))
{
    if (--lib->lib_OpenCnt == 0 && (lib->lib_Flags & LIBF_DELEXP))
        return libExpunge(lib);
    return 0;
}

static ULONG libNull(void)
{
    return 0;
}

/* --- the calls -------------------------------------------------------------- */

static ULONG sdKind(REG(a0, APTR data), REG(d0, ULONG size))
{
    return (ULONG)snddec_magic(data, size);
}

static APTR sdOpen(REG(a0, APTR data), REG(d0, ULONG size), REG(a1, struct SndDecInfo *info))
{
    int channels = 0;
    unsigned long rate = 0, frames = 0;
    snddec *d;

    if (!data || !info)
        return NULL;
    d = snddec_open(data, size, &channels, &rate, &frames);
    if (d) {
        info->sdi_Channels = channels;
        info->sdi_Rate = rate;
        info->sdi_Frames = frames;
    }
    return d;
}

static ULONG sdRead(REG(a0, APTR dec), REG(a1, WORD *out), REG(d0, ULONG frames))
{
    return dec && out ? snddec_read(dec, (short *)out, frames) : 0;
}

static LONG sdRewind(REG(a0, APTR dec))
{
    return dec ? snddec_rewind(dec) : -1;
}

static void sdClose(REG(a0, APTR dec))
{
    snddec_close(dec);
}

static const APTR funcTable[] = {
    (APTR)libOpen,
    (APTR)libClose,
    (APTR)libExpunge,
    (APTR)libNull,
    (APTR)sdKind,
    (APTR)sdOpen,
    (APTR)sdRead,
    (APTR)sdRewind,
    (APTR)sdClose,
    (APTR)-1
};

static const ULONG initTable[] = {
    sizeof(struct Library),
    (ULONG)funcTable,
    0,
    (ULONG)libInit
};

const struct Resident snddec_romtag __attribute__((used)) = {
    RTC_MATCHWORD,
    (struct Resident *)&snddec_romtag,
    (APTR)(&snddec_romtag + 1),
    RTF_AUTOINIT,
    LIB_VERSION,
    NT_LIBRARY,
    0,
    (char *)libName,
    (char *)libId,
    (APTR)initTable
};
