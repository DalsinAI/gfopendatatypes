/* GFOpenDatatypes anim.datatype core: IFF ANIM files, in plain C with no
 * Amiga headers.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * Clean-room, from the public IFF ANIM specification (Electronic Arts'
 * and Sparta's ANIM documents: ANHD, DLTA, the compression methods 0, 5,
 * 7 and 8), EA IFF 85 and the ILBM specification (BMHD, CMAP, CAMG,
 * BODY, ByteRun1), NDK 3.2's datatypes/animationclass.h, and OS 3.2's
 * answers measured by tests/AnimationClassABITest; never from Kickstart,
 * Hyperion, AROS or any third-party datatype's code.
 *
 * The core reads an ANIM held in memory: anm_scan indexes it, anm_goto
 * decodes frames into planar buffers (the bitplanes of a BitMap, one row
 * of bpr bytes per plane after the other), and anm_copy_out puts the
 * current frame into a caller's planes. Nothing here includes an Amiga
 * header or calls the system: memory comes in through struct anm_env.
 */
#ifndef OPENDATATYPES_ANIM_CORE_H
#define OPENDATATYPES_ANIM_CORE_H

#include "../text/dtc_util.h"
#include "../animation/animation_core.h"

#define ANM_ID_FORM ODT_ID('F', 'O', 'R', 'M')
#define ANM_ID_ANIM ODT_ID('A', 'N', 'I', 'M')
#define ANM_ID_ILBM ODT_ID('I', 'L', 'B', 'M')
#define ANM_ID_BMHD ODT_ID('B', 'M', 'H', 'D')
#define ANM_ID_CMAP ODT_ID('C', 'M', 'A', 'P')
#define ANM_ID_CAMG ODT_ID('C', 'A', 'M', 'G')
#define ANM_ID_BODY ODT_ID('B', 'O', 'D', 'Y')
#define ANM_ID_ANHD ODT_ID('A', 'N', 'H', 'D')
#define ANM_ID_DLTA ODT_ID('D', 'L', 'T', 'A')
#define ANM_ID_DPAN ODT_ID('D', 'P', 'A', 'N')

#define ANM_MAXDEPTH 8

/* BMHD (ILBM), as read */
struct anm_bmhd {
    odt_u16 w, h;
    odt_s16 x, y;
    odt_u8 nplanes, masking, compression, pad1;
    odt_u16 transparent;
    odt_u8 xaspect, yaspect;
    odt_s16 pagew, pageh;
};
#define ANM_MSK_NONE        0
#define ANM_MSK_HASMASK     1
#define ANM_MSK_TRANSPARENT 2
#define ANM_MSK_LASSO       3
#define ANM_CMP_NONE        0
#define ANM_CMP_BYTERUN1    1

/* ANHD, as read (the layout is datatypes/animationclass.h's AnimHeader) */
struct anm_anhd {
    odt_u8 op, mask;
    odt_u16 w, h;
    odt_s16 x, y;
    odt_u32 abstime, reltime;
    odt_u8 interleave, pad0;
    odt_u32 flags;
};
#define ANM_ANHD_SIZE 40
/* ah_Flags (the ANIM spec's "bits") */
#define ANM_F_LONG     (1UL << 0)      /* op 7, 8: long data, else short */
#define ANM_F_XOR      (1UL << 1)      /* op 5, 7, 8: XOR the data in, else store */

/* One frame: where its chunks are in the file (0: none) */
struct anm_frame {
    odt_u32 body, body_len;
    odt_u32 dlta, dlta_len;
    odt_u32 cmap, cmap_len;            /* the CMAP in effect: this frame's or an earlier one's */
    struct anm_anhd hd;
    odt_u8 has_hd;
    odt_u8 pad;
};

struct anm_info {
    struct anm_bmhd bmhd;
    odt_u32 camg;                       /* cleaned (anm_mode_id); 0: none */
    odt_u8 has_camg;
    odt_u8 depth;                       /* planes decoded: nplanes, no mask */
    odt_u16 bpr;                        /* bytes a plane row: ((w + 15) >> 4) << 1 */
    odt_u32 nframes;
    struct anm_frame *frames;           /* nframes of them, from env alloc */
    odt_u32 fps;                        /* ADTA_FramesPerSecond */
    odt_u32 dpan_frames;                /* DPAN's frame count, 0 if none */
};

struct anm_env {
    void *ctx;
    void *(*alloc)(void *ctx, odt_u32 n);  /* cleared */
    void (*free)(void *ctx, void *p);
};

/* Results */
#define ANM_OK          0
#define ANM_ERR_NOTANIM ODT_DTERROR_UNKNOWN_DATATYPE   /* not a FORM ANIM */
#define ANM_ERR_SHORT   ODT_DTERROR_NOT_ENOUGH_DATA
#define ANM_ERR_INVALID ODT_DTERROR_INVALID_DATA       /* no BMHD, no first BODY, depth 0 or > 8 */
#define ANM_ERR_COMP    ODT_DTERROR_UNKNOWN_COMPRESSION
#define ANM_ERR_MEMORY  DTC_ERROR_NO_FREE_STORE

