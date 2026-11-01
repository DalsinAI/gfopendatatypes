/* GFOpenDatatypes ascii.datatype core, in plain C with no Amiga headers.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * Clean-room, from the ascii_dtc and text_dtc autodocs, NDK 3.2's
 * datatypes/textclass.h, the IFF FTXT specification and OS 3.2's answers
 * measured by tests/TextClassABITest; never from Kickstart, Hyperion,
 * AROS or any third-party datatype's code.
 *
 * ascii.datatype is text.datatype with a source: at OM_NEW it reads the
 * file (DTST_FILE), the clipboard or an FTXT file (FTXT's CHRS chunks
 * through iffparse) or memory (DTST_MEMORY), and gives text.datatype the
 * bytes as TDTA_Buffer. Everything else is text.datatype's.
 */
#ifndef OPENDATATYPES_ASCII_CORE_H
#define OPENDATATYPES_ASCII_CORE_H

#include "../text/text_core.h"

#define ASC_IFFPARSE_SCAN 0
#define ASC_IFFERR_EOF   (-1)
#define ASC_IFFERR_EOC   (-2)
#define ASC_OFFSET_BEGINNING (-1)
#define ASC_MAXTEXT (64UL << 20)        /* the most read: 64 MB (ours) */

/* Reads the object's source into a new buffer (env alloc, *len bytes,
 * NUL after them); 0, or the DOS error (IoErr is set too). */
odt_s32 asc_read_source(const struct odt_cenv *env, void *obj, char **buf, odt_u32 *len);

odt_u32 asc_dispatch(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, void *msg);
odt_u32 asc_inst_size(void);

#endif
