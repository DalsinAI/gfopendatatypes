/* GFOpenDatatypes: an open ascii.datatype for AmigaOS 3.2, API and ABI
 * compatible.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * Clean-room: written from the ascii_dtc and text_dtc autodocs and OS
 * 3.2's answers measured by tests/TextClassABITest, never from
 * Kickstart's, Hyperion's or AROS's code or bytes. The class is
 * ascii_core.c (plain C, no Amiga headers); this file is the class
 * library around it, from ../text/dtc_shell.h: "ascii.datatype" under
 * text.datatype, which it keeps open (datatypes/text.datatype).
 *
 * LIBNAME is "ascii.datatype" for the replacement and "gfopenascii.datatype"
 * (under "gfopentext.datatype") for the side-by-side build.
 */
#ifndef LIBNAME
#define LIBNAME "ascii.datatype"
#endif
#ifdef DTC_SIDE_BY_SIDE
#define DTC_SUPERCLASS "gfopentext.datatype"
#define DTC_SUPERLIB "datatypes/gfopentext.datatype"
#else
#define DTC_SUPERCLASS "text.datatype"
#define DTC_SUPERLIB "datatypes/text.datatype"
#endif
#define DTC_DISPATCH asc_dispatch
#define DTC_INST_SIZE asc_inst_size
#define DTC_IDSTRING LIBNAME " 47.100 (13.10.2026) GFOpenDatatypes, AmiCradle Team\r\n"

#include "ascii_core.h"
#include "../text/dtc_shell.h"

static const char ver[] __attribute__((used)) =
    "\0$VER: " LIBNAME " 47.100 (13.10.2026) GFOpenDatatypes, AmiCradle Team";

static const APTR lib_vectors[] = { DTC_STD_VECTORS, (APTR)-1 };

DTC_ROMTAG(lib_vectors);
