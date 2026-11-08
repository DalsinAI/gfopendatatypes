/*
 * gfopenimage datatypes: gfopenservice.device calls (dtservice.h).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <string.h>
#include <exec/io.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "dtlib.h"
#include "dtservice.h"

/* A port that belongs to no task: PA_IGNORE until a call sets the caller's
 * task and signal on it for as long as it waits. */
static struct MsgPort *newPort(void)
{
    struct MsgPort *p = AllocVec(sizeof *p, MEMF_PUBLIC | MEMF_CLEAR);

    if (p) {
        p->mp_Node.ln_Type = NT_MSGPORT;
        p->mp_Flags = PA_IGNORE;
        p->mp_MsgList.lh_Head = (struct Node *)&p->mp_MsgList.lh_Tail;
        p->mp_MsgList.lh_Tail = NULL;
        p->mp_MsgList.lh_TailPred = (struct Node *)&p->mp_MsgList.lh_Head;
        p->mp_MsgList.lh_Type = NT_MESSAGE;
    }
    return p;
}

/* The port signals this task with sig until noSignal(). */
static void toMe(struct MsgPort *p, BYTE sig)
{
    p->mp_SigTask = FindTask(NULL);
    p->mp_SigBit = (UBYTE)sig;
    p->mp_Flags = PA_SIGNAL;
}

static void noSignal(struct MsgPort *p)
{
    p->mp_Flags = PA_IGNORE;
    p->mp_SigTask = NULL;
}

static LONG status(struct OSRequest *io)
{
    return io->os_Req.io_Error ? OSERR_LOST : io->os_Status;
}

static LONG run(struct dt_service *s, UWORD command)
{
    BYTE sig = AllocSignal(-1);

    if (sig < 0)
        return OSERR_HOST;
    toMe(s->port, sig);
    s->io->os_Req.io_Command = command;
    DoIO((struct IORequest *)s->io);
    noSignal(s->port);
    FreeSignal(sig);
    return status(s->io);
}

