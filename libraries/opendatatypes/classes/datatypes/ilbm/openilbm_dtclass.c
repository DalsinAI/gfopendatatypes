/* GFOpenILBM: an open ilbm.datatype for AmigaOS 3.2, API and ABI compatible.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * Clean-room: written from the autodocs, the NDK 3.2 headers and the IFF
 * specifications, never from Kickstart's, Hyperion's or AROS's code or
 * bytes. The class is ilbm_core.cpp (lean C++, no Amiga headers), tested
 * under qemu-m68k (tests/m68k/test_opendatatypes_picsound.sh); this file
 * is the class library around it, from ../picture/dtc_shell.h: the
 * RomTag, the vectors (ilbm.lvo: Open, Close, Expunge, Null, ObtainEngine
 * at -30), MakeClass and AddClass of the public class "ilbm.datatype" under
 * "picture.datatype", and the dispatcher's entry.
 *
 * LIBNAME is "ilbm.datatype" for the replacement and "gfopenilbm.datatype" for the
 * side-by-side build the ABI test opens next to OS 3.2's.
 */
#ifndef LIBNAME
#define LIBNAME "ilbm.datatype"
#endif
#ifdef DTC_SIDE_BY_SIDE
#define DTC_SUPER "gfopenpicture.datatype"
#define DTC_SUPERLIB "datatypes/gfopenpicture.datatype"
#else
#define DTC_SUPER "picture.datatype"
#define DTC_SUPERLIB "datatypes/picture.datatype"
#endif
#define DTC_DISPATCH ilbm_dispatch
#define DTC_INST_SIZE ilbm_inst_size
#define DTC_IDSTRING LIBNAME " 47.101 (8.11.2026) GFOpenILBM, AmiCradle Team\r\n"
#define DTC_NEED_GFX

#include "ilbm_core.h"
#include "../picture/dtc_shell.h"

static const char ver[] __attribute__((used)) =
    "\0$VER: " LIBNAME " 47.101 (8.11.2026) GFOpenILBM, AmiCradle Team";

DTC_ROMTAG;
