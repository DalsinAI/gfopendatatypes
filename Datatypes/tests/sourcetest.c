/*
 * common/dtsource.h: which source dt_read_source() reads (GF-70: a picture
 * from memory, DTST_MEMORY, was refused with ERROR_OBJECT_WRONG_TYPE).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include "../common/dtsource.h"

static int fails;

static void check(const char *what, enum dtsrc_kind got, enum dtsrc_kind want)
{
    if (got != want) {
        printf("FAIL %s: %d, want %d\n", what, (int)got, (int)want);
        fails++;
    }
}

int main(void)
{
    static const unsigned char png[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };

    check("file with handle", dtsrc_kind(DTSRC_DTST_FILE, 0x1234, 0, 0), DTSRC_FILE);
    check("file without handle", dtsrc_kind(DTSRC_DTST_FILE, 0, 0, 0), DTSRC_WRONG);
    check("memory", dtsrc_kind(DTSRC_DTST_MEMORY, 0, png, sizeof png), DTSRC_MEMORY);
    check("memory, handle ignored", dtsrc_kind(DTSRC_DTST_MEMORY, 0x1234, png, sizeof png), DTSRC_MEMORY);
    check("memory without address", dtsrc_kind(DTSRC_DTST_MEMORY, 0, 0, 8), DTSRC_WRONG);
    check("memory of size 0", dtsrc_kind(DTSRC_DTST_MEMORY, 0, png, 0), DTSRC_WRONG);
    check("RAM", dtsrc_kind(3, 0, 0, 0), DTSRC_WRONG);
    check("clipboard", dtsrc_kind(2, 0x1234, 0, 0), DTSRC_WRONG);
    if (fails)
        return 1;
    printf("dtsource: 8 cases ok\n");
    return 0;
}
