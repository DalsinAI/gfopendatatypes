/*
 * webm.datatype: a small WebM (Matroska) reader. It finds the first VP8 or
 * VP9 video track and lists where each of its frames lies in the file, so
 * frames can be read and decoded on demand. No floating point is used.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef WEBM_DEMUX_H
#define WEBM_DEMUX_H

#include <stdint.h>

enum { WEBM_CODEC_NONE, WEBM_CODEC_VP8, WEBM_CODEC_VP9 };

/* The widest and tallest picture the reader takes. A track that says more
 * is skipped: width x height must fit in memory and in 32 bits. */
#define WEBM_MAX_SIDE 8192

typedef struct {
    uint32_t offset;     /* where the frame's data starts in the file */
    uint32_t size;       /* its length in bytes */
    uint32_t timeMs;     /* presentation time, milliseconds */
    uint32_t keyFrame;   /* 1 when decoding can start at this frame */
} WebMFrame;

/* Reads bytes at an offset of the source; returns how many it read. */
typedef long (*WebMReadFn)(void *handle, uint32_t offset, void *buffer, uint32_t length);

typedef struct {
    int codec;                 /* WEBM_CODEC_VP8 or WEBM_CODEC_VP9 */
    uint32_t width, height;    /* the track's pixel size */
    uint32_t frameDurationNs;  /* DefaultDuration, 0 when the file gives none */
    uint32_t durationMs;       /* from the frames' times */
    uint32_t frameCount;
    WebMFrame *frames;         /* frameCount entries, in file order */
} WebMInfo;

/* Fills info from a source of fileSize bytes. Returns 0 on success, or a
 * negative error: -1 not WebM, -2 no VP8/VP9 video track (of at most
 * WEBM_MAX_SIDE each way), -3 no memory, -4 damaged file. info->frames is
 * allocated with malloc(). */
int webm_parse(WebMReadFn read, void *handle, uint32_t fileSize, WebMInfo *info);
void webm_free(WebMInfo *info);

#endif
