/* GFOpen8SVX: 8svx.datatype's class (a sound.datatype subclass) as a core
 * with no Amiga headers, over struct odt_cenv.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * Clean-room, from the IFF 8SVX specification (EA, public), the sound_dtc
 * and datatypes autodocs and the NDK 3.2 headers' layouts; never from
 * Kickstart, Hyperion or AROS code, nor any third-party datatype's.
 */
#ifndef OPENDATATYPES_SVX_CORE_H
#define OPENDATATYPES_SVX_CORE_H

#include "../sound/sound_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SVX_ID_8SVX ODT_ID('8', 'S', 'V', 'X')
#define SVX_ID_VHDR ODT_ID('V', 'H', 'D', 'R')
#define SVX_ID_BODY ODT_ID('B', 'O', 'D', 'Y')
#define SVX_ID_CHAN ODT_ID('C', 'H', 'A', 'N')
#define SVX_ID_NAME ODT_ID('N', 'A', 'M', 'E')
#define SVX_ID_AUTH ODT_ID('A', 'U', 'T', 'H')
#define SVX_ID_ANNO ODT_ID('A', 'N', 'N', 'O')
#define SVX_ID_COPY ODT_ID('(', 'c', ')', ' ')

/* CHAN */
#define SVX_LEFT   2
#define SVX_RIGHT  4
#define SVX_STEREO 6

odt_u32 svx_inst_size(void);
odt_u32 svx_dispatch(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, void *msg);

/* Fibonacci delta (the 8SVX spec's DUnpack): n bytes in, 2 * (n - 2)
 * samples out (the first byte pad, the second the start value) */
odt_u32 svx_fib_unpack(const odt_u8 *in, odt_u32 n, odt_s8 *out);

#ifdef __cplusplus
}
#endif
#endif
