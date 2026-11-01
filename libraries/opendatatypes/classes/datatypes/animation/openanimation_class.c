/* GFOpenDatatypes: an open animation.datatype for AmigaOS 3.2, API and ABI
 * compatible.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * Clean-room: written from the animation_dtc autodoc,
 * datatypes/animationclass.h (NDK 3.2) and OS 3.2's answers measured by
 * tests/AnimationClassABITest, never from Kickstart's, Hyperion's or
 * AROS's code or bytes. The class is animation_core.c (plain C, no Amiga
 * headers), tested under qemu-m68k (tests/m68k/test_opendatatypes_textanim.sh);
 * this file is the class library around it, from ../text/dtc_shell.h:
 * "animation.datatype" under datatypesclass, and the animation family's
 * services (adt_ext_amiga.h: bitmaps, pens, blits, the panel's drawing,
 * audio.device, the player process).
 *
 * LIBNAME is "animation.datatype" for the replacement and
 * "gfopenanimation.datatype" for the side-by-side build.
 */
#ifndef LIBNAME
#define LIBNAME "animation.datatype"
#endif
#define DTC_SUPERCLASS "datatypesclass"
#define DTC_SUPERLIB "datatypes.library"
#define DTC_DISPATCH adt_dispatch
#define DTC_INST_SIZE adt_inst_size
#define DTC_IDSTRING LIBNAME " 47.100 (13.10.2026) GFOpenDatatypes, AmiCradle Team\r\n"
#define DTC_EXT_INIT anim_ext_init
#define DTC_EXT_FREE anim_ext_free

#include "animation_core.h"
struct DTCBase;
static int anim_ext_init(struct DTCBase *base);
static void anim_ext_free(struct DTCBase *base);
#include "../text/dtc_shell.h"
#include "adt_ext_amiga.h"

static const char ver[] __attribute__((used)) =
    "\0$VER: " LIBNAME " 47.100 (13.10.2026) GFOpenDatatypes, AmiCradle Team";

/* class_ext: the services, then the one audio channel every object of
 * the class shares (ABI test: OS 3.2's own sharing) */
struct anim_ext {
    struct adt_ext x;
    struct ax_sound *snd;
};

static struct ax_sound *ax_sound(const struct odt_cenv *e) { return ((struct anim_ext *)e->class_ext)->snd; }

static int anim_ext_init(struct DTCBase *base)
{
    struct anim_ext *a = AllocVec(sizeof *a, MEMF_PUBLIC | MEMF_CLEAR);
    if (!a) return 0;
    if (!(a->snd = AllocVec(sizeof *a->snd, MEMF_PUBLIC | MEMF_CLEAR))) {
        FreeVec(a);
        return 0;
    }
    InitSemaphore(&a->snd->sem);
    a->x.alloc_bitmap = ax_alloc_bitmap;
    a->x.free_bitmap = ax_free_bitmap;
    a->x.obtain_pen = ax_obtain_pen;
    a->x.release_pen = ax_release_pen;
    a->x.screen_depth = ax_screen_depth;
    a->x.blit = ax_blit;
    a->x.fill = ax_fill;
    a->x.bevel = ax_bevel;
    a->x.glyph = ax_glyph;
    a->x.pen = ax_pen;
    a->x.font_ysize = ax_font_ysize;
    a->x.sound_play = ax_sound_play;
    a->x.sound_stop = ax_sound_stop;
    a->x.clock = ax_clock;
    a->x.player_start = ax_player_start;
    a->x.player_stop = ax_player_stop;
    base->ext = a;
    base->env.class_ext = a;
    return 1;
}

static void anim_ext_free(struct DTCBase *base)
{
    struct anim_ext *a = base->ext;
    if (!a) return;
    ax_sound_free(a->snd);
    FreeVec(a);
    base->ext = NULL;
}

static const APTR lib_vectors[] = { DTC_STD_VECTORS, (APTR)-1 };

DTC_ROMTAG(lib_vectors);
