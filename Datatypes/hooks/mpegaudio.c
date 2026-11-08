/*
 * DTCD recognition code: MPEG audio (MP3, MP2) without an ID3v2 tag: a valid frame header, and the next one
 * (dthooks.h, dth_mpegaudio).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "hookentry.h"
#include "dthooks.h"

DTHOOK_TAG("mpegaudio");

ULONG dthook(struct DTHookContext *dthc)
{
    return dthc->dthc_Buffer && dth_mpegaudio((const unsigned char *)dthc->dthc_Buffer, dthc->dthc_BufferLength);
}
