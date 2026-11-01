/*
 * webm.datatype: a small WebM (Matroska) reader (see webm_demux.h).
 *
 * It walks the EBML elements once: the Info element for the time scale,
 * Tracks for the first VP8 or VP9 video track, and every Cluster's
 * SimpleBlock and BlockGroup for that track's frames. Clusters of unknown
 * size (live recordings) end at the next top-level element.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include <stdlib.h>
#include <string.h>

#include "webm_demux.h"

#define ID_EBML           0x1A45DFA3u
#define ID_DOCTYPE        0x4282u
#define ID_SEGMENT        0x18538067u
#define ID_INFO           0x1549A966u
#define ID_TIMECODESCALE  0x2AD7B1u
#define ID_TRACKS         0x1654AE6Bu
#define ID_TRACKENTRY     0xAEu
#define ID_TRACKNUMBER    0xD7u
#define ID_TRACKTYPE      0x83u
#define ID_CODECID        0x86u
#define ID_DEFAULTDURATION 0x23E383u
#define ID_VIDEO          0xE0u
#define ID_PIXELWIDTH     0xB0u
#define ID_PIXELHEIGHT    0xBAu
#define ID_CLUSTER        0x1F43B675u
#define ID_TIMECODE       0xE7u
#define ID_SIMPLEBLOCK    0xA3u
#define ID_BLOCKGROUP     0xA0u
#define ID_BLOCK          0xA1u
#define ID_REFERENCEBLOCK 0xFBu
#define ID_CUES           0x1C53BB6Bu
#define ID_SEEKHEAD       0x114D9B74u
#define ID_TAGS           0x1254C367u
#define ID_CHAPTERS       0x1043A770u
#define ID_ATTACHMENTS    0x1941A469u

#define SIZE_UNKNOWN 0xFFFFFFFFu

typedef struct {
    WebMReadFn read;
    void *handle;
    uint32_t fileSize;
} Source;

typedef struct {
    uint32_t track;
    WebMFrame frame;
} Block;

typedef struct {
    Block *blocks;
    uint32_t count, capacity;
    uint32_t timecodeScale;   /* nanoseconds per timecode unit */
    uint32_t videoTrack;
    int codec;
    uint32_t width, height, defaultDuration;
    uint32_t audioTracks;
} Parser;

static int readBytes(Source *s, uint32_t pos, void *buffer, uint32_t length)
{
    if (pos > s->fileSize || length > s->fileSize - pos)
        return 0;
    return s->read(s->handle, pos, buffer, length) == (long)length;
}

/* An element ID: 1 to 4 bytes, the length marker kept in the value. */
static int readId(Source *s, uint32_t *pos, uint32_t *id)
{
    uint8_t b[4];
    int length, i;
    if (!readBytes(s, *pos, b, 1))
        return 0;
    if (b[0] & 0x80)
        length = 1;
    else if (b[0] & 0x40)
        length = 2;
    else if (b[0] & 0x20)
        length = 3;
    else if (b[0] & 0x10)
        length = 4;
    else
        return 0;
    if (length > 1 && !readBytes(s, *pos + 1, b + 1, length - 1))
        return 0;
    *id = 0;
    for (i = 0; i < length; i++)
        *id = (*id << 8) | b[i];
    *pos += length;
    return 1;
}

/* A size: 1 to 8 bytes; all value bits set means "unknown". Sizes beyond
 * 32 bits are refused (the reader handles files under 4 GB). */
static int readVint(const uint8_t *b, int available, uint32_t *value, int *used, int *unknown)
{
    int length = 1, i;
    uint8_t mask = 0x80;
    uint32_t high = 0, v;
    int allOnes;
    if (available < 1)
        return 0;
    while (length <= 8 && !(b[0] & mask)) {
        mask >>= 1;
        length++;
    }
    if (length > 8 || length > available)
        return 0;
    v = b[0] & (mask - 1);
    allOnes = v == (uint32_t)(mask - 1);
    for (i = 1; i < length; i++) {
        high = (high << 8) | (v >> 24);
        v = (v << 8) | b[i];
        if (b[i] != 0xff)
            allOnes = 0;
    }
    *unknown = allOnes;
    if (!allOnes && high)
        return 0;
    *value = allOnes ? SIZE_UNKNOWN : v;
    *used = length;
    return 1;
}

static int readSize(Source *s, uint32_t *pos, uint32_t *size)
{
    uint8_t b[8];
    uint32_t available = s->fileSize - *pos;
    int used, unknown;
    if (*pos >= s->fileSize)
        return 0;
    if (available > 8)
        available = 8;
    if (!readBytes(s, *pos, b, available))
        return 0;
    if (!readVint(b, available, size, &used, &unknown))
        return 0;
    *pos += used;
    return 1;
}

static uint32_t readUint(Source *s, uint32_t pos, uint32_t size)
{
    uint8_t b[8];
    uint32_t v = 0, i;
    if (size > 8 || !readBytes(s, pos, b, size))
        return 0;
    for (i = 0; i < size; i++)
        v = (v << 8) | b[i];
    return v;
}

