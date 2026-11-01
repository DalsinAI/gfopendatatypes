/*
 * webp.datatype check: open pictures through datatypes.library, read them
 * back as ARGB (PDTM_READPIXELARRAY) and print a checksum, to compare with
 * a PC (tests/webpref.py). Pictures without alpha count as opaque.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/datatypes.h>
#include <clib/alib_protos.h>

struct Library *DataTypesBase;

int main(int argc, char **argv)
{
    int i;
    if (!(DataTypesBase = OpenLibrary((CONST_STRPTR)"datatypes.library", 43)))
        return 20;
    for (i = 1; i < argc; i++) {
        struct BitMapHeader *bmh = NULL;
        ULONG alpha = 0, p, count;
        unsigned long sum = 0;
        UBYTE *pixels;
        Object *o = NewDTObject((APTR)argv[i], DTA_GroupID, GID_PICTURE, PDTA_DestMode, PMODE_V43, PDTA_Remap, FALSE, TAG_DONE);
        if (!o) {
            printf("%s: NEWDTOBJECT_FAIL ioerr=%ld\n", argv[i], (long)IoErr());
            continue;
        }
        GetDTAttrs(o, PDTA_BitMapHeader, (ULONG)&bmh, PDTA_AlphaChannel, (ULONG)&alpha, TAG_DONE);
        DoMethod(o, DTM_PROCLAYOUT, NULL, 1);
        count = (ULONG)bmh->bmh_Width * bmh->bmh_Height;
        pixels = malloc(count * 4);
        if (!pixels || !DoMethod(o, PDTM_READPIXELARRAY, (ULONG)pixels, PBPAFMT_ARGB, bmh->bmh_Width * 4, 0, 0, bmh->bmh_Width, bmh->bmh_Height)) {
            printf("%s: READ_FAIL\n", argv[i]);
        } else {
            for (p = 0; p < count; p++) {
                UBYTE *q = pixels + p * 4;
                unsigned long a = alpha ? q[0] : 0xff;
                sum = (sum * 31 + ((a << 24) | ((unsigned long)q[1] << 16) | ((unsigned long)q[2] << 8) | q[3])) & 0xffffffffUL;
            }
            printf("%s %u %u alpha=%d %08lx\n", argv[i], bmh->bmh_Width, bmh->bmh_Height, alpha ? 1 : 0, sum);
        }
        free(pixels);
        DisposeDTObject(o);
    }
    CloseLibrary(DataTypesBase);
    return 0;
}
