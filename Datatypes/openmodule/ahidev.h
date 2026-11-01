/*
 * gfopenmodule.datatype: the little of ahi.device's interface it uses (AHI 4
 * and later, the device interface: CMD_WRITE with double buffering through
 * ahir_Link), written out from AHI's documented ABI so the build needs no
 * AHI SDK. The NDK 3.2 has no AHI headers.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OM_AHIDEV_H
#define OM_AHIDEV_H

#include <exec/io.h>

#define AHINAME          "ahi.device"
#define AHI_DEFAULT_UNIT 0             /* the unit AHI prefs sets up for music */
#define AHIST_M16S       1UL           /* mono, 16-bit signed */
#define AHIST_S16S       3UL           /* stereo, 16-bit signed, left first */

struct AHIRequest {
    struct IOStdReq ahir_Std;
    UWORD ahir_Version;                /* the version needed, set before OpenDevice */
    UWORD ahir_Pad1;
    ULONG ahir_Private[2];
    ULONG ahir_Type;                   /* AHIST_ */
    ULONG ahir_Frequency;
    LONG ahir_Volume;                  /* Fixed: 0x10000 is full */
    LONG ahir_Position;                /* Fixed: 0x8000 is the middle */
    struct AHIRequest *ahir_Link;      /* plays this one straight after that one */
};

#endif
