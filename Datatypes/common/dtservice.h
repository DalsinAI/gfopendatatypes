/*
 * gfopenimage datatypes: work handed to a named service ("media.decode/1")
 * through gfopenservice.device, on the services card or a paired Cradle.
 *
 * One connection an object: dt_service_open() opens the device and the
 * service once (OSCMD_OPEN is a trip to the card or the Cradle), and the
 * object keeps the connection for its life. Any of the object's tasks may
 * call on it (the program's task in OM_NEW, animation.datatype's loader in
 * ADTM_LOADFRAME): each call takes a signal of the calling task for as long
 * as it waits, so the connection belongs to no task. The caller keeps two
 * tasks from calling at once (the classes hold their own semaphore).
 *
 * Read-ahead: dt_service_send() sends one request and returns at once (the
 * next frame or page, asked for while this one shows); dt_service_wait()
 * takes its answer, from the task that sent it or any other.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef DTSERVICE_H
#define DTSERVICE_H

#include <exec/types.h>
#include <exec/ports.h>
#include <exec/tasks.h>
#include "devices/gfopenservice.h"

struct dt_service {
    struct MsgPort *port;      /* no signal of its own: the caller's, for each call */
    struct OSRequest *io;
    struct MsgPort *aheadPort; /* the read-ahead's: made at the first dt_service_send */
    struct OSRequest *ahead;
    struct Task *aheadTask;    /* the task that sent it, and the signal it waits on */
    BYTE aheadSig;
    BOOL sent;                 /* a read-ahead is out */
    UWORD handle;
    BOOL open;
    ULONG where;               /* OSWHERE_*, from OSCMD_WHERE; 0 not asked yet or unknown */
};

/* Opens the device and the service; FALSE when neither a card nor a paired
 * Cradle offers it (or the device is not installed). */
BOOL dt_service_open(struct dt_service *s, const char *name);
/* One request; buffer i is written by the service when flags bit i is set.
 * Returns the service's status (0 is OK, OSERR_* otherwise). */
LONG dt_service_call(struct dt_service *s, UWORD op, ULONG arg, ULONG flags,
                     const struct OSBuffer buf[4], const ULONG extra[4], ULONG *result, ULONG *aux);
/* Sends one request without waiting (one at a time); FALSE when it could
 * not be sent (one is out already, no memory, no signal). The buffers must
 * stay until dt_service_wait(). */
BOOL dt_service_send(struct dt_service *s, UWORD op, ULONG arg, ULONG flags,
                     const struct OSBuffer buf[4], const ULONG extra[4]);
/* The answer to the request out (its status), waiting for it when it has
 * not come yet; OSERR_BADREQUEST when none is out. */
LONG dt_service_wait(struct dt_service *s, ULONG *result, ULONG *aux);
/* Where the service runs: OSWHERE_CARD (this machine's own host) or
 * OSWHERE_LAN, asked once; 0 when the device can't say. */
ULONG dt_service_where(struct dt_service *s);
/* Closes the service (a read-ahead still out is cancelled first). */
void dt_service_close(struct dt_service *s);

/* Files sent once (gfopenservice docs/MEDIA_DECODE.md, "Files sent once";
 * media.decode/1 and doc.render/1): extra[3] of PROBE, DECODE, NDECODE,
 * VOPEN, RENDER and TEXT. With DT_KEEP the host keeps buf0's file and
 * writes its handle into buf3 (4 bytes, flags bit 3, zeroed by the caller:
 * an older host leaves it 0, and the file goes on being sent); later calls
 * leave buf0 empty and name the file with DT_KEPT(handle). DT_GONE: the
 * host has let it go (pushed out by newer files, or restarted), so the
 * file is sent again. DT_RELEASE (arg the handle) lets it go. */
#define DT_KEEP      2UL
#define DT_DROP      4UL
#define DT_KEPT(h)   ((ULONG)(h) << 8)
#define DT_GONE      (-7)
#define DT_RELEASE   34

/* A file for calls that send it once: the first call sends it with
 * DT_KEEP, later ones name it (DT_KEPT); a host that lets it go gets it
 * again, and an older host keeps getting it whole, as before. */
struct dt_file {
    const UBYTE *data;
    ULONG size;
    ULONG kept;                /* the host's handle for it; 0 none */
    UBYTE answer[4];           /* buf3: where the host writes the handle */
};

/* One call with the file as buf0 (buf[0] and buf[3] and flags bit 3 are
 * set here; extra[3]'s other bits are the caller's). last: the file is
 * not needed after this call (it is let go with it, or not kept). */
LONG dt_file_call(struct dt_service *s, struct dt_file *f, UWORD op, ULONG arg, ULONG flags,
                  struct OSBuffer buf[4], ULONG extra[4], ULONG *result, ULONG *aux, BOOL last);
/* Lets the host's copy go (DT_RELEASE), when there is one. */
void dt_file_release(struct dt_service *s, struct dt_file *f);

/* The largest picture side to ask a service for: ENV:GFOpenImage/MaxSide, else
 * 4096. Bigger pictures come back scaled down to fit. */
ULONG dt_max_side(void);

/* The file name's extension as a service's hint: up to four letters in
 * capitals, space-padded, big-endian ("photo.cr2" -> 'CR2 '); 0 when none. */
ULONG dt_name_hint(CONST_STRPTR name);

#endif
