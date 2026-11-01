/*
 * webm.datatype: a read cache for the reader (webm_demux.c).
 *
 * The reader asks for an element's ID, then its size, then a few bytes of
 * its body, each at its own offset: on AmigaDOS every one of those was a
 * Seek and a Read, two packets to the file system for one to twelve bytes,
 * three or four times for every block of the clip. webm_cache_read keeps
 * WEBM_CACHE_SIZE bytes of the file and answers reads that fit in them
 * from there; a read it doesn't hold refills the cache from the read's
 * offset with one call of the source's own read, and a read longer than
 * the cache goes straight to the source. It answers exactly what the
 * source would: the same bytes, and as many of them (fewer at the end of
 * the file, -1 when the source fails).
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef WEBM_READCACHE_H
#define WEBM_READCACHE_H

#include <string.h>
#include "webm_demux.h"

#define WEBM_CACHE_SIZE 4096

typedef struct {
    WebMReadFn read;          /* the source */
    void *handle;
    uint32_t start, length;   /* buf holds length bytes from offset start */
    unsigned char buf[WEBM_CACHE_SIZE];
} WebMReadCache;

static void webm_cache_init(WebMReadCache *c, WebMReadFn read, void *handle)
{
    c->read = read;
    c->handle = handle;
    c->start = 0;
    c->length = 0;
}

/* A WebMReadFn: the handle is a WebMReadCache. */
static long webm_cache_read(void *handle, uint32_t offset, void *buffer, uint32_t length)
{
    WebMReadCache *c = (WebMReadCache *)handle;
    uint32_t at;
    if (length > sizeof c->buf)
        return c->read(c->handle, offset, buffer, length);
    if (offset < c->start || offset - c->start > c->length || length > c->length - (offset - c->start)) {
        long got = c->read(c->handle, offset, c->buf, sizeof c->buf);
        if (got < 0) {
            c->length = 0;
            return got;
        }
        c->start = offset;
        c->length = (uint32_t)got;
        if (length > c->length)
            length = c->length;
    }
    at = offset - c->start;
    memcpy(buffer, c->buf + at, length);
    return (long)length;
}

#endif
