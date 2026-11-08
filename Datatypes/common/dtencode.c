/*
 * gfopenimage datatypes: writing through media.encode/1 (dtencode.h).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <string.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <datatypes/datatypes.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "dtlib.h"
#include "dtencode.h"

/* Pieces of the file asked for at once. */
#define PIECE (128UL * 1024)
#define SMALL_PIECE (16UL * 1024)
/* The formats a FORMATS answer can list. */
#define MAX_FORMATS 16

/* A service status as an IoErr() code. */
static LONG ioerr(LONG status)
{
    switch (status) {
    case OSERR_OK:
        return 0;
    case OSERR_NOSERVICE:
    case OSERR_LOST:
        return ERROR_NOT_IMPLEMENTED;          /* the card or the Cradle went */
    case OSERR_CANCELLED:
        return ERROR_BREAK;
    default:
        return DTERROR_COULDNT_SAVE;           /* refused, or FFmpeg failed */
    }
}

static LONG call(struct dt_encoder *e, UWORD op, ULONG arg, ULONG flags, const struct OSBuffer buf[4],
                 const ULONG extra[4], ULONG *result, ULONG *aux)
{
    return dt_service_call(&e->svc, op, arg, flags, buf, extra, result, aux);
}

LONG dt_encode_begin(struct dt_encoder *e, const struct me_settings *s)
{
    struct OSBuffer buf[4];
    ULONG extra[4] = { 0, 0, 0, 0 }, count = 0;
    UBYTE list[MAX_FORMATS * 4], settings[ME_SETTINGS_SIZE];
    LONG st;

    memset(e, 0, sizeof *e);
    if (!dt_service_open(&e->svc, ME_SERVICE))
        return ERROR_NOT_IMPLEMENTED;          /* no card and no paired Cradle */
    memset(buf, 0, sizeof buf);
    buf[1].ob_Data = list;
    buf[1].ob_Length = sizeof list;
    st = call(e, ME_FORMATS, 0, 2, buf, extra, &count, NULL);
    if (st != OSERR_OK && st != OSERR_TOOSMALL)
        return ioerr(st);
    if (count > MAX_FORMATS)
        count = MAX_FORMATS;
    if (!me_offered(list, count, s->format))
        return ERROR_NOT_IMPLEMENTED;          /* its FFmpeg can't write this one */
    me_pack_settings(settings, s);
    memset(buf, 0, sizeof buf);
    buf[0].ob_Data = settings;
    buf[0].ob_Length = sizeof settings;
    if ((st = call(e, ME_BEGIN, 0, 0, buf, extra, &e->job, NULL)) != OSERR_OK)
        e->job = 0;
    return ioerr(st);
}

LONG dt_encode_put(struct dt_encoder *e, ULONG stream, const void *data, ULONG len)
{
    struct OSBuffer buf[4];
    ULONG extra[4] = { stream, 0, 0, 0 };

    if (!e->job)
        return DTERROR_COULDNT_SAVE;
    memset(buf, 0, sizeof buf);
    buf[0].ob_Data = (APTR)data;
    buf[0].ob_Length = len;
    return ioerr(call(e, ME_PUT, e->job, 0, buf, extra, NULL, NULL));
}

LONG dt_encode_finish(struct dt_encoder *e, BPTR fh)
{
    struct OSBuffer buf[4];
    ULONG extra[4] = { 0, 0, 0, 0 }, size = 0, working = 0, got = 0, offset = 0, piece = PIECE;
    UBYTE info[ME_INFO_SIZE], *data;
    LONG st;

    if (!e->job)
        return DTERROR_COULDNT_SAVE;
    if (!fh)
        return ERROR_REQUIRED_ARG_MISSING;
    memset(buf, 0, sizeof buf);
    buf[1].ob_Data = info;
    buf[1].ob_Length = sizeof info;
    do {
        if (SetSignal(0, 0) & SIGBREAKF_CTRL_C) {
            SetSignal(0, SIGBREAKF_CTRL_C);
            return ERROR_BREAK;
        }
        if ((st = call(e, ME_FINISH, e->job, 2, buf, extra, &size, &working)) != OSERR_OK)
            return ioerr(st);
    } while (working == ME_AUX_WORKING);
    if (!size)
        return DTERROR_COULDNT_SAVE;
    if (piece > size)
        piece = size;
    if (!(data = AllocVec(piece, MEMF_ANY))) {
        piece = SMALL_PIECE < size ? SMALL_PIECE : size;
        if (!(data = AllocVec(piece, MEMF_ANY)))
            return ERROR_NO_FREE_STORE;
    }
    st = 0;
    while (offset < size) {
        buf[1].ob_Data = data;
        buf[1].ob_Length = piece;
        extra[0] = offset;
        if ((st = ioerr(call(e, ME_READ, e->job, 2, buf, extra, &got, NULL))) != 0)
            break;
        if (!got || got > piece) {
            st = DTERROR_COULDNT_SAVE;         /* the file ended early */
            break;
        }
        if (Write(fh, data, got) != (LONG)got) {
            st = IoErr() ? IoErr() : ERROR_DISK_FULL;
            break;
        }
        offset += got;
    }
    FreeVec(data);
    return st;
}

void dt_encode_end(struct dt_encoder *e)
{
    if (e->job) {
        struct OSBuffer buf[4];
        ULONG extra[4] = { 0, 0, 0, 0 };
        memset(buf, 0, sizeof buf);
        call(e, ME_CLOSE, e->job, 0, buf, extra, NULL, NULL);
        e->job = 0;
    }
    if (e->svc.io)
        dt_service_close(&e->svc);
}
