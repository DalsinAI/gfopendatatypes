/*
 * gfopenimage datatypes: which source dt_read_source() (common/dtlib.c)
 * reads from, in plain C with no Amiga headers so the host can test it
 * (tests/sourcetest.c).
 *
 * A file (DTST_FILE with its DTA_Handle) is read through dos; memory
 * (DTST_MEMORY, V44: DTA_SourceAddress and DTA_SourceSize) is copied from
 * the program's buffer, so the class owns its bytes either way and frees
 * them with FreeVec() as before. datatypes.library 47.100 hands a non-IFF
 * memory source to the class with DTA_Handle NULL; before this every
 * picture class here refused it with ERROR_OBJECT_WRONG_TYPE.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef DTSOURCE_H
#define DTSOURCE_H

#define DTSRC_DTST_FILE   1     /* datatypes/datatypes.h */
#define DTSRC_DTST_MEMORY 5

enum dtsrc_kind {
    DTSRC_WRONG = 0,            /* neither: ERROR_OBJECT_WRONG_TYPE */
    DTSRC_FILE = 1,             /* read the file handle */
    DTSRC_MEMORY = 2            /* copy address .. address + size */
};

static enum dtsrc_kind dtsrc_kind(unsigned long type, unsigned long fh,
                                  const void *address, unsigned long size)
{
    if (type == DTSRC_DTST_FILE)
        return fh ? DTSRC_FILE : DTSRC_WRONG;
    if (type == DTSRC_DTST_MEMORY)
        return address && size ? DTSRC_MEMORY : DTSRC_WRONG;
    return DTSRC_WRONG;
}

#endif
