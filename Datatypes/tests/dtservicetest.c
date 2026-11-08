/*
 * common/dtservice.c on the host (tests/test-dtservice.sh), under
 * AddressSanitizer and UBSan: the file itself, with the Amiga's names from
 * tests/dtservice-host.h and fakes here for exec (tasks and their signals,
 * ports, DoIO, SendIO, WaitIO, AbortIO) and for gfopenservice.device and a
 * service that keeps files as media.decode/1 does (or, as an older host,
 * does not).
 *   - one connection serves any task: each call signals the task that
 *     waits, with a signal it has allocated, and frees it after;
 *   - a read-ahead (dt_service_send) is taken by dt_service_wait in the task
 *     that sent it, or in another one after the sender is gone (the reply
 *     then signals the waiter, never the gone task), and dt_service_close
 *     cancels one still out;
 *   - files sent once (dt_file_call): the first call sends the file with
 *     KEEP, the others name it, the last drops it; an older host gets it
 *     every time; a host that let it go (GONE) gets it again; and
 *     dt_file_release lets it go;
 *   - without the device or the service nothing opens and nothing leaks.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "dtservice.c"

#include <stdio.h>
#include <stdlib.h>

static int failures, checks;

static void check(int ok, const char *what)
{
    checks++;
    if (!ok) {
        printf("FAIL %s\n", what);
        failures++;
    }
}

/* ---- memory ---------------------------------------------------------------- */

static int blocks;

APTR AllocVec(ULONG size, ULONG flags)
{
    void *p = calloc(1, size ? size : 1);
    (void)flags;
    if (p)
        blocks++;
    return p;
}

void FreeVec(APTR p)
{
    if (p) {
        blocks--;
        free(p);
    }
}

/* ---- tasks and signals ----------------------------------------------------- */

static struct Task tasks[3];
static ULONG sigsOf[3];               /* the bits each task has allocated */
static int gone[3];                   /* the task has ended */
static int cur;                       /* the task running */
static int signalled[3];
static int disabled;

struct Task *FindTask(CONST_STRPTR name)
{
    (void)name;
    return &tasks[cur];
}

static int taskIndex(void *t)
{
    int i;
    for (i = 0; i < 3; i++)
        if (t == &tasks[i])
            return i;
    return -1;
}

BYTE AllocSignal(LONG bit)
{
    int b;
    (void)bit;
    for (b = 16; b < 32; b++)
        if (!(sigsOf[cur] & 1UL << b)) {
            sigsOf[cur] |= 1UL << b;
            return (BYTE)b;
        }
    return -1;
}

void FreeSignal(LONG bit)
{
    check(bit >= 0 && (sigsOf[cur] & 1UL << bit), "FreeSignal of a signal the task has not allocated");
    if (bit >= 0)
        sigsOf[cur] &= ~(1UL << bit);
}

void Disable(void) { disabled++; }
void Enable(void) { disabled--; }

/* ---- the device and the service -------------------------------------------- */

static struct { int noDevice, noService, oldHost, forget; ULONG where; } cfg;
static struct {
    int opens, closes, calls, releases, sentWhole, sentEmpty, cancelled;
    ULONG kept;                        /* the one file the host keeps; 0 none */
    ULONG serial;
    UWORD lastOp;
} host;
static const UBYTE fileBytes[] = "a module of some size, as the Amiga sends it";
static UBYTE devDummy;

static void put32(UBYTE *p, ULONG v)
{
    p[0] = (UBYTE)(v >> 24); p[1] = (UBYTE)(v >> 16); p[2] = (UBYTE)(v >> 8); p[3] = (UBYTE)v;
}

