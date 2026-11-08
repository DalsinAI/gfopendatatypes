/*
 * DTCD recognition code: Matroska, not WebM: the EBML header's DocType is "matroska"
 * (dthooks.h, dth_matroska).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "hookentry.h"
#include "dthooks.h"

DTHOOK_TAG("matroska");

ULONG dthook(struct DTHookContext *dthc)
{
    return dthc->dthc_Buffer && dth_matroska((const unsigned char *)dthc->dthc_Buffer, dthc->dthc_BufferLength);
}
