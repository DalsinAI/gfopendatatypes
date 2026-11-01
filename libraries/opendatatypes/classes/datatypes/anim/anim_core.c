/* GFOpenDatatypes anim.datatype core: IFF ANIM files.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 * Clean-room; see anim_core.h for what this is written from.
 */
#include "anim_core.h"

/* ---- scanning ------------------------------------------------------------------ */

/* A chunk's padded size, or 0 when it runs past the end */
static odt_u32 chunk_end(odt_u32 at, odt_u32 size, odt_u32 limit)
{
    odt_u32 end = at + 8 + size;
    if (end < at || end > limit) return 0;
    return end + (size & 1) > limit ? end : end + (size & 1);
}

static void read_bmhd(const odt_u8 *p, struct anm_bmhd *h)
{
    h->w = anm_be16(p);
    h->h = anm_be16(p + 2);
    h->x = (odt_s16)anm_be16(p + 4);
    h->y = (odt_s16)anm_be16(p + 6);
    h->nplanes = p[8];
    h->masking = p[9];
    h->compression = p[10];
    h->pad1 = p[11];
    h->transparent = anm_be16(p + 12);
    h->xaspect = p[14];
    h->yaspect = p[15];
    h->pagew = (odt_s16)anm_be16(p + 16);
    h->pageh = (odt_s16)anm_be16(p + 18);
}

static void read_anhd(const odt_u8 *p, odt_u32 len, struct anm_anhd *a)
{
    odt_u8 b[ANM_ANHD_SIZE];
    odt_u32 i;
    /* a short ANHD reads as if zero-filled (ABI test) */
    for (i = 0; i < ANM_ANHD_SIZE; i++) b[i] = i < len ? p[i] : 0;
    a->op = b[0];
    a->mask = b[1];
    a->w = anm_be16(b + 2);
    a->h = anm_be16(b + 4);
    a->x = (odt_s16)anm_be16(b + 6);
    a->y = (odt_s16)anm_be16(b + 8);
    a->abstime = anm_be32(b + 10);
    a->reltime = anm_be32(b + 14);
    a->interleave = b[18];
    a->pad0 = b[19];
    a->flags = anm_be32(b + 20);
}

/* Walks the FORM ILBMs; with frames NULL it only counts them. */
static int walk(const odt_u8 *data, odt_u32 len, struct anm_info *info, struct anm_frame *frames, odt_u32 *count)
{
    odt_u32 at = 12, end, n = 0, cmap = 0, cmap_len = 0;
    int have_bmhd = 0;
    end = 8 + anm_be32(data + 4);
    if (end > len || end < 12) end = len;               /* a FORM longer than the file: what is there (ABI test) */
    while (at + 8 <= end) {
        odt_u32 id = anm_be32(data + at), size = anm_be32(data + at + 4);
        odt_u32 next = chunk_end(at, size, end);
        if (!next) next = end;                          /* the last chunk cut short: read what is there */
        if (id == ANM_ID_FORM && at + 12 <= next && anm_be32(data + at + 8) == ANM_ID_ILBM) {
            odt_u32 c = at + 12;
            struct anm_frame f;
            odt_u8 *z = (odt_u8 *)&f;
            odt_u32 i;
            for (i = 0; i < sizeof f; i++) z[i] = 0;
            while (c + 8 <= next) {
                odt_u32 cid = anm_be32(data + c), cs = anm_be32(data + c + 4);
                odt_u32 cnext = chunk_end(c, cs, next);
                if (!cnext) { cnext = next; cs = next - c - 8; }
                switch (cid) {
                case ANM_ID_BMHD:
                    if (!have_bmhd && cs >= 20) { read_bmhd(data + c + 8, &info->bmhd); have_bmhd = 1; }
                    break;
                case ANM_ID_CAMG:
                    if (n == 0 && cs >= 4) { info->camg = anm_be32(data + c + 8); info->has_camg = 1; }
                    break;
                case ANM_ID_CMAP: cmap = c + 8; cmap_len = cs; break;
                case ANM_ID_BODY: f.body = c + 8; f.body_len = cs; break;
                case ANM_ID_DLTA: f.dlta = c + 8; f.dlta_len = cs; break;
                case ANM_ID_ANHD: read_anhd(data + c + 8, cs, &f.hd); f.has_hd = 1; break;
                case ANM_ID_DPAN:
                    /* DPAN: version (UWORD), nframes (UWORD), flags (ULONG) */
                    if (n == 0 && cs >= 4) info->dpan_frames = anm_be16(data + c + 10);
                    break;
                default: break;
                }
                c = cnext;
            }
            f.cmap = cmap;
            f.cmap_len = cmap_len;
            if (frames) frames[n] = f;
            n++;
        }
        at = next;
    }
    *count = n;
    return have_bmhd;
}