static int addBlock(Parser *p, uint32_t track, const WebMFrame *frame)
{
    if (p->count == p->capacity) {
        uint32_t capacity = p->capacity ? p->capacity * 2 : 256;
        Block *blocks = realloc(p->blocks, capacity * sizeof(Block));
        if (!blocks)
            return 0;
        p->blocks = blocks;
        p->capacity = capacity;
    }
    p->blocks[p->count].track = track;
    p->blocks[p->count].frame = *frame;
    p->count++;
    return 1;
}

/* A Block or SimpleBlock body: track number, 16-bit time, flags, data. */
static int parseBlock(Source *s, Parser *p, uint32_t pos, uint32_t size, uint32_t clusterTime, int simple, int keyFromGroup)
{
    uint8_t b[12];
    uint32_t track, header, available = size < sizeof b ? size : sizeof b;
    int used, unknown, lacing;
    int16_t relative;
    WebMFrame frame;
    if (!readBytes(s, pos, b, available))
        return 0;
    if (!readVint(b, available, &track, &used, &unknown) || unknown)
        return 0;
    header = used + 3;
    if (header > size)
        return 0;
    relative = (int16_t)((b[used] << 8) | b[used + 1]);
    lacing = (b[used + 2] >> 1) & 3;
    if (lacing)
        return 1; /* laced video is rare in WebM; such blocks are skipped */
    frame.offset = pos + header;
    frame.size = size - header;
    {
        int32_t time = (int32_t)clusterTime + relative;
        uint64_t ns = (uint64_t)(time < 0 ? 0 : time) * p->timecodeScale;
        frame.timeMs = (uint32_t)(ns / 1000000u);
    }
    frame.keyFrame = simple ? (b[used + 2] & 0x80 ? 1 : 0) : keyFromGroup;
    return addBlock(p, track, &frame);
}

static int isTopLevel(uint32_t id)
{
    return id == ID_CLUSTER || id == ID_CUES || id == ID_TAGS || id == ID_SEEKHEAD || id == ID_INFO
        || id == ID_TRACKS || id == ID_CHAPTERS || id == ID_ATTACHMENTS || id == ID_SEGMENT || id == ID_EBML;
}

static int parseCluster(Source *s, Parser *p, uint32_t pos, uint32_t end, uint32_t *next)
{
    uint32_t clusterTime = 0;
    while (pos < end) {
        uint32_t id, size, start = pos;
        if (!readId(s, &pos, &id) || !readSize(s, &pos, &size))
            break;
        if (isTopLevel(id)) {
            /* The end of a cluster of unknown size. */
            *next = start;
            return 1;
        }
        if (size == SIZE_UNKNOWN || size > end - pos)
            return 0;
        if (id == ID_TIMECODE)
            clusterTime = readUint(s, pos, size);
        else if (id == ID_SIMPLEBLOCK) {
            if (!parseBlock(s, p, pos, size, clusterTime, 1, 0))
                return 0;
        } else if (id == ID_BLOCKGROUP) {
            /* A Block without ReferenceBlock is a key frame. */
            uint32_t gpos = pos, gend = pos + size, blockPos = 0, blockSize = 0;
            int key = 1;
            while (gpos < gend) {
                uint32_t gid, gsize;
                if (!readId(s, &gpos, &gid) || !readSize(s, &gpos, &gsize) || gsize > gend - gpos)
                    return 0;
                if (gid == ID_BLOCK) {
                    blockPos = gpos;
                    blockSize = gsize;
                } else if (gid == ID_REFERENCEBLOCK)
                    key = 0;
                gpos += gsize;
            }
            if (blockSize && !parseBlock(s, p, blockPos, blockSize, clusterTime, 0, key))
                return 0;
        }
        pos += size;
    }
    *next = pos;
    return 1;
}

static void parseTrackEntry(Source *s, Parser *p, uint32_t pos, uint32_t end)
{
    uint32_t number = 0, type = 0, width = 0, height = 0, duration = 0;
    char codecId[16];
    codecId[0] = 0;
    while (pos < end) {
        uint32_t id, size;
        if (!readId(s, &pos, &id) || !readSize(s, &pos, &size) || size > end - pos)
            return;
        switch (id) {
        case ID_TRACKNUMBER:
            number = readUint(s, pos, size);
            break;
        case ID_TRACKTYPE:
            type = readUint(s, pos, size);
            break;
        case ID_DEFAULTDURATION:
            duration = readUint(s, pos, size);
            break;
        case ID_CODECID:
            if (size < sizeof codecId && readBytes(s, pos, codecId, size))
                codecId[size] = 0;
            break;
        case ID_VIDEO: {
            uint32_t vpos = pos, vend = pos + size;
            while (vpos < vend) {
                uint32_t vid, vsize;
                if (!readId(s, &vpos, &vid) || !readSize(s, &vpos, &vsize) || vsize > vend - vpos)
                    break;
                if (vid == ID_PIXELWIDTH)
                    width = readUint(s, vpos, vsize);
                else if (vid == ID_PIXELHEIGHT)
                    height = readUint(s, vpos, vsize);
                vpos += vsize;
            }
            break;
        }
        }
        pos += size;
    }
    if (type == 2 && number && p->audioTracks < 0xffffffffu)
        p->audioTracks++;
    if (p->videoTrack || type != 1 || !number || !width || !height)
        return;
    if (!strcmp(codecId, "V_VP8"))
        p->codec = WEBM_CODEC_VP8;
    else if (!strcmp(codecId, "V_VP9"))
        p->codec = WEBM_CODEC_VP9;
    else
        return;
    p->videoTrack = number;
    p->width = width;
    p->height = height;
    p->defaultDuration = duration;
}

