/*
 * gfopenimage datatypes: work handed to a named service ("media.decode/1")
 * through gfopenservice.device, on the services card or a paired Cradle.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef DTSERVICE_H
#define DTSERVICE_H

#include <exec/types.h>
#include <exec/ports.h>
#include "devices/gfopenservice.h"

struct dt_service {
    struct MsgPort *port;
    struct OSRequest *io;
    UWORD handle;
    BOOL open;
};

/* Opens the device and the service; FALSE when neither a card nor a paired
 * Cradle offers it (or the device is not installed). The caller's task owns
 * the reply port, so open, call and close from the same task. */
BOOL dt_service_open(struct dt_service *s, const char *name);
/* One request; buffer i is written by the service when flags bit i is set.
 * Returns the service's status (0 is OK, OSERR_* otherwise). */
LONG dt_service_call(struct dt_service *s, UWORD op, ULONG arg, ULONG flags,
                     const struct OSBuffer buf[4], const ULONG extra[4], ULONG *result, ULONG *aux);
void dt_service_close(struct dt_service *s);

/* The largest picture side to ask a service for: ENV:GFOpenImage/MaxSide, else
 * 4096. Bigger pictures come back scaled down to fit. */
ULONG dt_max_side(void);

/* The file name's extension as a service's hint: up to four letters in
 * capitals, space-padded, big-endian ("photo.cr2" -> 'CR2 '); 0 when none. */
ULONG dt_name_hint(CONST_STRPTR name);

#endif