int anm_scan(const struct anm_env *env, const odt_u8 *data, odt_u32 len, struct anm_info *info)
{
    odt_u32 n = 0, i, rel = 0;
    odt_u8 *z = (odt_u8 *)info;
    for (i = 0; i < sizeof *info; i++) z[i] = 0;
    if (len < 12 || anm_be32(data) != ANM_ID_FORM || anm_be32(data + 8) != ANM_ID_ANIM) return ANM_ERR_NOTANIM;
    if (!walk(data, len, info, 0, &n)) return ANM_ERR_INVALID;
    if (!n) return ANM_ERR_INVALID;
    if (info->bmhd.nplanes == 0 || info->bmhd.nplanes > ANM_MAXDEPTH || info->bmhd.w == 0 || info->bmhd.h == 0)
        return ANM_ERR_INVALID;
    info->depth = info->bmhd.nplanes;
    info->bpr = (odt_u16)(((info->bmhd.w + 15) >> 4) << 1);
    info->frames = env->alloc(env->ctx, n * sizeof(struct anm_frame));
    if (!info->frames) return ANM_ERR_MEMORY;
    walk(data, len, info, info->frames, &n);
    info->nframes = n;
    if (!info->frames[0].body) {
        anm_free_info(env, info);
        return ANM_ERR_INVALID;
    }
    /* fps: 60 over the first delta's reltime (jiffies), 10 without one (ABI test) */
    for (i = 1; i < n && !rel; i++)
        if (info->frames[i].has_hd) rel = info->frames[i].hd.reltime;
    info->fps = rel ? (60 + rel / 2) / rel : 10;
    if (!info->fps) info->fps = 1;
    if (info->has_camg) info->camg = anm_mode_id(info);
    return ANM_OK;
}

void anm_free_info(const struct anm_env *env, struct anm_info *info)
{
    if (info->frames) env->free(env->ctx, info->frames);
    info->frames = 0;
    info->nframes = 0;
}

odt_u32 anm_mode_id(const struct anm_info *info)
{
    odt_u32 id = info->camg;
    if (!info->has_camg) {
        /* no CAMG: from the size (ABI test). HIRES 0x8000, LACE 0x0004 */
        id = 0;
        if (info->bmhd.w >= 640) id |= 0x8000;
        if (info->bmhd.h >= 400) id |= 0x0004;
        if (info->bmhd.nplanes == 6 && !(id & 0x8000)) id |= 0x0080;   /* 6 planes lores: EHB */
        return id;
    }
    /* the ILBM spec's cleaning: an old CAMG with junk in the upper word,
     * then the bits that aren't the mode's (SPRITES, VP_HIDE, GENLOCK) */
    if ((id & 0xffff0000UL) && !(id & 0x1000)) id &= 0xffff;
    if (!(id & 0xffff0000UL)) id &= ~(0x4000UL | 0x2000UL | 0x0100UL | 0x0002UL);
    return id;
}

odt_u32 anm_colors(const struct anm_info *info, const odt_u8 *data, odt_u32 n, odt_u8 *rgb)
{
    const struct anm_frame *f;
    odt_u32 count, i;
    if (n >= info->nframes) n = info->nframes ? info->nframes - 1 : 0;
    if (!info->nframes) return 0;
    f = &info->frames[n];
    if (!f->cmap) return 0;
    count = f->cmap_len / 3;
    if (count > 256) count = 256;
    for (i = 0; i < count * 3; i++) rgb[i] = data[f->cmap + i];
    /* EHB: 32 more at half brightness (ABI test: OS 3.2 may leave them to the mode) */
    if ((anm_mode_id(info) & 0x0080) && count == 32 && info->depth == 6) {
        for (i = 0; i < 32 * 3; i++) rgb[96 + i] = rgb[i] >> 1;
        count = 64;
    }
    return count;
}

/* ---- BODY ---------------------------------------------------------------------- */

