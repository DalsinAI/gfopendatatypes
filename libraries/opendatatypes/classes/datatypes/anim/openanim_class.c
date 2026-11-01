/* GFOpenDatatypes: an open anim.datatype for AmigaOS 3.2, API and ABI
 * compatible.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * Clean-room: written from the IFF ANIM and ILBM specifications (EA's
 * public documents), the animation_dtc autodoc and OS 3.2's answers
 * measured by tests/AnimationClassABITest, never from Kickstart's,
 * Hyperion's, AROS's or any third-party datatype's code or bytes. The
 * class is anim_core.c (plain C, no Amiga headers), tested pixel by pixel
 * under qemu-m68k (tests/m68k/test_opendatatypes_textanim.sh); this file
 * is the class library around it, from ../text/dtc_shell.h:
 * "anim.datatype" under animation.datatype, which it keeps open, and the
 * bitmaps its frames go in.
 *
 * LIBNAME is "anim.datatype" for the replacement and "gfopenanim.datatype"
 * (under "gfopenanimation.datatype") for the side-by-side build.
 */
#ifndef LIBNAME
#define LIBNAME "anim.datatype"
#endif
#ifdef DTC_SIDE_BY_SIDE
#define DTC_SUPERCLASS "gfopenanimation.datatype"
#define DTC_SUPERLIB "datatypes/gfopenanimation.datatype"
#else
#define DTC_SUPERCLASS "animation.datatype"
#define DTC_SUPERLIB "datatypes/animation.datatype"
#endif
#define DTC_DISPATCH anm_dispatch
#define DTC_INST_SIZE anm_inst_size
#define DTC_IDSTRING LIBNAME " 47.100 (13.10.2026) GFOpenDatatypes, AmiCradle Team\r\n"
#define DTC_EXT_INIT anim_ext_init
#define ADT_EXT_BITMAPS_ONLY

#include "anim_core.h"
struct DTCBase;
static int anim_ext_init(struct DTCBase *base);
#include "../text/dtc_shell.h"
#include "../animation/adt_ext_amiga.h"

static const char ver[] __attribute__((used)) =
    "\0$VER: " LIBNAME " 47.100 (13.10.2026) GFOpenDatatypes, AmiCradle Team";

/* a subclass only makes and frees its frames' bitmaps */
static const struct adt_ext anim_ext = { .alloc_bitmap = ax_alloc_bitmap, .free_bitmap = ax_free_bitmap };

static int anim_ext_init(struct DTCBase *base)
{
    base->ext = (void *)&anim_ext;
    base->env.class_ext = (void *)&anim_ext;
    return 1;
}

static const APTR lib_vectors[] = { DTC_STD_VECTORS, (APTR)-1 };

DTC_ROMTAG(lib_vectors);
