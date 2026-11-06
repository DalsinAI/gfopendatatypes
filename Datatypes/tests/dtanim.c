/*
 * webm.datatype check: open a file as an animation through datatypes.library,
 * load every frame with ADTM_LOADFRAME and print a checksum of its colour
 * indices, to compare with the PC (tests/webmprobe.c -chunky).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <exec/memory.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/animationclass.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/datatypes.h>
#include <clib/alib_protos.h>

struct Library *DataTypesBase;

int main(int argc, char **argv)
{
    Object *o;
    ULONG width = 0, height = 0, frames = 0, fps = 0, depth = 0, i;
    struct BitMap *key = NULL;
    UBYTE *chunky;
    struct RastPort rp, temp;
    struct BitMap *tempBm;

    if (argc < 2)
        return 10;
    if (!(DataTypesBase = OpenLibrary((CONST_STRPTR)"datatypes.library", 39)))
        return 20;
    o = NewDTObject((APTR)argv[1], DTA_GroupID, GID_ANIMATION, TAG_DONE);
    if (!o) {
        printf("%s: NEWDTOBJECT_FAIL ioerr=%ld\n", argv[1], (long)IoErr());
        CloseLibrary(DataTypesBase);
        return 20;
    }
    GetDTAttrs(o, ADTA_Width, (ULONG)&width, ADTA_Height, (ULONG)&height, ADTA_Depth, (ULONG)&depth,
        ADTA_Frames, (ULONG)&frames, ADTA_FramesPerSecond, (ULONG)&fps, ADTA_KeyFrame, (ULONG)&key, TAG_DONE);
    printf("%s %lux%lu depth=%lu frames=%lu fps=%lu key=%s\n", argv[1], (unsigned long)width,
           (unsigned long)height, (unsigned long)depth, (unsigned long)frames, (unsigned long)fps, key ? "yes" : "no");
    chunky = malloc(((width + 15) & ~15) * height);
    tempBm = AllocBitMap((width + 15) & ~15, 1, 8, 0, NULL);
    InitRastPort(&temp);
    temp.BitMap = tempBm;
    for (i = 0; chunky && tempBm && i < frames; i++) {
        struct adtFrame alf = { ADTM_LOADFRAME };
        unsigned long sum = 0, p;
        ULONG y;
        alf.alf_TimeStamp = i;
        alf.alf_Frame = i;
        if (!DoMethodA(o, (Msg)&alf) || !alf.alf_BitMap) {
            printf("frame %lu LOAD_FAIL\n", (unsigned long)i);
            continue;
        }
        InitRastPort(&rp);
        rp.BitMap = alf.alf_BitMap;
        /* One line at a time: ReadPixelLine8 wants a temporary one-line RastPort. */
        for (y = 0; y < height; y++)
            ReadPixelLine8(&rp, 0, y, width, chunky + y * width, &temp);
        for (p = 0; p < width * height; p++)
            sum = (sum * 31 + chunky[p]) & 0xffffffffUL;
        printf("frame %lu shown=%lu sum=%08lx\n", (unsigned long)i, (unsigned long)alf.alf_Frame, sum);
        alf.MethodID = ADTM_UNLOADFRAME;
        DoMethodA(o, (Msg)&alf);
    }
    if (tempBm)
        FreeBitMap(tempBm);
    free(chunky);
    DisposeDTObject(o);
    CloseLibrary(DataTypesBase);
    printf("DTANIM_DONE\n");
    return 0;
}
