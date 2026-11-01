/*
 * webm.datatype's sound on the host (tests/test-webm-read.sh), under
 * AddressSanitizer and UBSan: webm/webmclass.c itself, built with the
 * Amiga's names from tests/webmclass-host.h and fakes for them here, and a
 * fake media.decode/1 that keeps a record of every request. The object is
 * made (OM_NEW), every frame loaded in order (ADTM_LOADFRAME), then a few
 * out of order, then disposed; with a clip that has sound:
 *   - no buffer of more than 256 KB (gfopenvideo.datatype's VUPLOAD part)
 *     goes to the service, and with a link that carries no more than that
 *     the sound still comes;
 *   - the file goes up once (one VUPLOAD started, its parts the file's
 *     bytes, the last one marked) and is opened from the upload (VOPEN with
 *     no file), and not sent for PROBE or DECODE;
 *   - no block as big as the file is allocated, and no chip RAM block
 *     bigger than a ring of 64 frames' sound;
 *   - each frame's sound is asked for as it loads (VSOUND), for the
 *     service's steady frames from the frame's start to the next frame's:
 *     the same frame when the rates agree, scaled when the service counts
 *     at 50, 12.5 or 29.97 frames a second for a 25 a second clip, so the
 *     sound of a whole play is every steady frame once, in order; the
 *     sample handed out is what VSOUND gave, in chip RAM;
 *   - the video is closed (VCLOSE) when the object goes, and every block
 *     freed.
 * Without the service, without VUPLOAD, when the service finds no sound or
 * VSOUND fails, the clip opens and plays silent. A clip without sound never
 * calls the service.
 *
 *   webmsoundtest [-audio N] file.webm...
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "webmclass.c"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define PIECE (256UL * 1024)         /* gfopenvideo.datatype's VUPLOAD part */
#define RING_FRAMES 64

static int failures, checks;

static void check(int ok, const char *what, const char *detail)
{
    checks++;
    if (!ok) {
        printf("FAIL %s%s%s\n", what, detail ? ": " : "", detail ? detail : "");
        failures++;
    }
}

/* ---- memory ---------------------------------------------------------------- */

typedef struct {
    UBYTE *p;
    ULONG size, flags;
} Block;

static Block blocks[256];
static int nblocks;
static ULONG largestAny, largestChip;

APTR AllocVec(ULONG size, ULONG flags)
{
    UBYTE *p;
    if (nblocks == (int)(sizeof blocks / sizeof blocks[0]) || !(p = calloc(1, size ? size : 1)))
        return NULL;
    blocks[nblocks].p = p;
    blocks[nblocks].size = size;
    blocks[nblocks++].flags = flags;
    if (flags & MEMF_CHIP) {
        if (size > largestChip)
            largestChip = size;
    } else if (size > largestAny)
        largestAny = size;
    return p;
}

void FreeVec(APTR p)
{
    int i;
    for (i = 0; i < nblocks; i++)
        if (blocks[i].p == p) {
            free(p);
            blocks[i] = blocks[--nblocks];
            return;
        }
    check(0, "FreeVec of a block not allocated", NULL);
}

static int inChip(const void *p, ULONG n)
{
    int i;
    for (i = 0; i < nblocks; i++)
        if ((blocks[i].flags & MEMF_CHIP) && (const UBYTE *)p >= blocks[i].p
            && (const UBYTE *)p + n <= blocks[i].p + blocks[i].size)
            return 1;
    return 0;
}

void InitSemaphore(struct SignalSemaphore *s) { s->ss_Count = 0; }
void ObtainSemaphore(struct SignalSemaphore *s) { s->ss_Count++; }
void ReleaseSemaphore(struct SignalSemaphore *s) { s->ss_Count--; }

/* ---- dos ------------------------------------------------------------------- */

static LONG ioErr;
static ULONG largestRead;

LONG Seek(BPTR file, LONG position, LONG mode)
{
    off_t old = lseek((int)file, 0, SEEK_CUR);
    if (lseek((int)file, position, mode == OFFSET_BEGINNING ? SEEK_SET : mode == OFFSET_END ? SEEK_END : SEEK_CUR) < 0)
        return -1;
    return (LONG)old;
}

LONG Read(BPTR file, APTR buffer, LONG length)
{
    if ((ULONG)length > largestRead)
        largestRead = length;
    return (LONG)read((int)file, buffer, length);
}