int anm_body(const struct anm_bmhd *h, odt_u32 bpr, odt_u32 depth, const odt_u8 *body, odt_u32 len, odt_u8 *planes)
{
    odt_u32 rows = h->h, src = 0, row, p, pl = depth + (h->masking == ANM_MSK_HASMASK ? 1 : 0);
    odt_u32 psize = rows * bpr;
    /* ByteRun1 runs may cross row ends: the stream is read as one (ABI test) */
    odt_s32 run = 0;                    /* > 0: literal bytes left; < 0: repeats left */
    odt_u8 rep = 0;
    if (h->compression > ANM_CMP_BYTERUN1) return ANM_ERR_COMP;
    for (row = 0; row < rows; row++) {
        for (p = 0; p < pl; p++) {
            odt_u8 *dst = p < depth ? planes + p * psize + row * bpr : 0;
            odt_u32 x = 0;
            while (x < bpr) {
                odt_u8 b;
                if (h->compression == ANM_CMP_NONE) {
                    if (src >= len) return ANM_ERR_SHORT;
                    b = body[src++];
                } else {
                    while (!run) {
                        odt_s8 c;
                        if (src >= len) return ANM_ERR_SHORT;
                        c = (odt_s8)body[src++];
                        if (c >= 0) run = c + 1;
                        else if (c != -128) {
                            if (src >= len) return ANM_ERR_SHORT;
                            rep = body[src++];
                            run = -(odt_s32)c + 1;
                            run = -run;
                        }
                    }
                    if (run > 0) {
                        if (src >= len) return ANM_ERR_SHORT;
                        b = body[src++];
                        run--;
                    } else {
                        b = rep;
                        run++;
                    }
                }
                if (dst) dst[x] = b;
                x++;
            }
        }
    }
    return ANM_OK;
}

/* ---- DLTA ---------------------------------------------------------------------- */

/* Byte vertical delta (op 5): per plane a list, per byte column an op count
 * then ops: 0 count value (same), 0x80|n then n bytes (unique), else skip. */
static int delta5(odt_u32 xor_, odt_u32 bpr, odt_u32 rows, odt_u32 depth, const odt_u8 *d, odt_u32 len,
                  odt_u8 *planes)
{
    odt_u32 p, psize = rows * bpr;
    if (len < 64) return ANM_ERR_SHORT;
    for (p = 0; p < depth; p++) {
        odt_u32 at = anm_be32(d + 4 * p), col;
        odt_u8 *plane = planes + p * psize;
        if (!at) continue;                              /* plane unchanged */
        if (at >= len) return ANM_ERR_SHORT;
        for (col = 0; col < bpr; col++) {
            odt_u32 ops, row = 0;
            odt_u8 *dst = plane + col;
            if (at >= len) return ANM_ERR_SHORT;
            ops = d[at++];
            while (ops--) {
                odt_u32 op, n, k;
                if (at >= len) return ANM_ERR_SHORT;
                op = d[at++];
                if (op == 0) {                          /* same */
                    odt_u8 v;
                    if (at + 2 > len) return ANM_ERR_SHORT;
                    n = d[at++];
                    v = d[at++];
                    for (k = 0; k < n; k++, row++)
                        if (row < rows) { if (xor_) dst[row * bpr] ^= v; else dst[row * bpr] = v; }
                } else if (op & 0x80) {                 /* unique */
                    n = op & 0x7f;
                    if (at + n > len) return ANM_ERR_SHORT;
                    for (k = 0; k < n; k++, row++) {
                        odt_u8 v = d[at++];
                        if (row < rows) { if (xor_) dst[row * bpr] ^= v; else dst[row * bpr] = v; }
                    }
                } else row += op;                       /* skip */
            }
        }
    }
    return ANM_OK;
}

/* Puts a unit of size bytes (2 or 4) at dst */
static void put_unit(odt_u8 *dst, const odt_u8 *src, odt_u32 size, odt_u32 xor_)
{
    odt_u32 i;
    for (i = 0; i < size; i++) { if (xor_) dst[i] ^= src[i]; else dst[i] = src[i]; }
}

/* The columns of a plane row in units of size: bpr / size, and a last
 * word column when long data doesn't fill the row (ABI test: as the
 * players do) */
static odt_u32 columns(odt_u32 bpr, odt_u32 size, odt_u32 *extra)
{
    *extra = (size == 4 && (bpr & 2)) ? 1 : 0;
    return bpr / size;
}

