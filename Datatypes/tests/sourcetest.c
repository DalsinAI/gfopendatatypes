/*
 * common/dtsource.h: which source dt_read_source() reads (GF-70: a picture
 * from memory, DTST_MEMORY, was refused with ERROR_OBJECT_WRONG_TYPE).
 * The DTST_ numbers here are the NDK's own, written out, so a wrong one in
 * dtsource.h fails this test.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>

/* datatypes/datatypes.h, as common/dtlib.c sees it before dtsource.h */
#define DTST_RAM       1
#define DTST_FILE      2
#define DTST_CLIPBOARD 3
#define DTST_HOTLINK   4
#define DTST_MEMORY    5

#include "../common/dtsource.h"

static int fails, cases;

static void check(const char *what, enum dtsrc_kind got, enum dtsrc_kind want)
{
    cases++;
    if (got != want) {
        printf("FAIL %s: %d, want %d\n", what, (int)got, (int)want);
        fails++;
    }
}

int main(void)
{
    static const unsigned char png[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };

    check("file with handle", dtsrc_kind(DTST_FILE, 0x1234, 0, 0), DTSRC_FILE);
    check("file without handle", dtsrc_kind(DTST_FILE, 0, 0, 0), DTSRC_WRONG);
    check("file, memory tags ignored", dtsrc_kind(DTST_FILE, 0x1234, png, sizeof png), DTSRC_FILE);
    check("memory", dtsrc_kind(DTST_MEMORY, 0, png, sizeof png), DTSRC_MEMORY);
    check("memory, handle ignored", dtsrc_kind(DTST_MEMORY, 0x1234, png, sizeof png), DTSRC_MEMORY);
    check("memory without address", dtsrc_kind(DTST_MEMORY, 0, 0, 8), DTSRC_WRONG);
    check("memory of size 0", dtsrc_kind(DTST_MEMORY, 0, png, 0), DTSRC_WRONG);
    check("RAM", dtsrc_kind(DTST_RAM, 0, 0, 0), DTSRC_WRONG);
    check("RAM with a handle", dtsrc_kind(DTST_RAM, 0x1234, 0, 0), DTSRC_WRONG);
    check("clipboard", dtsrc_kind(DTST_CLIPBOARD, 0x1234, 0, 0), DTSRC_WRONG);
    check("hotlink", dtsrc_kind(DTST_HOTLINK, 0x1234, 0, 0), DTSRC_WRONG);
    if (fails)
        return 1;
    printf("dtsource: %d cases ok\n", cases);
    return 0;
}
