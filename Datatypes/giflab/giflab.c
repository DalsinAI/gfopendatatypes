/*
 * Lab tool, not for merging: opens each GIF named twice through
 * datatypes.library. As GID_PICTURE (GFOpenBrowser's way): OIA_Frames,
 * then every frame through OIM_READFRAME with its delay and an FNV-1a
 * hash of the ARGB bytes (tests/giftest.c prints the same hashes). With no
 * group (MultiView's way): the object's ADTA_Frames and fps, or that it is
 * a picture.
 */
#include <stdio.h>
#include <stdlib.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <datatypes/animationclass.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/datatypes.h>
#include <clib/alib_protos.h>
#include <datatypes/gfopenimage.h>

struct Library *DataTypesBase;

int main(int argc, char **argv)
{
    int i;
    if (!(DataTypesBase = OpenLibrary((CONST_STRPTR)"datatypes.library", 39)))
        return 20;
    for (i = 1; i < argc; i++) {
        struct BitMapHeader *bmh = NULL;
        ULONG frames = 0, f, w, h, n;
        Object *o = NewDTObject((APTR)argv[i], DTA_GroupID, GID_PICTURE, TAG_DONE);
        if (!o) {
            printf("%s picture: NewDTObject failed, IoErr %ld\n", argv[i], (long)IoErr());
        } else {
            GetDTAttrs(o, PDTA_BitMapHeader, (ULONG)&bmh, TAG_DONE);
            GetAttr(OIA_Frames, o, &frames);
            w = bmh ? bmh->bmh_Width : 0;
            h = bmh ? bmh->bmh_Height : 0;
            printf("%s picture: %lux%lu depth %u frames=%lu\n", argv[i], w, h, bmh ? bmh->bmh_Depth : 0, frames);
            if (frames && w && h) {
                UBYTE *px = AllocVec(w * h * 4, MEMF_ANY);
                for (f = 0; px && f < frames; f++) {
                    struct oiReadFrame m;
                    ULONG hash = 2166136261UL;
                    m.MethodID = OIM_READFRAME;
                    m.oirf_GInfo = NULL;
                    m.oirf_Frame = f;
                    m.oirf_Pixels = px;
                    m.oirf_Stride = w * 4;
                    m.oirf_Flags = 0;
                    m.oirf_Delay = 0;
                    m.oirf_HasAlpha = 0;
                    if (!DoMethodA(o, (Msg)&m)) {
                        printf("%s#%lu READFRAME failed\n", argv[i], f);
                        continue;
                    }
                    for (n = 0; n < w * h * 4; n++) {
                        hash ^= px[n];
                        hash *= 16777619UL;
                    }
                    printf("%s#%lu delay=%lu alpha=%lu fnv=%08lx\n", argv[i], f, m.oirf_Delay, m.oirf_HasAlpha, hash);
                }
                if (px)
                    FreeVec(px);
            }
            DisposeDTObject(o);
        }
        o = NewDTObject((APTR)argv[i], TAG_DONE);
        if (!o) {
            printf("%s any: NewDTObject failed, IoErr %ld\n", argv[i], (long)IoErr());
        } else {
            ULONG af = 0, fps = 0, aw = 0, ah = 0, ad = 0;
            if (GetDTAttrs(o, ADTA_Frames, (ULONG)&af, ADTA_FramesPerSecond, (ULONG)&fps,
                    ADTA_Width, (ULONG)&aw, ADTA_Height, (ULONG)&ah, ADTA_Depth, (ULONG)&ad, TAG_DONE) == 5 && af)
                printf("%s any: animation %lux%lu depth %lu frames=%lu fps=%lu\n", argv[i], aw, ah, ad, af, fps);
            else
                printf("%s any: picture\n", argv[i]);
            DisposeDTObject(o);
        }
    }
    CloseLibrary(DataTypesBase);
    return 0;
}