/* Short/long vertical delta with separate op and data lists (op 7) */
static int delta7(odt_u32 flags, odt_u32 bpr, odt_u32 rows, odt_u32 depth, const odt_u8 *d, odt_u32 len,
                  odt_u8 *planes)
{
    odt_u32 p, psize = rows * bpr, lsize = (flags & ANM_F_LONG) ? 4 : 2, xor_ = flags & ANM_F_XOR;
    if (len < 64) return ANM_ERR_SHORT;
    for (p = 0; p < depth; p++) {
        odt_u32 opat = anm_be32(d + 4 * p), dat = anm_be32(d + 32 + 4 * p), extra, ncol, col;
        odt_u8 *plane = planes + p * psize;
        if (!opat) continue;
        if (opat >= len || dat > len) return ANM_ERR_SHORT;
        ncol = columns(bpr, lsize, &extra);
        for (col = 0; col < ncol + extra; col++) {
            odt_u32 size = col < ncol ? lsize : 2, x = col < ncol ? col * lsize : ncol * lsize;
            odt_u32 ops, row = 0;
            if (opat >= len) return ANM_ERR_SHORT;
            ops = d[opat++];
            while (ops--) {
                odt_u32 op, n, k;
                if (opat >= len) return ANM_ERR_SHORT;
                op = d[opat++];
                if (op == 0) {
                    if (opat >= len || dat + size > len) return ANM_ERR_SHORT;
                    n = d[opat++];
                    for (k = 0; k < n; k++, row++)
                        if (row < rows) put_unit(plane + row * bpr + x, d + dat, size, xor_);
                    dat += size;
                } else if (op & 0x80) {
                    n = op & 0x7f;
                    if (dat + n * size > len) return ANM_ERR_SHORT;
                    for (k = 0; k < n; k++, row++, dat += size)
                        if (row < rows) put_unit(plane + row * bpr + x, d + dat, size, xor_);
                } else row += op;
            }
        }
    }
    return ANM_OK;
}

static odt_u32 get_unit(const odt_u8 *p, odt_u32 size) { return size == 4 ? anm_be32(p) : anm_be16(p); }

/* Short/long vertical delta, ops and data together (op 8) */
static int delta8(odt_u32 flags, odt_u32 bpr, odt_u32 rows, odt_u32 depth, const odt_u8 *d, odt_u32 len,
                  odt_u8 *planes)
{
    odt_u32 p, psize = rows * bpr, lsize = (flags & ANM_F_LONG) ? 4 : 2, xor_ = flags & ANM_F_XOR;
    if (len < 64) return ANM_ERR_SHORT;
    for (p = 0; p < depth; p++) {
        odt_u32 at = anm_be32(d + 4 * p), extra, ncol, col;
        odt_u8 *plane = planes + p * psize;
        if (!at) continue;
        if (at >= len) return ANM_ERR_SHORT;
        ncol = columns(bpr, lsize, &extra);
        for (col = 0; col < ncol + extra; col++) {
            odt_u32 size = col < ncol ? lsize : 2, x = col < ncol ? col * lsize : ncol * lsize;
            odt_u32 hibit = size == 4 ? 0x80000000UL : 0x8000UL;
            odt_u32 ops, row = 0;
            if (at + size > len) return ANM_ERR_SHORT;
            ops = get_unit(d + at, size);
            at += size;
            while (ops--) {
                odt_u32 op, n, k;
                if (at + size > len) return ANM_ERR_SHORT;
                op = get_unit(d + at, size);
                at += size;
                if (op == 0) {
                    if (at + 2 * size > len) return ANM_ERR_SHORT;
                    n = get_unit(d + at, size);
                    at += size;
                    for (k = 0; k < n && row < rows; k++, row++)
                        put_unit(plane + row * bpr + x, d + at, size, xor_);
                    row += n - k;
                    at += size;
                } else if (op & hibit) {
                    n = op & (hibit - 1);
                    if (n > (len - at) / size) return ANM_ERR_SHORT;
                    for (k = 0; k < n; k++, row++, at += size)
                        if (row < rows) put_unit(plane + row * bpr + x, d + at, size, xor_);
                } else row += op;
            }
        }
    }
    return ANM_OK;
}

int anm_delta(odt_u32 op, odt_u32 flags, odt_u32 bpr, odt_u32 rows, odt_u32 depth, const odt_u8 *dlta, odt_u32 len,
              odt_u8 *planes)
{
    switch (op) {
    case 5: return delta5(flags & ANM_F_XOR, bpr, rows, depth, dlta, len, planes);
    case 7: return delta7(flags, bpr, rows, depth, dlta, len, planes);
    case 8: return delta8(flags, bpr, rows, depth, dlta, len, planes);
    default: return ANM_ERR_COMP;
    }
}

/* ---- the decoder --------------------------------------------------------------- */