void SetIoErr(LONG code) { ioErr = code; }
LONG IoErr(void) { return ioErr; }
STRPTR FilePart(CONST_STRPTR path)
{
    const char *s = strrchr(path, '/');
    return (STRPTR)(s ? s + 1 : path);
}

/* ---- graphics and libvpx --------------------------------------------------- */

struct BitMap *AllocBitMap(ULONG width, ULONG height, ULONG depth, ULONG flags, struct BitMap *friend_)
{
    (void)width; (void)height; (void)depth; (void)flags; (void)friend_;
    return calloc(1, sizeof(struct BitMap));
}
void FreeBitMap(struct BitMap *bm) { free(bm); }
void InitRastPort(struct RastPort *rp) { rp->BitMap = NULL; }
void WriteChunkyPixels(struct RastPort *rp, ULONG x0, ULONG y0, ULONG x1, ULONG y1, UBYTE *array, LONG modulo)
{
    (void)rp; (void)x0; (void)y0; (void)x1; (void)y1; (void)array; (void)modulo;
}
void WaitBlit(void) {}

static vpx_codec_iface_t vp8, vp9;
int vpx_codec_dec_init(vpx_codec_ctx_t *ctx, vpx_codec_iface_t *iface, const void *cfg, long flags)
{
    (void)ctx; (void)iface; (void)cfg; (void)flags;
    return 0;
}
int vpx_codec_decode(vpx_codec_ctx_t *ctx, const uint8_t *data, unsigned int size, void *user, long deadline)
{
    (void)ctx; (void)data; (void)size; (void)user; (void)deadline;
    return 0;
}
vpx_image_t *vpx_codec_get_frame(vpx_codec_ctx_t *ctx, vpx_codec_iter_t *iter)
{
    (void)ctx; (void)iter;
    return NULL;
}
int vpx_codec_destroy(vpx_codec_ctx_t *ctx) { (void)ctx; return 0; }
vpx_codec_iface_t *vpx_codec_vp8_dx(void) { return &vp8; }
vpx_codec_iface_t *vpx_codec_vp9_dx(void) { return &vp9; }

ULONG dt_call_with_stack(ULONG bytes, ULONG (*fn)(APTR), APTR arg)
{
    (void)bytes;
    return fn(arg);
}

/* ---- the object ------------------------------------------------------------ */

static Class cls;
static Object *theObject;
static BPTR theFile;
static const char *theName;
static ULONG setPeriod, setFps;

ULONG DoSuperMethodA(Class *cl, Object *o, Msg msg)
{
    (void)cl; (void)o;
    return msg->MethodID == OM_NEW ? (ULONG)theObject : 0;
}

ULONG fake_coerce(Class *cl, Object *o, ULONG method)
{
    struct { ULONG MethodID; } m = { method };
    return dt_dispatch(cl, o, (Msg)&m);
}

ULONG fake_getdtattrs(Object *o, const ULONG *tags)
{
    ULONG n = 0;
    (void)o;
    for (; tags[0] != TAG_DONE; tags += 2, n++) {
        if (tags[0] == DTA_SourceType)
            *(ULONG *)tags[1] = DTST_FILE;
        else if (tags[0] == DTA_Handle)
            *(BPTR *)tags[1] = theFile;
        else if (tags[0] == DTA_Name)
            *(STRPTR *)tags[1] = (STRPTR)theName;
    }
    return n;
}

ULONG fake_setdtattrs(Object *o, const ULONG *tags)
{
    ULONG n = 0;
    (void)o;
    for (; tags[0] != TAG_DONE; tags += 2, n++) {
        if (tags[0] == ADTA_Period)
            setPeriod = tags[1];
        else if (tags[0] == ADTA_FramesPerSecond)
            setFps = tags[1];
    }
    return n;
}

/* ---- media.decode/1 -------------------------------------------------------- */

typedef struct {
    const char *name;
    int service;               /* the service is offered */
    int upload;                /* it has VUPLOAD */
    int sound;                 /* VOPEN finds a sound track */
    int vsoundFails;
    ULONG hostFps1000;         /* the steady rate VOPEN answers */
    ULONG limit;               /* the largest buffer the link carries, 0: any */
    int expectSound;
} Case;

static const Case *cfg;
static const UBYTE *fileData;
static ULONG fileSize, clipMs;

#define UPLOAD_ID 0x55AA0001UL
#define VIDEO_ID 0x00001234UL

