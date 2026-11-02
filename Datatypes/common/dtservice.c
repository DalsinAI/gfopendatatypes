/*
 * gfopenimage datatypes: gfopenservice.device calls (dtservice.h).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <string.h>
#include <exec/io.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "dtlib.h"
#include "dtservice.h"

static LONG run(struct dt_service *s, UWORD command)
{
    s->io->os_Req.io_Command = command;
    DoIO((struct IORequest *)s->io);
    return s->io->os_Req.io_Error ? OSERR_LOST : s->io->os_Status;
}

BOOL dt_service_open(struct dt_service *s, const char *name)
{
    memset(s, 0, sizeof *s);
    if (!(s->port = CreateMsgPort()))
        return FALSE;
    if (!(s->io = (struct OSRequest *)CreateIORequest(s->port, sizeof *s->io))) {
        dt_service_close(s);
        return FALSE;
    }
    if (OpenDevice((CONST_STRPTR)OPENSERVICE_NAME, 0, (struct IORequest *)s->io, 0)) {
        DeleteIORequest((struct IORequest *)s->io);
        s->io = NULL;
        dt_service_close(s);
        return FALSE;
    }
    memset(s->io->os_Buf, 0, sizeof s->io->os_Buf);
    s->io->os_Buf[0].ob_Data = (APTR)name;
    s->io->os_Buf[0].ob_Length = strlen(name);
    if (run(s, OSCMD_OPEN) != OSERR_OK) {
        dt_service_close(s);
        return FALSE;
    }
    s->handle = (UWORD)s->io->os_Result;
    s->open = TRUE;
    return TRUE;
}

LONG dt_service_call(struct dt_service *s, UWORD op, ULONG arg, ULONG flags,
                     const struct OSBuffer buf[4], const ULONG extra[4], ULONG *result, ULONG *aux)
{
    LONG status;

    s->io->os_Service = s->handle;
    s->io->os_Op = op;
    s->io->os_Flags = flags;
    s->io->os_Arg = arg;
    memcpy(s->io->os_Buf, buf, sizeof s->io->os_Buf);
    memcpy(s->io->os_Extra, extra, sizeof s->io->os_Extra);
    status = run(s, OSCMD_CALL);
    if (result)
        *result = s->io->os_Result;
    if (aux)
        *aux = s->io->os_Aux;
    return status;
}

void dt_service_close(struct dt_service *s)
{
    if (s->io) {
        if (s->open) {
            s->io->os_Service = s->handle;
            run(s, OSCMD_CLOSE);
        }
        if (s->io->os_Req.io_Device)
            CloseDevice((struct IORequest *)s->io);
        DeleteIORequest((struct IORequest *)s->io);
    }
    if (s->port)
        DeleteMsgPort(s->port);
    memset(s, 0, sizeof *s);
}

ULONG dt_max_side(void)
{
    char buf[16];
    LONG n = GetVar((CONST_STRPTR)"GFOpenImage/MaxSide", (STRPTR)buf, sizeof buf, GVF_GLOBAL_ONLY);
    ULONG v = 0;
    LONG i;

    for (i = 0; i < n && buf[i] >= '0' && buf[i] <= '9'; i++)
        v = v * 10 + (buf[i] - '0');
    return v >= 16 ? v : 4096;
}

ULONG dt_name_hint(CONST_STRPTR name)
{
    CONST_STRPTR dot = NULL, p;
    ULONG hint = 0;
    int i;

    if (!name)
        return 0;
    for (p = name; *p; p++)
        if (*p == '.')
            dot = p + 1;
        else if (*p == '/' || *p == ':')
            dot = NULL;
    if (!dot || !*dot)
        return 0;
    for (i = 0; i < 4; i++) {
        UBYTE c = dot[0] ? *dot++ : ' ';
        if (c >= 'a' && c <= 'z')
            c -= 'a' - 'A';
        hint = hint << 8 | c;
    }
    return hint;                       /* longer ones cut to four: "markdown" -> 'MARK' */
}