int anm_dec_init(const struct anm_env *env, struct anm_dec *d, const struct anm_info *info, const odt_u8 *data,
                 odt_u32 len)
{
    d->info = info;
    d->data = data;
    d->len = len;
    d->size = (odt_u32)info->depth * info->bmhd.h * info->bpr;
    d->cur = -1;
    d->decoded = 0;
    d->env = env;
    d->cp = 0;
    d->ncp = d->every = 0;
    d->err = ANM_OK;
    d->buf[0] = env->alloc(env->ctx, d->size);
    d->buf[1] = env->alloc(env->ctx, d->size);
    if (!d->buf[0] || !d->buf[1]) {
        anm_dec_free(env, d);
        return ANM_ERR_MEMORY;
    }
    /* the checkpoints: as many as ANM_CP_BYTES holds (two buffers each),
     * spread over the frames; none when one would not fit */
    if (d->size && d->size <= ANM_CP_BYTES / 2 && info->nframes > ANM_CP_MIN_EVERY) {
        odt_u32 most = ANM_CP_BYTES / 2 / d->size, every;
        if (most > ANM_CP_MAX) most = ANM_CP_MAX;
        every = info->nframes / (most + 1) + 1;
        if (every < ANM_CP_MIN_EVERY) every = ANM_CP_MIN_EVERY;
        d->ncp = (info->nframes - 1) / every;
        if (d->ncp > most) d->ncp = most;
        d->every = every;
        if (d->ncp && !(d->cp = env->alloc(env->ctx, d->ncp * (odt_u32)sizeof *d->cp))) d->ncp = 0;
    }
    return ANM_OK;
}

void anm_dec_free(const struct anm_env *env, struct anm_dec *d)
{
    odt_u32 j;
    if (d->buf[0]) env->free(env->ctx, d->buf[0]);
    if (d->buf[1]) env->free(env->ctx, d->buf[1]);
    d->buf[0] = d->buf[1] = 0;
    if (d->cp) {
        for (j = 0; j < d->ncp; j++)
            if (d->cp[j].buf) env->free(env->ctx, d->cp[j].buf);
        env->free(env->ctx, d->cp);
    }
    d->cp = 0;
    d->ncp = 0;
    d->cur = -1;
}

static void copy_bytes(odt_u8 *dst, const odt_u8 *src, odt_u32 n)
{
    while (n--) *dst++ = *src++;
}

/* Frame k from frames k - 1 (in buf[(k - 1) & 1]) and k - 2 (in buf[k & 1]) */
static int decode_next(struct anm_dec *d)
{
    const struct anm_info *info = d->info;
    odt_u32 k = (odt_u32)(d->cur + 1), i;
    const struct anm_frame *f = &info->frames[k];
    odt_u8 *dst = d->buf[k & 1], *prev = d->buf[(k - 1) & 1];
    int r;
    d->decoded++;
    d->cur = (odt_s32)k;
    if (k == 0 || (f->has_hd ? f->hd.op == 0 : f->body != 0)) {
        /* a whole picture (the first frame, or op 0) */
        if (k == 0) for (i = 0; i < d->size; i++) dst[i] = 0;
        else copy_bytes(dst, prev, d->size);     /* what a short BODY leaves out stays (ABI test) */
        r = f->body ? anm_body(&info->bmhd, info->bpr, info->depth, d->data + f->body, f->body_len, dst)
                    : ANM_ERR_INVALID;
    } else {
        /* a delta on frame k - 2 (interleave 0 or 2), k - 1 (interleave 1,
         * and always for frame 1); other interleaves as 2 (ABI test) */
        if (k == 1 || f->hd.interleave == 1) copy_bytes(dst, prev, d->size);
        if (!f->dlta) r = ANM_OK;                 /* no DLTA: the frame is its base (ABI test) */
        else r = anm_delta(f->hd.op, f->hd.flags, info->bpr, info->bmhd.h, info->depth, d->data + f->dlta,
                           f->dlta_len, dst);
    }
    if (r == ANM_ERR_SHORT) return ANM_OK;       /* a short frame shows what it has (ABI test) */
    if (r != ANM_OK) {
        /* a frame that can't be made shows the one before it (ABI test) */
        if (k == 0) for (i = 0; i < d->size; i++) dst[i] = 0;
        else copy_bytes(dst, prev, d->size);
    }
    return r;
}

/* After frame cur: keep it as checkpoint when it is one not kept yet (no
 * memory: not kept) */