int webm_parse(WebMReadFn read, void *handle, uint32_t fileSize, WebMInfo *info)
{
    Source s = { read, handle, fileSize };
    Parser p;
    uint32_t pos = 0, id, size, end, i, n;
    char docType[16];
    int result = -4;

    memset(info, 0, sizeof *info);
    memset(&p, 0, sizeof p);
    p.timecodeScale = 1000000;

    /* The EBML header, with the document type. */
    if (!readId(&s, &pos, &id) || id != ID_EBML || !readSize(&s, &pos, &size) || size == SIZE_UNKNOWN || size > fileSize - pos)
        return -1;
    end = pos + size;
    docType[0] = 0;
    while (pos < end) {
        uint32_t did, dsize;
        if (!readId(&s, &pos, &did) || !readSize(&s, &pos, &dsize) || dsize > end - pos)
            return -1;
        if (did == ID_DOCTYPE && dsize < sizeof docType && readBytes(&s, pos, docType, dsize))
            docType[dsize] = 0;
        pos += dsize;
    }
    if (strcmp(docType, "webm") && strcmp(docType, "matroska"))
        return -1;

    /* The segment: its size may be unknown in live recordings. */
    if (!readId(&s, &pos, &id) || id != ID_SEGMENT || !readSize(&s, &pos, &size))
        return -1;
    end = (size == SIZE_UNKNOWN || size > fileSize - pos) ? fileSize : pos + size;

    while (pos < end) {
        uint32_t elementEnd;
        if (!readId(&s, &pos, &id) || !readSize(&s, &pos, &size))
            break;
        elementEnd = (size == SIZE_UNKNOWN || size > end - pos) ? end : pos + size;
        if (id == ID_INFO) {
            uint32_t ipos = pos;
            while (ipos < elementEnd) {
                uint32_t iid, isize;
                if (!readId(&s, &ipos, &iid) || !readSize(&s, &ipos, &isize) || isize > elementEnd - ipos)
                    break;
                if (iid == ID_TIMECODESCALE)
                    p.timecodeScale = readUint(&s, ipos, isize);
                ipos += isize;
            }
            if (!p.timecodeScale)
                p.timecodeScale = 1000000;
        } else if (id == ID_TRACKS) {
            uint32_t tpos = pos;
            while (tpos < elementEnd) {
                uint32_t tid, tsize;
                if (!readId(&s, &tpos, &tid) || !readSize(&s, &tpos, &tsize) || tsize > elementEnd - tpos)
                    break;
                if (tid == ID_TRACKENTRY)
                    parseTrackEntry(&s, &p, tpos, tpos + tsize);
                tpos += tsize;
            }
        } else if (id == ID_CLUSTER) {
            uint32_t next = elementEnd;
            if (!parseCluster(&s, &p, pos, elementEnd, &next)) {
                if (!p.count)
                    goto fail;
                break; /* a damaged tail: keep what was read */
            }
            pos = next;
            continue;
        }
        pos = elementEnd;
    }

    if (!p.videoTrack) {
        result = -2;
        goto fail;
    }
    for (i = 0, n = 0; i < p.count; i++)
        if (p.blocks[i].track == p.videoTrack)
            n++;
    if (!n)
        goto fail;
    info->frames = malloc(n * sizeof(WebMFrame));
    if (!info->frames) {
        result = -3;
        goto fail;
    }
    for (i = 0, n = 0; i < p.count; i++)
        if (p.blocks[i].track == p.videoTrack)
            info->frames[n++] = p.blocks[i].frame;
    info->frameCount = n;
    info->codec = p.codec;
    info->width = p.width;
    info->height = p.height;
    info->frameDurationNs = p.defaultDuration;
    info->durationMs = info->frames[n - 1].timeMs;
    info->audioTracks = p.audioTracks;
    free(p.blocks);
    return 0;

fail:
    free(p.blocks);
    return result;
}

void webm_free(WebMInfo *info)
{
    free(info->frames);
    info->frames = NULL;
    info->frameCount = 0;
}
