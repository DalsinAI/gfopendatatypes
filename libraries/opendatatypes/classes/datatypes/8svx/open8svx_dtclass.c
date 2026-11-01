/* GFOpen8SVX: an open 8svx.datatype for AmigaOS 3.2, API and ABI compatible.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * Clean-room: written from the autodocs, the NDK 3.2 headers and the IFF
 * specifications, never from Kickstart's, Hyperion's or AROS's code or
 * bytes. The class is svx_core.cpp (lean C++, no Amiga headers), tested
 * under qemu-m68k (tests/m68k/test_opendatatypes_picsound.sh); this file
 * is the class library around it, from ../picture/dtc_shell.h: the
 * RomTag, the vectors (8svx.lvo: Open, Close, Expunge, Null, ObtainEngine
 * at -30), MakeClass and AddClass of the public class "8svx.datatype" under
 * "sound.datatype", and the dispatcher's entry.
 *
 * LIBNAME is "8svx.datatype" for the replacement and "gfopen8svx.datatype" for the
 * side-by-side build the ABI test opens next to OS 3.2's.
 */
#ifndef LIBNAME
#define LIBNAME "8svx.datatype"
#endif
#ifdef DTC_SIDE_BY_SIDE
#define DTC_SUPER "gfopensound.datatype"
#define DTC_SUPERLIB "datatypes/gfopensound.datatype"
#else
#define DTC_SUPER "sound.datatype"
#define DTC_SUPERLIB "datatypes/sound.datatype"
#endif
#define DTC_DISPATCH svx_dispatch
#define DTC_INST_SIZE svx_inst_size
#define DTC_IDSTRING LIBNAME " 47.100 (13.10.2026) GFOpen8SVX, AmiCradle Team\r\n"
#define DTC_NEED_AUDIO

#include "svx_core.h"
#include "../picture/dtc_shell.h"

static const char ver[] __attribute__((used)) =
    "\0$VER: " LIBNAME " 47.100 (13.10.2026) GFOpen8SVX, AmiCradle Team";

DTC_ROMTAG;
