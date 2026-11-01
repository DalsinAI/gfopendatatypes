/* GFOpenILBM: ilbm.datatype's class (ILBM and PBM, a picture.datatype
 * subclass) as a core with no Amiga headers, over struct odt_cenv.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * Clean-room, from the IFF ILBM specification and its updates (EA and
 * Commodore, public), the picture_dtc and datatypes autodocs and the NDK
 * 3.2 headers' layouts; never from Kickstart, Hyperion or AROS code, nor
 * any third-party datatype's.
 */
#ifndef OPENDATATYPES_ILBM_CORE_H
#define OPENDATATYPES_ILBM_CORE_H

#include "../picture/picture_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ILBM_ID_ILBM ODT_ID('I', 'L', 'B', 'M')
#define ILBM_ID_PBM  ODT_ID('P', 'B', 'M', ' ')
#define ILBM_ID_BMHD ODT_ID('B', 'M', 'H', 'D')
#define ILBM_ID_CMAP ODT_ID('C', 'M', 'A', 'P')
#define ILBM_ID_CAMG ODT_ID('C', 'A', 'M', 'G')
#define ILBM_ID_GRAB ODT_ID('G', 'R', 'A', 'B')
#define ILBM_ID_BODY ODT_ID('B', 'O', 'D', 'Y')
#define ILBM_ID_NAME ODT_ID('N', 'A', 'M', 'E')
#define ILBM_ID_AUTH ODT_ID('A', 'U', 'T', 'H')
#define ILBM_ID_ANNO ODT_ID('A', 'N', 'N', 'O')
#define ILBM_ID_COPY ODT_ID('(', 'c', ')', ' ')

/* iffparse.library */
#define ILBM_IFFPARSE_SCAN 0
#define ILBM_IFFERR_EOF    (-1)

/* graphics/view.h's bits a CAMG of the old 16-bit kind may carry that are
 * not part of a mode */
#define ILBM_GENLOCK_VIDEO 0x0002UL
#define ILBM_GENLOCK_AUDIO 0x0100UL
#define ILBM_VP_HIDE       0x2000UL
#define ILBM_SPRITES       0x4000UL

/* ilbm.datatype keeps nothing of its own: everything is the picture's */
odt_u32 ilbm_inst_size(void);
odt_u32 ilbm_dispatch(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, void *msg);

/* ByteRun1: unpacks into out (n bytes) from in (len bytes); answers the
 * bytes of in used; what in lacks is left 0 */
odt_u32 ilbm_byterun1_unpack(const odt_u8 *in, odt_u32 len, odt_u8 *out, odt_u32 n);

#ifdef __cplusplus
}
#endif
#endif
