/*
 * DTCD recognition code for the documents gfopendoc.datatype has laid out by
 * the doc.render/1 service but that are text all the same (Markdown, CSV):
 * TRUE when the service can be there (dthooks.h, dth_docservice), so that
 * without it the ascii datatype shows them as text. Cheap enough for every
 * such file a directory lister asks about: a device list look-up, a board
 * look-up and a lock in ENV:, no I/O to the service itself.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#define __NOLIBBASE__
#include "hookentry.h"
#include <exec/execbase.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/expansion.h>
#include "dthooks.h"

DTHOOK_TAG("docservice");

/* include/devices/gfopenservice.h and gfopenservice's openservice.h */
#define SERVICE_DEVICE "gfopenservice.device"
#define SERVICE_MANUFACTURER 0xDA15
#define SERVICE_PAIRED "ENV:GFOpenService/Paired"

static int exists(struct Library *DOSBase, const char *path)
{
    BPTR l = Lock((CONST_STRPTR)path, ACCESS_READ);
    if (!l) return 0;
    UnLock(l);
    return 1;
}

ULONG dthook(struct DTHookContext *dthc)
{
    struct ExecBase *SysBase = (struct ExecBase *)dthc->dthc_SysBase;
    struct Library *DOSBase = dthc->dthc_DOSBase;
    struct Library *ExpansionBase;
    int device, card = 0;

    Forbid();
    device = FindName(&SysBase->DeviceList, (CONST_STRPTR)SERVICE_DEVICE) != NULL;
    Permit();
    if (!device) device = exists(DOSBase, "DEVS:" SERVICE_DEVICE);
    if (device && (ExpansionBase = OpenLibrary((CONST_STRPTR)"expansion.library", 37))) {
        card = FindConfigDev(NULL, SERVICE_MANUFACTURER, -1) != NULL;
        CloseLibrary(ExpansionBase);
    }
    return dth_docservice(device, card, device && !card && exists(DOSBase, SERVICE_PAIRED));
}