/* The fake service's answer to one OSCMD_CALL. */
static void serve(struct OSRequest *io)
{
    ULONG e3 = io->os_Extra[3], named = e3 >> 8;
    int haveFile = 0;

    host.calls++;
    host.lastOp = io->os_Op;
    io->os_Result = io->os_Aux = 0;
    if (io->os_Op == DT_RELEASE) {
        host.releases++;
        io->os_Status = (!cfg.oldHost && host.kept && io->os_Arg == host.kept) ? 0 : DT_GONE;
        if (!io->os_Status)
            host.kept = 0;
        if (cfg.oldHost)
            io->os_Status = OSERR_BADREQUEST;
        return;
    }
    if (io->os_Buf[0].ob_Length) {
        haveFile = io->os_Buf[0].ob_Length == sizeof fileBytes && !memcmp(io->os_Buf[0].ob_Data, fileBytes, sizeof fileBytes);
        host.sentWhole++;
    } else if (named && !cfg.oldHost) {
        host.sentEmpty++;
        if (named != host.kept) {
            io->os_Status = DT_GONE;
            return;
        }
        haveFile = 1;
    }
    if (!haveFile) {
        io->os_Status = OSERR_BADREQUEST;
        return;
    }
    io->os_Status = 0;
    io->os_Result = 1234;
    if (io->os_Buf[1].ob_Data && io->os_Buf[1].ob_Length)
        memset(io->os_Buf[1].ob_Data, 0x5A, io->os_Buf[1].ob_Length);
    if (cfg.oldHost)
        return;                         /* an older host: extra[3] means nothing to it */
    if ((e3 & DT_KEEP) && (io->os_Flags & 8) && io->os_Buf[3].ob_Length >= 4) {
        if (!named)
            host.kept = 0x100 | ++host.serial;
        put32(io->os_Buf[3].ob_Data, host.kept);
    }
    if ((e3 & DT_DROP) && named == host.kept)
        host.kept = 0;
    if (cfg.forget) {
        cfg.forget = 0;                 /* the host lets the file go after this call */
        host.kept = 0;
    }
}

/* The device's reply: the request back on its port, and the port's task
 * signalled when the port asks for it. */
static void reply(struct IORequest *io)
{
    struct MsgPort *p = io->io_Message.mn_ReplyPort;
    int t;
    io->io_Message.mn_Node.ln_Type = NT_REPLYMSG;
    io->io_Message.mn_Node.ln_Succ = (struct Node *)p;   /* "on the port's list" */
    if (p->mp_Flags == PA_SIGNAL) {
        t = taskIndex(p->mp_SigTask);
        check(t >= 0, "a reply signals no task");
        if (t >= 0) {
            check(!gone[t], "a reply signals a task that has ended");
            check((sigsOf[t] & 1UL << p->mp_SigBit) != 0, "a reply signals a signal the task has not allocated");
            signalled[t]++;
        }
    }
}

static void devRun(struct IORequest *io)
{
    struct OSRequest *o = (struct OSRequest *)io;
    switch (io->io_Command) {
    case OSCMD_OPEN:
        o->os_Status = cfg.noService ? OSERR_NOSERVICE : 0;
        o->os_Result = 7;
        host.opens += !cfg.noService;
        break;
    case OSCMD_CLOSE:
        host.closes++;
        o->os_Status = 0;
        break;
    case OSCMD_WHERE:
        o->os_Status = 0;
        o->os_Result = cfg.where;
        break;
    case OSCMD_CALL:
        check(o->os_Service == 7, "a call on another handle");
        if (o->os_Private[1]) {
            o->os_Status = OSERR_CANCELLED;
            host.cancelled++;
        } else
            serve(o);
        break;
    default:
        io->io_Error = -3;
    }
    io->io_Error = 0;
    reply(io);
}

static struct IORequest *pending;

struct IORequest *CreateIORequest(struct MsgPort *port, ULONG size)
{
    struct IORequest *io = AllocVec(size, MEMF_CLEAR);
    if (io)
        io->io_Message.mn_ReplyPort = port;
    return io;
}

void DeleteIORequest(struct IORequest *io) { FreeVec(io); }

BYTE OpenDevice(CONST_STRPTR name, ULONG unit, struct IORequest *io, ULONG flags)
{
    (void)unit; (void)flags;
    if (cfg.noDevice || strcmp(name, OPENSERVICE_NAME))
        return -1;
    io->io_Device = (struct Device *)&devDummy;
    return 0;
}

void CloseDevice(struct IORequest *io) { io->io_Device = NULL; }

/* Removes a replied request from its port, as WaitIO does. */
static void take(struct IORequest *io)
{
    check(io->io_Message.mn_Node.ln_Type == NT_REPLYMSG, "a request taken before its reply");
    io->io_Message.mn_Node.ln_Succ = NULL;
}

BYTE DoIO(struct IORequest *io)
{
    struct MsgPort *p = io->io_Message.mn_ReplyPort;
    check(p->mp_Flags == PA_SIGNAL && p->mp_SigTask == &tasks[cur], "DoIO on a port that would not signal its caller");
    io->io_Message.mn_Node.ln_Type = NT_MESSAGE;
    devRun(io);
    take(io);
    return io->io_Error;
}