static struct {
    int opens, closes, probes, decodes, vopens, vopensWhole, vopensUpload, vcloses, uploadsStarted, refused;
    ULONG largestBuf, wholeSends, truncated;
    UBYTE *up;
    ULONG upLength, upId;
    int upDone, videoOpen;
    ULONG *vsFrame, *vsCount, vsN, vsCap;   /* every VSOUND: the frame, the samples given */
    ULONG vsRate;
} svc;

BOOL dt_service_open(struct dt_service *s, const char *name)
{
    if (!cfg->service || strcmp(name, "media.decode/1"))
        return FALSE;
    memset(s, 0, sizeof *s);
    s->open = TRUE;
    svc.opens++;
    return TRUE;
}

void dt_service_close(struct dt_service *s)
{
    check(s->open, "service closed that was not open", NULL);
    s->open = FALSE;
    svc.closes++;
}

static void put32(UBYTE *p, ULONG v)
{
    p[0] = (UBYTE)(v >> 24);
    p[1] = (UBYTE)(v >> 16);
    p[2] = (UBYTE)(v >> 8);
    p[3] = (UBYTE)v;
}

LONG dt_service_call(struct dt_service *s, UWORD op, ULONG arg, ULONG flags, const struct OSBuffer buf[4],
                     const ULONG extra[4], ULONG *result, ULONG *aux)
{
    ULONG b0 = buf[0].ob_Length, b1 = buf[1].ob_Length, i;
    (void)flags;
    check(s->open, "service called while not open", NULL);
    if (b0 > svc.largestBuf)
        svc.largestBuf = b0;
    if (b0 >= fileSize)
        svc.wholeSends++;
    if (cfg->limit && (b0 > cfg->limit || b1 > cfg->limit)) {
        svc.refused++;
        return OSERR_HOST;                     /* more than the link carries */
    }
    switch (op) {
    case 1:                                     /* PROBE: Opus at 24 kHz, mono */
    case 2: {                                   /* DECODE */
        ULONG frames = clipMs / 1000 * 24000 + clipMs % 1000 * 24;
        if (op == 1) {
            svc.probes++;
            if (b1 < 24)
                return OSERR_TOOSMALL;
            put32(buf[1].ob_Data, 3);
            put32((UBYTE *)buf[1].ob_Data + 4, 0x4F505553UL);
            put32((UBYTE *)buf[1].ob_Data + 8, 0);
            put32((UBYTE *)buf[1].ob_Data + 12, frames);
            put32((UBYTE *)buf[1].ob_Data + 16, 24000);
            put32((UBYTE *)buf[1].ob_Data + 20, 1);
            if (result)
                *result = frames;
            if (aux)
                *aux = 24000;
            return OSERR_OK;
        }
        svc.decodes++;
        if (frames > b1 / 2)
            frames = b1 / 2;
        for (i = 0; i < frames * 2; i++)
            ((UBYTE *)buf[1].ob_Data)[i] = (UBYTE)(i >> 9);
        if (result)
            *result = frames;
        if (aux)
            *aux = 24000;
        return OSERR_OK;
    }
    case 8:                                     /* VUPLOAD */
        if (!cfg->upload)
            return OSERR_BADREQUEST;           /* an older service */
        if (!arg) {
            svc.uploadsStarted++;
            free(svc.up);
            svc.up = NULL;
            svc.upLength = 0;
            svc.upDone = 0;
            svc.upId = UPLOAD_ID;
        } else if (arg != svc.upId || svc.upDone)
            return OSERR_BADREQUEST;
        svc.up = realloc(svc.up, svc.upLength + b0 + 1);
        memcpy(svc.up + svc.upLength, buf[0].ob_Data, b0);
        svc.upLength += b0;
        if (extra[0] & 1)
            svc.upDone = 1;
        if (result)
            *result = svc.upId;
        if (aux)
            *aux = svc.upLength;
        return OSERR_OK;
    case 3:                                     /* VOPEN */
        svc.vopens++;
        if (b0)
            svc.vopensWhole++;
        else if (extra[2] == svc.upId && svc.upId && svc.upDone && svc.upLength == fileSize
                 && !memcmp(svc.up, fileData, fileSize)) {
            svc.vopensUpload++;
            svc.upId = 0;                       /* the upload is used up */
        } else
            return OSERR_BADREQUEST;
        if (b1 < 24)
            return OSERR_TOOSMALL;
        put32(buf[1].ob_Data, 2);
        put32((UBYTE *)buf[1].ob_Data + 4, 0x56503820UL);
        put32((UBYTE *)buf[1].ob_Data + 8, cfg->sound ? 2 : 0);
        put32((UBYTE *)buf[1].ob_Data + 12, 0);
        put32((UBYTE *)buf[1].ob_Data + 16, extra[0]);
        put32((UBYTE *)buf[1].ob_Data + 20, extra[1]);
        svc.videoOpen = 1;
        if (result)
            *result = VIDEO_ID;
        if (aux)
            *aux = cfg->hostFps1000;
        return OSERR_OK;
    case 7: {                                   /* VSOUND: frame n's share, each byte n & 0x7f */
        ULONG rate = extra[1] ? extra[1] : 22050, n = extra[0], count;
        if (arg != VIDEO_ID || !svc.videoOpen || !cfg->sound || cfg->vsoundFails)
            return OSERR_BADREQUEST;
        count = (ULONG)((unsigned long long)(n + 1) * rate * 1000 / cfg->hostFps1000
                        - (unsigned long long)n * rate * 1000 / cfg->hostFps1000);
        if (count > b1) {
            svc.truncated++;
            count = b1;
        }
        memset(buf[1].ob_Data, n & 0x7f, count);
        if (svc.vsN == svc.vsCap) {
            svc.vsCap = svc.vsCap ? svc.vsCap * 2 : 1024;
            svc.vsFrame = realloc(svc.vsFrame, svc.vsCap * sizeof *svc.vsFrame);
            svc.vsCount = realloc(svc.vsCount, svc.vsCap * sizeof *svc.vsCount);
        }
        svc.vsFrame[svc.vsN] = n;
        svc.vsCount[svc.vsN++] = count;
        svc.vsRate = rate;
        if (result)
            *result = count;
        if (aux)
            *aux = rate;
        return OSERR_OK;
    }
    case 5:                                     /* VCLOSE */
        if (arg == VIDEO_ID && svc.videoOpen) {
            svc.videoOpen = 0;
            svc.vcloses++;
        }
        return OSERR_OK;
    default:
        return OSERR_BADREQUEST;
    }
}

