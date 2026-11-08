/*
 * gfopenimage datatypes: writing a file through media.encode/1 (HEIC, M4A,
 * AAC, WMA, HEVC, WMV: the service's FFmpeg, never code in the pack). A
 * datatype opens a job, sends the object's pixels, samples or frames in
 * pieces, and the file comes back into DTM_WRITE's file handle.
 *
 *     struct dt_encoder e;
 *     if (!(err = dt_encode_begin(&e, &settings))) {
 *         ... err = dt_encode_put(&e, ME_STREAM_PICTURE, rows, bytes) ...
 *         if (!err)
 *             err = dt_encode_finish(&e, w->dtw_FileHandle);
 *     }
 *     dt_encode_end(&e);
 *     if (err) { SetIoErr(err); return FALSE; }
 *
 * Open, call and end from one task (the reply port is its own).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef DTENCODE_H
#define DTENCODE_H

#include <exec/types.h>
#include <dos/dos.h>
#include "dtservice.h"
#include "encodemsg.h"

struct dt_encoder {
    struct dt_service svc;
    ULONG job;                     /* the service's, 0 when none */
};

/* Opens media.encode/1, checks with FORMATS that it writes s->format, and
 * begins a job. 0, or an IoErr() code: ERROR_NOT_IMPLEMENTED when no card
 * or paired Cradle offers the service, or its FFmpeg can't write that
 * format; DTERROR_COULDNT_SAVE when it refuses the settings. Call
 * dt_encode_end() whatever it returns. */
LONG dt_encode_begin(struct dt_encoder *e, const struct me_settings *s);

/* The next bytes of a stream (ME_STREAM_PICTURE or ME_STREAM_SOUND). 0, or an IoErr() code. */
LONG dt_encode_put(struct dt_encoder *e, ULONG stream, const void *data, ULONG len);

/* FFmpeg makes the file (FINISH, sent again while the host says it is at
 * work; Ctrl-C to the task stops waiting, ERROR_BREAK), then it is read
 * back in pieces and written to fh. 0, or an IoErr() code. */
LONG dt_encode_finish(struct dt_encoder *e, BPTR fh);

/* Closes the job and the service. */
void dt_encode_end(struct dt_encoder *e);

#endif