void SendIO(struct IORequest *io)
{
    check(!pending, "two read-aheads out at once");
    io->io_Message.mn_Node.ln_Type = NT_MESSAGE;
    ((struct OSRequest *)io)->os_Private[1] = 0;
    pending = io;
}

/* The device answers the request out (when the test lets it, or at WaitIO). */
static void deviceRuns(void)
{
    if (pending) {
        struct IORequest *io = pending;
        pending = NULL;
        devRun(io);
    }
}

BYTE WaitIO(struct IORequest *io)
{
    struct MsgPort *p = io->io_Message.mn_ReplyPort;
    if (io->io_Message.mn_Node.ln_Type != NT_REPLYMSG) {
        check(p->mp_Flags == PA_SIGNAL && p->mp_SigTask == &tasks[cur], "WaitIO on a port that would not signal the waiter");
        if (io == pending)
            deviceRuns();
    }
    take(io);
    return io->io_Error;
}

struct IORequest *CheckIO(struct IORequest *io)
{
    if (io == pending)
        deviceRuns();
    return io->io_Message.mn_Node.ln_Type == NT_REPLYMSG ? io : NULL;
}

LONG AbortIO(struct IORequest *io)
{
    if (io == pending)
        ((struct OSRequest *)io)->os_Private[1] = 1;
    return 0;
}

LONG GetVar(CONST_STRPTR name, STRPTR buffer, LONG size, ULONG flags)
{
    (void)name; (void)buffer; (void)size; (void)flags;
    return -1;
}

/* ---- the cases ------------------------------------------------------------- */

static void reset(void)
{
    memset(&cfg, 0, sizeof cfg);
    memset(&host, 0, sizeof host);
    memset(sigsOf, 0, sizeof sigsOf);
    memset(gone, 0, sizeof gone);
    memset(signalled, 0, sizeof signalled);
    cur = 0;
    cfg.where = OSWHERE_CARD;
}

static void balanced(const char *what)
{
    char line[160];
    snprintf(line, sizeof line, "%s: every signal freed, every block freed, interrupts on", what);
    check(!sigsOf[0] && !sigsOf[1] && !sigsOf[2] && !blocks && !disabled && !pending, line);
}

/* PROBE, then DECODE n times, the last one last. */
static void fileCalls(struct dt_service *s, struct dt_file *f, int n, LONG *worst)
{
    struct OSBuffer buf[4];
    ULONG extra[4] = { 2, 22050, 0, 0 }, result;
    UBYTE out[24];
    int i;
    memset(buf, 0, sizeof buf);
    buf[1].ob_Data = out;
    buf[1].ob_Length = sizeof out;
    *worst = dt_file_call(s, f, 1, 0, 2, buf, extra, &result, NULL, FALSE);
    for (i = 0; i < n; i++) {
        LONG st = dt_file_call(s, f, 2, i, 2, buf, extra, &result, NULL, i == n - 1);
        if (st)
            *worst = st;
        check(extra[3] == 0, "dt_file_call leaves extra[3] as it found it");
    }
}