/* Big-endian reads */
static inline odt_u32 anm_be32(const odt_u8 *p)
{ return ((odt_u32)p[0] << 24) | ((odt_u32)p[1] << 16) | ((odt_u32)p[2] << 8) | p[3]; }
static inline odt_u16 anm_be16(const odt_u8 *p) { return (odt_u16)((p[0] << 8) | p[1]); }

/* Indexes an ANIM of len bytes. info->frames is allocated; anm_free_info
 * frees it. */
int anm_scan(const struct anm_env *env, const odt_u8 *data, odt_u32 len, struct anm_info *info);
void anm_free_info(const struct anm_env *env, struct anm_info *info);

/* The display mode: CAMG cleaned as the ILBM spec says, or one made from
 * the size when there is no CAMG. */
odt_u32 anm_mode_id(const struct anm_info *info);

/* Colours: the CMAP in effect at frame n as 8-bit R, G, B triples into
 * rgb (room for 3 * 256); returns how many. EHB's upper half is made from
 * the lower when the CMAP has 32. */
odt_u32 anm_colors(const struct anm_info *info, const odt_u8 *data, odt_u32 n, odt_u8 *rgb);

/* ByteRun1 or plain BODY into planes (depth planes, rows x bpr each,
 * plane after plane); the mask plane is skipped. Returns ANM_OK,
 * ANM_ERR_SHORT (what was there was decoded, the rest left as it was) or
 * ANM_ERR_COMP. */
int anm_body(const struct anm_bmhd *h, odt_u32 bpr, odt_u32 depth, const odt_u8 *body, odt_u32 len,
             odt_u8 *planes);

/* One DLTA applied to planes (as anm_body's). op is the ANHD's (5, 7 or
 * 8), flags its ah_Flags. Returns ANM_OK, ANM_ERR_SHORT (data ran out:
 * what came before is applied) or ANM_ERR_COMP for another op. */
int anm_delta(odt_u32 op, odt_u32 flags, odt_u32 bpr, odt_u32 rows, odt_u32 depth,
              const odt_u8 *dlta, odt_u32 len, odt_u8 *planes);

/* A checkpoint: the decoder's two buffers as they were at a frame */
struct anm_cp {
    odt_u8 *buf;                        /* buf[0] then buf[1]; 0 until the decoder first passes the frame */
    int err;                            /* the first error from frame 0 to here */
};

/* Checkpoints keep a backward seek from decoding again from frame 0: one
 * each `every` frames (frame every, 2 * every, ...), made the first time
 * the decoder passes, up to ANM_CP_BYTES of them, no more than ANM_CP_MAX
 * and no closer than ANM_CP_MIN_EVERY frames apart. */
#define ANM_CP_BYTES     (1UL << 20)
#define ANM_CP_MAX       32
#define ANM_CP_MIN_EVERY 4

/* The decoder: two frame buffers, as the double-buffered ANIMs need */
struct anm_dec {
    const struct anm_info *info;
    const odt_u8 *data;
    odt_u32 len;
    odt_u8 *buf[2];                     /* frame k is in buf[k & 1] */
    odt_u32 size;                       /* of each: depth * rows * bpr */
    odt_s32 cur;                        /* the frame in buf[cur & 1]; -1 none */
    odt_u32 decoded;                    /* frames decoded, for the tests */
    const struct anm_env *env;
    struct anm_cp *cp;                  /* ncp of them, or 0 */
    odt_u32 ncp, every;
    int err;                            /* the first error from frame 0 to cur */
};

int anm_dec_init(const struct anm_env *env, struct anm_dec *d, const struct anm_info *info,
                 const odt_u8 *data, odt_u32 len);
void anm_dec_free(const struct anm_env *env, struct anm_dec *d);

/* Decodes up to frame n (when n is behind, from the last checkpoint at or
 * before it, else from the start), leaving it current. Returns ANM_OK or
 * the first error (the frames up to it stay): from the start when n was
 * behind, else after the frame that was current. */
int anm_goto(struct anm_dec *d, odt_u32 n);

/* The current frame's planes */
static inline const odt_u8 *anm_planes(const struct anm_dec *d) { return d->cur < 0 ? 0 : d->buf[d->cur & 1]; }

/* Copies the current frame into the planes of a BitMap: bpr bytes of each
 * row, rows modulo bytes apart (a BitMap's BytesPerRow, interleaved or
 * not); planes past the frame's depth are cleared. */
void anm_copy_out(const struct anm_dec *d, odt_u8 *const *planes, odt_u32 dst_depth, odt_u32 modulo);

/* A frame's time on screen in ticks of the animation's fps (alf_Duration) */
odt_u32 anm_duration(const struct anm_info *info, odt_u32 n);

/* ---- the class: anim.datatype under animation.datatype -------------------------- */

struct anm_inst {
    odt_u8 *data;                       /* the whole file, ours */
    odt_u32 len;
    struct anm_info info;
    struct anm_dec dec;
    struct anm_env aenv;
    struct adt_BitMap *key;             /* ADTA_KeyFrame */
    struct odt_SignalSemaphore lock;    /* the decoder, between the player and the application */
    odt_u8 ready;
    odt_u8 pad[3];
};

/* Reads the object's source whole (env alloc); 0 or the DOS error */
odt_s32 anm_read_source(const struct odt_cenv *env, void *obj, odt_u8 **data, odt_u32 *len);

odt_u32 anm_dispatch(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, void *msg);
odt_u32 anm_inst_size(void);

#endif