static void keep_cp(struct anm_dec *d)
{
    odt_u32 k = (odt_u32)d->cur, j;
    struct anm_cp *c;
    if (!d->ncp || !k || k % d->every) return;
    j = k / d->every;
    if (j > d->ncp) return;
    c = &d->cp[j - 1];
    if (c->buf || !(c->buf = d->env->alloc(d->env->ctx, 2 * d->size))) return;
    copy_bytes(c->buf, d->buf[0], d->size);
    copy_bytes(c->buf + d->size, d->buf[1], d->size);
    c->err = d->err;
}

int anm_goto(struct anm_dec *d, odt_u32 n)
{
    int r = ANM_OK;
    if (n >= d->info->nframes) return ANM_ERR_INVALID;
    if (d->cur >= 0 && (odt_u32)d->cur > n) {
        /* behind: from the last checkpoint at or before n, else from the
         * start; its frames' first error is reported as if they were
         * decoded again */
        odt_u32 j = d->every ? n / d->every : 0;
        if (j > d->ncp) j = d->ncp;
        while (j && !d->cp[j - 1].buf) j--;
        if (j) {
            const struct anm_cp *c = &d->cp[j - 1];
            copy_bytes(d->buf[0], c->buf, d->size);
            copy_bytes(d->buf[1], c->buf + d->size, d->size);
            d->cur = (odt_s32)(j * d->every);
            d->err = r = c->err;
        } else {
            d->cur = -1;
            d->err = ANM_OK;
        }
    }
    while (d->cur < (odt_s32)n) {
        int r2 = decode_next(d);
        if (r2 != ANM_OK && r == ANM_OK) r = r2;
        if (r2 != ANM_OK && d->err == ANM_OK) d->err = r2;
        keep_cp(d);
    }
    return r;
}

void anm_copy_out(const struct anm_dec *d, odt_u8 *const *planes, odt_u32 dst_depth, odt_u32 modulo)
{
    const odt_u8 *src = anm_planes(d);
    odt_u32 p, row, x, rows = d->info->bmhd.h, bpr = d->info->bpr, n = bpr < modulo ? bpr : modulo;
    for (p = 0; p < dst_depth; p++) {
        odt_u8 *dst = planes[p];
        if (!dst) continue;
        for (row = 0; row < rows; row++)
            for (x = 0; x < n; x++)
                dst[row * modulo + x] = (src && p < d->info->depth) ? src[(p * rows + row) * bpr + x] : 0;
    }
}

odt_u32 anm_duration(const struct anm_info *info, odt_u32 n)
{
    odt_u32 rel = 0, t;
    /* frame n stays until frame n + 1, whose reltime says when; the last
     * frame by its own (ABI test) */
    if (n + 1 < info->nframes && info->frames[n + 1].has_hd) rel = info->frames[n + 1].hd.reltime;
    else if (n < info->nframes && info->frames[n].has_hd) rel = info->frames[n].hd.reltime;
    if (!rel) return 1;
    t = (rel * info->fps + 30) / 60;
    return t ? t : 1;
}

/* ---- the class ------------------------------------------------------------------- */

#define AEXT(env) ((const struct adt_ext *)(env)->class_ext)
#define ANM_MAXFILE (256UL << 20)

static void *aenv_alloc(void *ctx, odt_u32 n)
{
    const struct odt_cenv *env = ctx;
    return env->alloc(env, n);
}

static void aenv_free(void *ctx, void *p)
{
    const struct odt_cenv *env = ctx;
    env->free(env, p);
}