int main(void)
{
    struct dt_service s;
    struct dt_file f;
    struct OSBuffer buf[4];
    ULONG extra[4] = { 0, 0, 0, 0 }, result = 0;
    UBYTE out[16];
    LONG st;

    /* One connection, two tasks calling in turn. */
    reset();
    check(dt_service_open(&s, "media.decode/1"), "the service opens");
    memset(buf, 0, sizeof buf);
    cur = 1;
    st = dt_service_call(&s, 9, 0, 0, buf, extra, NULL, NULL);
    cur = 0;
    check(dt_service_where(&s) == OSWHERE_CARD, "OSCMD_WHERE: the card");
    check(signalled[0] >= 2 && signalled[1] == 1, "each call signals the task that waits");
    check(!sigsOf[0] && !sigsOf[1], "the calls' signals freed");
    dt_service_close(&s);
    check(host.opens == 1 && host.closes == 1, "opened and closed once");
    balanced("two tasks");

    /* A read-ahead, taken by the task that sent it. */
    reset();
    dt_service_open(&s, "media.decode/1");
    f.data = fileBytes; f.size = sizeof fileBytes; f.kept = 0;
    memset(buf, 0, sizeof buf);
    buf[0].ob_Data = (APTR)fileBytes;
    buf[0].ob_Length = sizeof fileBytes;
    buf[1].ob_Data = out;
    buf[1].ob_Length = sizeof out;
    cur = 1;
    check(dt_service_send(&s, 2, 5, 2, buf, extra), "a read-ahead goes out");
    check(!dt_service_send(&s, 2, 6, 2, buf, extra), "a second read-ahead is refused while one is out");
    check(s.sent && sigsOf[1], "it holds a signal of the sender while out");
    deviceRuns();
    st = dt_service_wait(&s, &result, NULL);
    check(st == 0 && result == 1234 && out[0] == 0x5A, "the read-ahead's answer");
    check(signalled[1] == 1 && !sigsOf[1], "it signalled its sender, whose signal is free again");
    check(dt_service_wait(&s, NULL, NULL) == OSERR_BADREQUEST, "a wait with none out");
    cur = 0;
    dt_service_close(&s);
    balanced("a read-ahead");

    /* Sent by a task that then ends; taken by another (the object going). */
    reset();
    dt_service_open(&s, "media.decode/1");
    cur = 1;
    dt_service_send(&s, 2, 5, 2, buf, extra);
    gone[1] = 1;
    sigsOf[1] = 0;                     /* its signals went with it */
    cur = 2;
    st = dt_service_wait(&s, &result, NULL);
    check(st == 0 && result == 1234, "the read-ahead taken by another task");
    check(signalled[2] == 1 && signalled[1] == 0, "the reply signalled the task waiting, not the one gone");
    cur = 0;
    dt_service_close(&s);
    balanced("a read-ahead taken by another task");

    /* Closed with a read-ahead out: cancelled, and waited for. */
    reset();
    dt_service_open(&s, "media.decode/1");
    dt_service_send(&s, 2, 5, 2, buf, extra);
    dt_service_close(&s);
    check(host.cancelled == 1 && host.closes == 1, "close cancels the read-ahead out, then closes");
    balanced("closed with a read-ahead out");

    /* Files sent once. */
    reset();
    dt_service_open(&s, "media.decode/1");
    f.kept = 0;
    fileCalls(&s, &f, 4, &st);
    check(st == 0, "files sent once: every call answered");
    check(host.sentWhole == 1 && host.sentEmpty == 4, "files sent once: the file went up once, then was named");
    check(!f.kept && !host.kept, "files sent once: the last call let it go");
    dt_service_close(&s);
    balanced("files sent once");

    reset();
    cfg.oldHost = 1;
    dt_service_open(&s, "media.decode/1");
    f.kept = 0;
    fileCalls(&s, &f, 4, &st);
    check(st == 0 && host.sentWhole == 5 && host.sentEmpty == 0 && !f.kept,
          "an older host: the file sent whole every time, as before");
    dt_file_release(&s, &f);
    check(host.releases == 0, "an older host: nothing kept, nothing released");
    dt_service_close(&s);
    balanced("an older host");

    reset();
    dt_service_open(&s, "media.decode/1");
    f.kept = 0;
    cfg.forget = 1;                    /* the host lets the file go after PROBE */
    fileCalls(&s, &f, 3, &st);
    check(st == 0 && host.sentWhole == 2, "a file the host let go (GONE) is sent again, and kept again");
    dt_service_close(&s);
    balanced("GONE");

    reset();
    dt_service_open(&s, "media.decode/1");
    f.kept = 0;
    memset(buf, 0, sizeof buf);
    buf[1].ob_Data = out;
    buf[1].ob_Length = sizeof out;
    dt_file_call(&s, &f, 1, 0, 2, buf, extra, NULL, NULL, FALSE);
    check(f.kept && host.kept == f.kept, "PROBE keeps the file");
    dt_file_release(&s, &f);
    check(host.releases == 1 && !host.kept && !f.kept && host.lastOp == DT_RELEASE, "dt_file_release lets it go");
    dt_service_close(&s);
    balanced("release");

    /* Nothing to open. */
    reset();
    cfg.noDevice = 1;
    check(!dt_service_open(&s, "media.decode/1"), "no device: no service");
    balanced("no device");
    reset();
    cfg.noService = 1;
    check(!dt_service_open(&s, "media.decode/1"), "no card or Cradle: no service");
    balanced("no service");

    printf("dtservicetest: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