/* ---- the cases ------------------------------------------------------------- */

/* The steady frame where frame i starts, for a clip played at fps. */
static ULONG steady(ULONG i, ULONG fps)
{
    return (ULONG)((unsigned long long)i * cfg->hostFps1000 / ((unsigned long long)fps * 1000));
}

/* Loads frame i; checks its sound against the VSOUND requests it made.
 * *mismatch counts frames whose sound is wrong (printed for the first). */
static void loadOne(ULONG i, int *mismatch)
{
    struct adtFrame alf;
    ULONG first = svc.vsN, k, at = 0, want, wantEnd;
    char why[200];
    int bad = 0;

    memset(&alf, 0, sizeof alf);
    alf.MethodID = ADTM_LOADFRAME;
    alf.alf_TimeStamp = i;
    dt_dispatch(&cls, theObject, (Msg)&alf);
    why[0] = 0;
    if (!cfg->expectSound) {
        if (alf.alf_Sample || alf.alf_SampleLength)
            bad = snprintf(why, sizeof why, "frame %lu has %lu samples, expected none", i, alf.alf_SampleLength);
    } else {
        want = steady(i, setFps);
        wantEnd = steady(i + 1, setFps);
        if (svc.vsN - first != wantEnd - want)
            bad = snprintf(why, sizeof why, "frame %lu asked VSOUND %lu times, expected %lu (steady %lu to %lu)", i,
                           svc.vsN - first, wantEnd - want, want, wantEnd);
        for (k = first; !bad && k < svc.vsN; k++)
            if (svc.vsFrame[k] != want + (k - first))
                bad = snprintf(why, sizeof why, "frame %lu asked for steady frame %lu, expected %lu", i,
                               svc.vsFrame[k], want + (k - first));
        for (k = first; !bad && k < svc.vsN; k++) {
            ULONG j;
            for (j = 0; j < svc.vsCount[k] && at + j < alf.alf_SampleLength; j++)
                if ((UBYTE)alf.alf_Sample[at + j] != (svc.vsFrame[k] & 0x7f))
                    break;
            if (j < svc.vsCount[k])
                bad = snprintf(why, sizeof why, "frame %lu's sample is not VSOUND's", i);
            at += svc.vsCount[k];
        }
        if (!bad && alf.alf_SampleLength != at)
            bad = snprintf(why, sizeof why, "frame %lu has %lu samples, VSOUND gave %lu", i, alf.alf_SampleLength, at);
        if (!bad && at && (!alf.alf_Sample || !inChip(alf.alf_Sample, at)))
            bad = snprintf(why, sizeof why, "frame %lu's sample is not in chip RAM", i);
        if (!bad && at && alf.alf_Period != setPeriod)
            bad = snprintf(why, sizeof why, "frame %lu's period %lu, the object's %lu", i, alf.alf_Period, setPeriod);
    }
    if (bad && !(*mismatch)++)
        check(0, cfg->name, why);
    alf.MethodID = ADTM_UNLOADFRAME;
    dt_dispatch(&cls, theObject, (Msg)&alf);
}

