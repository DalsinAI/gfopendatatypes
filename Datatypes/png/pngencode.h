/*
 * gfopenpng.datatype's encoder: 32-bit ARGB pixels to a PNG file in memory,
 * written from the PNG specification (ISO/IEC 15948, W3C PNG 3rd edition).
 * The deflate and the chunk CRCs are z.library's (GFOpenZlib), through
 * common/zlibrary.h, so neither libpng nor a zlib is linked in.
 *
 * The file is RGBA 8 (colour type 6) when any pixel is not opaque, else RGB
 * 8 (colour type 2), with one row filter a row, chosen as most encoders do:
 * the one whose bytes, taken as signed, add up to the least. Level 0 stores
 * (filter None, deflate level 0); -1 is z.library's default.
 *
 * Plain C with no OS calls and no static data: memory comes from the
 * caller's env.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 * SPDX-License-Identifier: MIT
 */
#ifndef PNGENCODE_H
#define PNGENCODE_H

#include <exec/types.h>
#include "zlibrary.h"

struct pnge_env {
    void *(*alloc)(struct pnge_env *env, ULONG size);
    void (*free)(struct pnge_env *env, void *mem);
};

/* Flags */
#define PNGE_PREMULTIPLIED 1UL     /* colour is premultiplied by alpha (cairo, WebCore): undo it */
#define PNGE_OPAQUE        2UL     /* ignore alpha: always RGB */

/* The IoErr() codes it gives (as sdtcore.h's) */
#define PNGE_ERR_MEMORY  103L      /* ERROR_NO_FREE_STORE */
#define PNGE_ERR_ARGS    116L      /* ERROR_REQUIRED_ARG_MISSING: no pixels or a bad size */
#define PNGE_ERR_DEFLATE 2001L     /* DTERROR_COULDNT_SAVE: z.library said no */

/* Encodes width x height pixels, each four bytes A, R, G, B (0xAARRGGBB
 * on the 68k: picture.datatype's PBPAFMT_ARGB, WebCore's ARGB32), rows
 * stride bytes apart. 0 and the file in *png (from env, *size bytes), or
 * an error code. */
LONG png_encode(struct Library *zbase, struct pnge_env *env, const UBYTE *argb, ULONG width,
                ULONG height, ULONG stride, ULONG flags, LONG level, UBYTE **png, ULONG *size);

#endif