BOOL dt_service_open(struct dt_service *s, const char *name)
{
    memset(s, 0, sizeof *s);
    s->aheadSig = -1;
    if (!(s->port = newPort()))
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

static void fill(struct dt_service *s, struct OSRequest *io, UWORD op, ULONG arg, ULONG flags,
                 const struct OSBuffer buf[4], const ULONG extra[4])
{
    io->os_Service = s->handle;
    io->os_Op = op;
    io->os_Flags = flags;
    io->os_Arg = arg;
    memcpy(io->os_Buf, buf, sizeof io->os_Buf);
    memcpy(io->os_Extra, extra, sizeof io->os_Extra);
}

LONG dt_service_call(struct dt_service *s, UWORD op, ULONG arg, ULONG flags,
                     const struct OSBuffer buf[4], const ULONG extra[4], ULONG *result, ULONG *aux)
{
    LONG st;

    if (!s->open)
        return OSERR_LOST;
    fill(s, s->io, op, arg, flags, buf, extra);
    st = run(s, OSCMD_CALL);
    if (result)
        *result = s->io->os_Result;
    if (aux)
        *aux = s->io->os_Aux;
    return st;
}

BOOL dt_service_send(struct dt_service *s, UWORD op, ULONG arg, ULONG flags,
                     const struct OSBuffer buf[4], const ULONG extra[4])
{
    BYTE sig;

    if (!s->open || s->sent)
        return FALSE;
    if (!s->ahead) {
        /* A copy of the opened request, on a port of its own. */
        if (!s->aheadPort && !(s->aheadPort = newPort()))
            return FALSE;
        if (!(s->ahead = AllocVec(sizeof *s->ahead, MEMF_PUBLIC | MEMF_CLEAR)))
            return FALSE;
        *s->ahead = *s->io;
        s->ahead->os_Req.io_Message.mn_ReplyPort = s->aheadPort;
    }
    if ((sig = AllocSignal(-1)) < 0)
        return FALSE;
    fill(s, s->ahead, op, arg, flags, buf, extra);
    s->ahead->os_Req.io_Command = OSCMD_CALL;
    s->aheadTask = FindTask(NULL);
    s->aheadSig = sig;
    toMe(s->aheadPort, sig);
    s->sent = TRUE;
    SendIO((struct IORequest *)s->ahead);
    return TRUE;
}

LONG dt_service_wait(struct dt_service *s, ULONG *result, ULONG *aux)
{
    struct Task *me = FindTask(NULL);
    LONG st;

    if (!s->sent)
        return OSERR_BADREQUEST;
    if (me == s->aheadTask) {
        WaitIO((struct IORequest *)s->ahead);
        FreeSignal(s->aheadSig);
    } else {
        /* Another task takes the answer (the object going while its
         * loader's read-ahead is out): the reply is pointed at it before
         * it comes, with interrupts off so it can't come in between. The
         * sender's signal stays the sender's (its task may be gone). */
        BYTE sig = AllocSignal(-1);
        if (sig >= 0) {
            Disable();
            if (s->ahead->os_Req.io_Message.mn_Node.ln_Type != NT_REPLYMSG)
                toMe(s->aheadPort, sig);
            Enable();
            WaitIO((struct IORequest *)s->ahead);
            FreeSignal(sig);
        } else {
            /* No signal free: look until it has come, then take it. */
            Disable();
            s->aheadPort->mp_Flags = PA_IGNORE;
            Enable();
            while (!CheckIO((struct IORequest *)s->ahead))
                ;
            WaitIO((struct IORequest *)s->ahead);
        }
    }
    noSignal(s->aheadPort);
    s->sent = FALSE;
    s->aheadTask = NULL;
    s->aheadSig = -1;
    st = status(s->ahead);
    if (result)
        *result = s->ahead->os_Result;
    if (aux)
        *aux = s->ahead->os_Aux;
    return st;
}

ULONG dt_service_where(struct dt_service *s)
{
    if (s->open && !s->where) {
        s->io->os_Service = s->handle;
        if (run(s, OSCMD_WHERE) == OSERR_OK)
            s->where = s->io->os_Result;
    }
    return s->where;
}

void dt_service_close(struct dt_service *s)
{
    if (s->sent) {
        AbortIO((struct IORequest *)s->ahead);
        dt_service_wait(s, NULL, NULL);
    }
    if (s->ahead)
        FreeVec(s->ahead);
    if (s->aheadPort)
        FreeVec(s->aheadPort);
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
        FreeVec(s->port);
    memset(s, 0, sizeof *s);
    s->aheadSig = -1;
}

static ULONG get32(const UBYTE *p)
{
    return (ULONG)p[0] << 24 | (ULONG)p[1] << 16 | (ULONG)p[2] << 8 | p[3];
}

LONG dt_file_call(struct dt_service *s, struct dt_file *f, UWORD op, ULONG arg, ULONG flags,
                  struct OSBuffer buf[4], ULONG extra[4], ULONG *result, ULONG *aux, BOOL last)
{
    ULONG mine = extra[3] & 0xffUL & ~(DT_KEEP | DT_DROP);
    LONG st;
    int tries;

    for (tries = 0; tries < 2; tries++) {
        memset(f->answer, 0, sizeof f->answer);
        buf[3].ob_Data = f->answer;
        buf[3].ob_Length = sizeof f->answer;
        if (f->kept) {
            buf[0].ob_Data = NULL;
            buf[0].ob_Length = 0;
            extra[3] = mine | DT_KEPT(f->kept) | (last ? DT_DROP : 0);
        } else {
            buf[0].ob_Data = (APTR)f->data;
            buf[0].ob_Length = f->size;
            extra[3] = mine | (last ? 0 : DT_KEEP);
        }
        st = dt_service_call(s, op, arg, flags | 8, buf, extra, result, aux);
        if (st == DT_GONE && f->kept) {
            f->kept = 0;               /* the host let it go: once more, with the file */
            continue;
        }
        if (last)
            f->kept = 0;
        else if (!f->kept && st == OSERR_OK)
            f->kept = get32(f->answer);  /* 0 from a host that does not keep files */
        break;
    }
    extra[3] = mine;
    buf[3].ob_Data = NULL;
    buf[3].ob_Length = 0;
    return st;
}

void dt_file_release(struct dt_service *s, struct dt_file *f)
{
    struct OSBuffer buf[4];
    ULONG extra[4] = { 0, 0, 0, 0 };

    if (f->kept && s->open) {
        memset(buf, 0, sizeof buf);
        dt_service_call(s, DT_RELEASE, f->kept, 0, buf, extra, NULL, NULL);
    }
    f->kept = 0;
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