static void runCase(const Case *c, const char *path, ULONG frames)
{
    struct { ULONG MethodID; } m;
    Object *o;
    ULONG i, sequence = 0;
    int mismatch = 0, seekMismatch = 0;
    char what[160], detail[160];

    cfg = c;
    free(svc.up);
    free(svc.vsFrame);
    free(svc.vsCount);
    memset(&svc, 0, sizeof svc);
    largestAny = largestChip = largestRead = 0;
    setPeriod = setFps = 0;
    theFile = open(path, O_RDONLY);
    theName = path;
    theObject = calloc(1, dt_instsize);
    cls.cl_InstOffset = 0;
    cls.cl_InstSize = dt_instsize;

    m.MethodID = OM_NEW;
    o = (Object *)dt_dispatch(&cls, NULL, (Msg)&m);
    snprintf(what, sizeof what, "%s: %s opens", path, c->name);
    check(o == theObject, what, NULL);
    if (!o)
        goto out;

#define CHECK(cond, ...) do { snprintf(what, sizeof what, "%s: %s", path, c->name); \
        snprintf(detail, sizeof detail, __VA_ARGS__); check(cond, what, detail); } while (0)
    if (c->service && c->expectSound) {
        CHECK(svc.largestBuf <= PIECE, "largest buffer sent %lu bytes, more than a %lu-byte part", svc.largestBuf, PIECE);
        CHECK(svc.wholeSends == 0, "the whole file sent %lu times", svc.wholeSends);
        CHECK(svc.probes == 0 && svc.decodes == 0, "%d PROBE and %d DECODE sends of the file", svc.probes, svc.decodes);
        CHECK(svc.uploadsStarted == 1 && svc.vopensUpload == 1 && svc.vopensWhole == 0,
              "%d uploads, %d VOPENs of the upload, %d of the whole file", svc.uploadsStarted, svc.vopensUpload,
              svc.vopensWhole);
        CHECK(largestAny < fileSize, "a %lu-byte block for a %lu-byte file", largestAny, fileSize);
        CHECK(largestRead <= PIECE, "a %lu-byte Read", largestRead);
        CHECK(svc.refused == 0, "%d requests over the link's %lu bytes", svc.refused, c->limit);
    }
    if (!c->service || !c->upload || !c->sound)
        CHECK(setPeriod == 0, "silent, yet a period of %lu", setPeriod);
    if (!c->service || !c->upload)
        CHECK(svc.vopens == 0 && svc.probes == 0 && svc.decodes == 0,
              "%d VOPEN, %d PROBE, %d DECODE without the service or VUPLOAD", svc.vopens, svc.probes, svc.decodes);

    /* a play, frame by frame */
    for (i = 0; i < frames; i++)
        loadOne(i, &mismatch);
    snprintf(what, sizeof what, "%s: %s: every frame's sound as VSOUND gives it", path, c->name);
    check(!mismatch, what, NULL);
    if (c->expectSound) {
        ULONG k;
        for (k = 0; k < svc.vsN && svc.vsFrame[k] == sequence; k++)
            sequence++;
        CHECK(sequence == svc.vsN && sequence == steady(frames, setFps),
              "a play asked for steady frames 0 to %lu in order once each, then %lu more (of %lu, to %lu)", sequence,
              svc.vsN - sequence, svc.vsN, steady(frames, setFps));
        CHECK(svc.truncated == 0, "%lu VSOUNDs had too small a buffer", svc.truncated);
        CHECK(setPeriod && setPeriod * svc.vsRate <= PAL_CLOCK && (setPeriod + 1) * svc.vsRate > PAL_CLOCK,
              "period %lu for VSOUND's rate %lu", setPeriod, svc.vsRate);
        CHECK(largestChip <= RING_FRAMES * (2 * (ULONG)((unsigned long long)svc.vsRate * 1000
                                                        / (c->hostFps1000 < setFps * 1000 ? c->hostFps1000 : setFps * 1000)) + 8),
              "a %lu-byte chip RAM block, more than a ring of %d frames' sound", largestChip, RING_FRAMES);
    }
    /* out of order */
    for (i = frames > 100 ? 100 : frames - 1; ; i = i > 7 ? 7 : i + 1) {
        loadOne(i, &seekMismatch);
        if (i == 8 || frames < 9)
            break;
    }
    snprintf(what, sizeof what, "%s: %s: out of order", path, c->name);
    check(!seekMismatch, what, NULL);

    m.MethodID = OM_DISPOSE;
    dt_dispatch(&cls, theObject, (Msg)&m);
    CHECK(svc.vopens == 0 || svc.vcloses == 1, "%d VOPENs, %d VCLOSEs", svc.vopens, svc.vcloses);
    CHECK(!svc.videoOpen, "the video is still open on the service");
    CHECK(nblocks == 0, "%d blocks not freed", nblocks);
    CHECK(svc.opens == svc.closes, "service opened %d times, closed %d", svc.opens, svc.closes);