odt_s32 anm_read_source(const struct odt_cenv *env, void *obj, odt_u8 **data, odt_u32 *len)
{
    odt_u32 st = ODT_DTST_FILE, handle = 0, dtv = 0, fh, size = 0, n = 0, i;
    odt_u8 *buf = 0;
    *data = 0;
    *len = 0;
    dtc_get(env, obj, ODT_DTA_SourceType, &st);
    dtc_get(env, obj, ODT_DTA_Handle, &handle);
    if (st == ODT_DTST_MEMORY) {
        odt_u32 at = 0;
        dtc_get(env, obj, ODT_DTA_SourceAddress, &at);
        dtc_get(env, obj, ODT_DTA_SourceSize, &size);
        if (!at || !size) return ODT_DTERROR_NOT_ENOUGH_DATA;
        if (!(buf = env->alloc(env, size))) return DTC_ERROR_NO_FREE_STORE;
        for (i = 0; i < size; i++) buf[i] = ((const odt_u8 *)at)[i];
        *data = buf;
        *len = size;
        return 0;
    }
    if (st != ODT_DTST_FILE || !handle) return DTC_ERROR_OBJECT_WRONG_TYPE;  /* the clipboard: not yet */
    fh = handle;
    if (dtc_get(env, obj, ODT_DTA_DataType, &dtv) && dtv) {
        const struct odt_DataType *dt = (const struct odt_DataType *)dtv;
        /* an IFF type's handle is an IFFHandle; its iff_Stream is the file
         * (libraries/iffparse.h, InitIFFasDOS) */
        if (dt->dtn_Header && (dt->dtn_Header->dth_Flags & ODT_DTF_TYPE_MASK) == ODT_DTF_IFF)
            fh = *(const odt_u32 *)handle;
    }
    env->seek(env, fh, 0, -1);
    for (;;) {
        odt_s32 got;
        if (n == size) {
            odt_u32 nsize = size ? size * 2 : 65536;
            odt_u8 *q;
            if (nsize > ANM_MAXFILE || !(q = env->alloc(env, nsize))) {
                env->free(env, buf);
                return DTC_ERROR_NO_FREE_STORE;
            }
            for (i = 0; i < n; i++) q[i] = buf[i];
            env->free(env, buf);
            buf = q;
            size = nsize;
        }
        got = env->read(env, fh, buf + n, (odt_s32)(size - n));
        if (got < 0) {
            odt_s32 e = env->io_err(env);
            env->free(env, buf);
            return e ? e : ODT_DTERROR_NOT_ENOUGH_DATA;
        }
        if (got == 0) break;
        n += (odt_u32)got;
    }
    *data = buf;
    *len = n;
    return 0;
}

static struct adt_BitMap *frame_bitmap(const struct odt_cenv *env, struct anm_inst *in, struct adt_BitMap *bm)
{
    if (!bm) {
        if (!AEXT(env)->alloc_bitmap) return 0;
        bm = AEXT(env)->alloc_bitmap(env, in->info.bmhd.w, in->info.bmhd.h, in->info.depth, 0);
        if (!bm) return 0;
    }
    anm_copy_out(&in->dec, bm->Planes, bm->Depth > 8 ? 8 : bm->Depth, bm->BytesPerRow);
    return bm;
}

static void free_all(const struct odt_cenv *env, struct anm_inst *in)
{
    if (in->key && AEXT(env)->free_bitmap) AEXT(env)->free_bitmap(env, in->key);
    in->key = 0;
    anm_dec_free(&in->aenv, &in->dec);
    anm_free_info(&in->aenv, &in->info);
    env->free(env, in->data);
    in->data = 0;
    in->ready = 0;
}

static odt_s32 open_anim(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, struct anm_inst *in,
                         struct odt_GadgetInfo *gi)
{
    odt_s32 err;
    odt_u8 rgb[3 * 256];
    odt_u32 ncol, i, v = 0;
    struct odt_TagItem t[12];
    struct odt_opSet set;
    in->aenv.ctx = (void *)env;
    in->aenv.alloc = aenv_alloc;
    in->aenv.free = aenv_free;
    if ((err = anm_read_source(env, obj, &in->data, &in->len)) != 0) return err;
    if ((err = anm_scan(&in->aenv, in->data, in->len, &in->info)) != ANM_OK) return err;
    if ((err = anm_dec_init(&in->aenv, &in->dec, &in->info, in->data, in->len)) != ANM_OK) return err;
    if ((err = anm_goto(&in->dec, 0)) != ANM_OK) return err;
    if (!(in->key = frame_bitmap(env, in, 0))) return DTC_ERROR_NO_FREE_STORE;
    ncol = anm_colors(&in->info, in->data, 0, rgb);
    in->ready = 1;
    t[0].ti_Tag = ADT_ADTA_Width;           t[0].ti_Data = in->info.bmhd.w;
    t[1].ti_Tag = ADT_ADTA_Height;          t[1].ti_Data = in->info.bmhd.h;
    t[2].ti_Tag = ADT_ADTA_Depth;           t[2].ti_Data = in->info.depth;
    t[3].ti_Tag = ADT_ADTA_Frames;          t[3].ti_Data = in->info.nframes;
    t[4].ti_Tag = ADT_ADTA_FramesPerSecond; t[4].ti_Data = in->info.fps;
    t[5].ti_Tag = ADT_ADTA_ModeID;          t[5].ti_Data = anm_mode_id(&in->info);
    t[6].ti_Tag = ADT_ADTA_NumColors;       t[6].ti_Data = ncol;
    t[7].ti_Tag = ADT_ADTA_KeyFrame;        t[7].ti_Data = (odt_u32)in->key;
    t[8].ti_Tag = ODT_DTA_NominalHoriz;     t[8].ti_Data = in->info.bmhd.w;
    t[9].ti_Tag = ODT_DTA_NominalVert;      t[9].ti_Data = in->info.bmhd.h;
    t[10].ti_Tag = ODT_TAG_DONE;            t[10].ti_Data = 0;
    set.MethodID = ODT_OM_SET;
    set.ops_AttrList = t;
    set.ops_GInfo = gi;
    env->do_super(env, cl, obj, &set);
    /* the colours, into the tables animation.datatype made for them */
    if (ncol && dtc_get(env, obj, ADT_ADTA_ColorRegisters, &v) && v) {
        struct adt_ColorRegister *cr = (struct adt_ColorRegister *)v;
        odt_u32 *cregs = 0;
        v = 0;
        if (dtc_get(env, obj, ADT_ADTA_CRegs, &v)) cregs = (odt_u32 *)v;
        for (i = 0; i < ncol; i++) {
            cr[i].red = rgb[3 * i];
            cr[i].green = rgb[3 * i + 1];
            cr[i].blue = rgb[3 * i + 2];
            if (cregs) {
                cregs[3 * i] = rgb[3 * i] * 0x01010101UL;
                cregs[3 * i + 1] = rgb[3 * i + 1] * 0x01010101UL;
                cregs[3 * i + 2] = rgb[3 * i + 2] * 0x01010101UL;
            }
        }
    }
    return 0;
}

