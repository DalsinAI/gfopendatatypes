/*
 * gfopenimage datatypes: our own picture decoders on the services card
 * (dtnative.h). MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <string.h>
#include <exec/memory.h>
#include <dos/var.h>
#include <libraries/configvars.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/expansion.h>

#include "dtlib.h"
#include "dtservice.h"
#include "dtnative.h"

#define MD_NDECODE 33
/* The AmiCradle Team's AutoConfig boards (gfopenservice include/openservice.h). */
#define CARD_MANUFACTURER 0xDA15

/* A board of ours in this machine: without one there is no card, and the
 * device (perhaps not even on disk) is not opened at all. */
static BOOL boardThere(void)
{
    struct ExpansionBase *ExpansionBase = (struct ExpansionBase *)OpenLibrary((CONST_STRPTR)"expansion.library", 37);
    BOOL there = FALSE;

    if (ExpansionBase) {
        there = FindConfigDev(NULL, CARD_MANUFACTURER, -1) != NULL;
        CloseLibrary((struct Library *)ExpansionBase);
    }
    return there;
}

static ULONG get32(const UBYTE *p)
{
    return (ULONG)p[0] << 24 | (ULONG)p[1] << 16 | (ULONG)p[2] << 8 | p[3];
}

BOOL dt_card_decode(ULONG decoder, ULONG mode, ULONG frame, const UBYTE *file, ULONG size, APTR pixels,
                    ULONG room, ULONG info[4], ULONG *pw, ULONG *ph)
{
    struct dt_service svc;
    struct OSBuffer buf[4];
    ULONG extra[4] = { decoder, mode, 0, 0 }, w = 0, h = 0;
    UBYTE answer[16], var[4];
    LONG st;
    int i;

    if (GetVar((CONST_STRPTR)"GFOpenImage/CardDecode", (STRPTR)var, sizeof var, GVF_GLOBAL_ONLY) > 0 && var[0] == '0')
        return FALSE;
    if (!boardThere() || !dt_service_open(&svc, "media.decode/1"))
        return FALSE;
    if (dt_service_where(&svc) != OSWHERE_CARD) {
        dt_service_close(&svc);
        return FALSE;
    }
    memset(buf, 0, sizeof buf);
    memset(answer, 0, sizeof answer);
    buf[0].ob_Data = (APTR)file;
    buf[0].ob_Length = size;
    buf[1].ob_Data = pixels;
    buf[1].ob_Length = room;
    buf[2].ob_Data = answer;
    buf[2].ob_Length = sizeof answer;
    st = dt_service_call(&svc, MD_NDECODE, frame, 2 | 4, buf, extra, &w, &h);
    dt_service_close(&svc);
    if (st != OSERR_OK)
        return FALSE;
    for (i = 0; i < 4; i++)
        info[i] = get32(answer + 4 * i);
    *pw = w;
    *ph = h;
    return TRUE;
}