out:
    close((int)theFile);
    free(theObject);
    printf("%s: %s: %lu VSOUNDs, %d uploads, largest send %lu, largest chip block %lu\n", path, c->name, svc.vsN,
           svc.uploadsStarted, svc.largestBuf, largestChip);
}

static UBYTE *readFile(const char *path, ULONG *size)
{
    FILE *f = fopen(path, "rb");
    UBYTE *p;
    long n;
    if (!f || fseek(f, 0, SEEK_END) || (n = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET) || !(p = malloc(n))
        || fread(p, 1, n, f) != (size_t)n) {
        printf("FAIL cannot read %s\n", path);
        exit(1);
    }
    fclose(f);
    *size = (ULONG)n;
    return p;
}

int main(int argc, char **argv)
{
    static const Case withSound[] = {
        { "rates agree", 1, 1, 1, 0, 25000, 0, 1 },
        { "a link of 256 KB", 1, 1, 1, 0, 25000, 256 * 1024, 1 },
        { "service at 50 a second", 1, 1, 1, 0, 50000, 0, 1 },
        { "service at 12.5 a second", 1, 1, 1, 0, 12500, 0, 1 },
        { "service at 29.97 a second", 1, 1, 1, 0, 29970, 0, 1 },
        { "no service", 0, 0, 0, 0, 25000, 0, 0 },
        { "no VUPLOAD", 1, 0, 1, 0, 25000, 0, 0 },
        { "no sound found", 1, 1, 0, 0, 25000, 0, 0 },
        { "VSOUND fails", 1, 1, 1, 1, 25000, 0, 0 },
    };
    static const Case noSound = { "no audio track", 1, 1, 1, 0, 25000, 0, 0 };
    int a, audio = -1;
    unsigned c;

    dt_init();
    for (a = 1; a < argc; a++) {
        WebMInfo info;
        int fd, rc;
        if (!strcmp(argv[a], "-audio") && a + 1 < argc) {
            audio = atoi(argv[++a]);
            continue;
        }
        fileData = readFile(argv[a], &fileSize);
        fd = open(argv[a], O_RDONLY);
        rc = webm_parse(readAt, (void *)(long)fd, fileSize, &info);
        close(fd);
        if (rc) {
            printf("FAIL %s: not read (%d)\n", argv[a], rc);
            failures++;
            continue;
        }
        clipMs = info.durationMs;
        if (audio >= 0 && (int)(info.audioTracks != 0) != audio) {
            printf("FAIL %s: %u audio tracks\n", argv[a], (unsigned)info.audioTracks);
            failures++;
        }
        if (info.audioTracks) {
            for (c = 0; c < sizeof withSound / sizeof withSound[0]; c++)
                runCase(&withSound[c], argv[a], info.frameCount);
        } else {
            runCase(&noSound, argv[a], info.frameCount);
            if (svc.opens)
                check(0, argv[a], "a clip without sound called the service");
        }
        webm_free(&info);
        free((void *)fileData);
        audio = -1;
    }
    free(svc.up);
    free(svc.vsFrame);
    free(svc.vsCount);
    printf("webmsoundtest: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
