/*
 * DTCD recognition code: Ogg Theora: a BOS page starts a Theora stream
 * (dthooks.h, dth_oggtheora).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "hookentry.h"
#include "dthooks.h"

DTHOOK_TAG("oggtheora");

ULONG dthook(struct DTHookContext *dthc)
{
    return dthc->dthc_Buffer && dth_oggtheora((const unsigned char *)dthc->dthc_Buffer, dthc->dthc_BufferLength);
}
