/* GFOpenSound: an open sound.datatype for AmigaOS 3.2, API and ABI compatible.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * Clean-room: written from the autodocs, the NDK 3.2 headers and the IFF
 * specifications, never from Kickstart's, Hyperion's or AROS's code or
 * bytes. The class is sound_core.cpp (lean C++, no Amiga headers), tested
 * under qemu-m68k (tests/m68k/test_opendatatypes_picsound.sh); this file
 * is the class library around it, from ../picture/dtc_shell.h: the
 * RomTag, the vectors (sound.lvo: Open, Close, Expunge, Null, ObtainEngine
 * at -30), MakeClass and AddClass of the public class "sound.datatype" under
 * "datatypesclass", and the dispatcher's entry.
 *
 * LIBNAME is "sound.datatype" for the replacement and "gfopensound.datatype" for the
 * side-by-side build the ABI test opens next to OS 3.2's.
 */
#ifndef LIBNAME
#define LIBNAME "sound.datatype"
#endif
#define DTC_SUPER "datatypesclass"
#define DTC_DISPATCH snd_dispatch
#define DTC_INST_SIZE snd_inst_size
#define DTC_IDSTRING LIBNAME " 47.100 (13.10.2026) GFOpenSound, AmiCradle Team\r\n"
#define DTC_NEED_AUDIO

#include "sound_core.h"
#include "../picture/dtc_shell.h"

static const char ver[] __attribute__((used)) =
    "\0$VER: " LIBNAME " 47.100 (13.10.2026) GFOpenSound, AmiCradle Team";

DTC_ROMTAG;