odt_u32 anm_inst_size(void) { return sizeof(struct anm_inst); }

odt_u32 anm_dispatch(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, void *msg)
{
    odt_u32 id = *(odt_u32 *)msg;
    struct anm_inst *in = (id == ODT_OM_NEW) ? 0 : ODT_INST_DATA(cl, obj);
    switch (id) {
    case ODT_OM_NEW: {
        struct odt_opSet *ops = msg;
        void *o = (void *)env->do_super(env, cl, obj, msg);
        odt_s32 err;
        if (!o) return 0;
        in = ODT_INST_DATA(cl, o);
        if (env->sem_init) env->sem_init(env, &in->lock);
        if ((err = open_anim(env, cl, o, in, ops->ops_GInfo)) != 0) {
            struct odt_dtGeneral d;
            free_all(env, in);
            d.MethodID = ODT_OM_DISPOSE;
            d.dtg_GInfo = 0;
            env->coerce(env, cl, o, &d);
            env->set_io_err(env, err);
            return 0;
        }
        return (odt_u32)o;
    }
    case ODT_OM_DISPOSE:
        /* ours go first: the object's memory goes with the superclass.
         * animation.datatype's dispose then gives its frames back to us
         * (ADTM_UNLOADFRAME), which needs no more than the bitmaps. */
        free_all(env, in);
        return env->do_super(env, cl, obj, msg);
    case ADT_ADTM_LOADFRAME:
    case ADT_ADTM_LOADNEWFORMATFRAME: {
        struct adt_Frame *f = msg;
        struct adt_BitMap *bm;
        int r;
        if (!in->ready || f->alf_Frame >= in->info.nframes) return 0;
        if (env->sem_obtain) env->sem_obtain(env, &in->lock);
        r = anm_goto(&in->dec, f->alf_Frame);
        bm = frame_bitmap(env, in, (struct adt_BitMap *)f->alf_BitMap);
        if (env->sem_release) env->sem_release(env, &in->lock);
        (void)r;                        /* a frame that couldn't be made shows the one before it */
        if (!bm) return 0;
        f->alf_BitMap = bm;
        f->alf_Duration = anm_duration(&in->info, f->alf_Frame);
        f->alf_CMap = 0;                /* colour changes in later frames: not yet */
        return 1;
    }
    case ADT_ADTM_UNLOADFRAME:
    case ADT_ADTM_UNLOADNEWFORMATFRAME: {
        struct adt_Frame *f = msg;
        if (f->alf_BitMap && f->alf_BitMap != in->key && AEXT(env)->free_bitmap)
            AEXT(env)->free_bitmap(env, (struct adt_BitMap *)f->alf_BitMap);
        f->alf_BitMap = 0;
        return 0;
    }
    default:
        return env->do_super(env, cl, obj, msg);
    }
}
