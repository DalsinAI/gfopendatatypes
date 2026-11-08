/*
 * cdxl.datatype's core (cdxl_core.h). Written from the CDXL header layout
 * as cdxl_core.h gives it; no other player's code.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "cdxl_core.h"
#include "cdxl_cube_tables.h"
#include "../../common/include/odt_planar.h"

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] << 8 | p[1]);
}

int cdxl_parse(const uint8_t head[CDXL_HEAD], struct cdxl_frame *f)
{
    uint16_t palbytes = get16(head + 20);

    if (head[0] != 1)
        return -1;
    f->size = get32(head + 2);
    f->width = get16(head + 14);
    f->height = get16(head + 16);
    f->planes = head[19];
    f->ham = (head[1] & 7) == 1;
    f->layout = head[1] & 0xe0;
    f->stereo = (head[1] & 0x10) != 0;
    f->colours = palbytes / 2;
    f->audio = get16(head + 22);
    f->rate = get16(head + 24);
    f->fps = head[26];
    if ((head[1] & 7) > 1 || !f->width || !f->height || f->width > 4096 || f->height > 4096
        || (palbytes & 1) || f->colours > 256)
        return -1;
    if (f->layout == CDXL_CHUNKY) {
        if (f->planes != 24 || f->ham)
            return -1;
    } else if (f->layout != CDXL_PLANAR && f->layout != CDXL_LINE)
        return -1;
    else if (!f->planes || f->planes > 8 || (f->ham && f->planes != 6 && f->planes != 8))
        return -1;
    if (f->size < cdxl_audio_at(f) + (uint32_t)f->audio * (f->stereo ? 2 : 1))
        return -1;
    return 0;
}

uint32_t cdxl_rowbytes(const struct cdxl_frame *f)
{
    return (uint32_t)(f->width + 15) / 16 * 2;
}

uint32_t cdxl_image_bytes(const struct cdxl_frame *f)
{
    if (f->layout == CDXL_CHUNKY)
        return (uint32_t)f->width * f->height * 3;
    return cdxl_rowbytes(f) * f->height * f->planes;
}

uint32_t cdxl_palette_at(const struct cdxl_frame *f)
{
    (void)f;
    return CDXL_HEAD;
}

uint32_t cdxl_image_at(const struct cdxl_frame *f)
{
    return CDXL_HEAD + 2u * f->colours;
}

uint32_t cdxl_audio_at(const struct cdxl_frame *f)
{
    return cdxl_image_at(f) + cdxl_image_bytes(f);
}

uint32_t cdxl_plane_row(const struct cdxl_frame *f, unsigned p, unsigned y)
{
    uint32_t row = cdxl_rowbytes(f);
    if (f->layout == CDXL_LINE)
        return ((uint32_t)y * f->planes + p) * row;
    return ((uint32_t)p * f->height + y) * row;
}

void cdxl_to_chunky(const uint8_t *frame, const struct cdxl_frame *f, uint8_t *out)
{
    const uint8_t *img = frame + cdxl_image_at(f), *rows[8];
    unsigned y, p;

    /* 8 pixels at a time; a pixel is a byte, so planes past 8 never showed */
    for (y = 0; y < f->height; y++, out += f->width) {
        for (p = 0; p < f->planes && p < 8; p++)
            rows[p] = img + cdxl_plane_row(f, p, y);
        odt_planar_row(rows, p, f->width, out, 1);
    }
}

void cdxl_palette(const uint8_t *frame, const struct cdxl_frame *f, uint8_t *rgb)
{
    const uint8_t *p = frame + cdxl_palette_at(f);
    unsigned i;

    for (i = 0; i < f->colours; i++, p += 2) {
        *rgb++ = (uint8_t)((p[0] & 15) * 17);
        *rgb++ = (uint8_t)((p[1] >> 4) * 17);
        *rgb++ = (uint8_t)((p[1] & 15) * 17);
    }
}

static const uint8_t cdxl_bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };

uint8_t cdxl_cube_pixel(unsigned d, unsigned r, unsigned g, unsigned b)
{
    const uint8_t *t = cdxl_cube[d & 15];
    return (uint8_t)(t[r & 255] + t[256 + (g & 255)] + t[512 + (b & 255)]);
}

void cdxl_rgb_to_cube(const uint8_t *frame, const struct cdxl_frame *f, uint8_t *out)
{
    const uint8_t *s = frame + cdxl_image_at(f);
    unsigned x, y;

    /* the cube's parts from the tables (cdxl_cube_tables.h), a row of them
     * for each of the four dither values on a line: four pixels a turn */
    for (y = 0; y < f->height; y++) {
        const uint8_t *b = cdxl_bayer[y & 3];
        const uint8_t *t0 = cdxl_cube[b[0]], *t1 = cdxl_cube[b[1]], *t2 = cdxl_cube[b[2]], *t3 = cdxl_cube[b[3]];
        for (x = 0; x + 4 <= f->width; x += 4, s += 12, out += 4) {
            out[0] = (uint8_t)(t0[s[0]] + t0[256 + s[1]] + t0[512 + s[2]]);
            out[1] = (uint8_t)(t1[s[3]] + t1[256 + s[4]] + t1[512 + s[5]]);
            out[2] = (uint8_t)(t2[s[6]] + t2[256 + s[7]] + t2[512 + s[8]]);
            out[3] = (uint8_t)(t3[s[9]] + t3[256 + s[10]] + t3[512 + s[11]]);
        }
        for (; x < f->width; x++, s += 3) {
            const uint8_t *t = cdxl_cube[b[x & 3]];
            *out++ = (uint8_t)(t[s[0]] + t[256 + s[1]] + t[512 + s[2]]);
        }
    }
}

void cdxl_mono(const uint8_t *frame, const struct cdxl_frame *f, int8_t *out)
{
    const int8_t *l = (const int8_t *)(frame + cdxl_audio_at(f)), *r = l + f->audio;
    unsigned i;

    for (i = 0; i < f->audio; i++)
        out[i] = f->stereo ? (int8_t)((l[i] + r[i]) >> 1) : l[i];
}

unsigned cdxl_rate(const struct cdxl_frame *f)
{
    return f->rate ? f->rate : 11025;
}

unsigned cdxl_fps(const struct cdxl_frame *f)
{
    unsigned fps = f->fps;
    if (!fps && f->audio)
        fps = (cdxl_rate(f) + f->audio / 2) / f->audio;
    if (!fps || fps > 60)
        fps = 12;
    return fps;
}

void cdxl_show(const uint8_t *frame, const struct cdxl_frame *f, uint8_t *rgb)
{
    uint8_t pal[256 * 3];
    const uint8_t *img = frame + cdxl_image_at(f);
    unsigned x, y, p, i;

    if (f->layout == CDXL_CHUNKY) {
        for (i = 0; i < (unsigned)f->width * f->height * 3; i++)
            rgb[i] = img[i];
        return;
    }
    for (i = 0; i < 256 * 3; i++)
        pal[i] = 0;
    cdxl_palette(frame, f, pal);
    for (y = 0; y < f->height; y++) {
        unsigned r = pal[0], g = pal[1], b = pal[2];
        for (x = 0; x < f->width; x++) {
            unsigned v = 0;
            for (p = 0; p < f->planes; p++)
                if (img[cdxl_plane_row(f, p, y) + (x >> 3)] & (0x80 >> (x & 7)))
                    v |= 1u << p;
            if (!f->ham) {
                r = pal[v * 3];
                g = pal[v * 3 + 1];
                b = pal[v * 3 + 2];
            } else {
                unsigned bits = f->planes - 2, c = v >> bits, d = v & ((1u << bits) - 1);
                /* HAM6 sets a channel's four bits, HAM8 its top six */
                unsigned m = bits == 4 ? d * 17 : d << 2;
                if (c == 0) {
                    r = pal[d * 3];
                    g = pal[d * 3 + 1];
                    b = pal[d * 3 + 2];
                } else if (c == 1)
                    b = bits == 4 ? m : m | (b & 3);
                else if (c == 2)
                    r = bits == 4 ? m : m | (r & 3);
                else
                    g = bits == 4 ? m : m | (g & 3);
            }
            *rgb++ = (uint8_t)r;
            *rgb++ = (uint8_t)g;
            *rgb++ = (uint8_t)b;
        }
    }
}

/* ---- where the frames are ------------------------------------------------- */

int cdxl_same(const struct cdxl_frame *a, const struct cdxl_frame *b)
{
    return a->width == b->width && a->height == b->height && a->planes == b->planes && a->layout == b->layout
           && a->ham == b->ham;
}

/* The most sound a frame of size bytes like f can hold, a channel */
static uint32_t audio_room(const struct cdxl_frame *f, uint32_t size)
{
    uint32_t used = CDXL_HEAD + cdxl_image_bytes(f);
    return size > used ? (size - used) / (f->stereo ? 2u : 1u) : 0;
}

static int index_quick(const struct cdxl_io *io, struct cdxl_index *ix)
{
    uint8_t head[CDXL_HEAD];
    struct cdxl_frame last;
    long len = io->size ? io->size(io->ctx) : -1;
    uint32_t n, size = ix->first.size;
    if (len < 0 || !size || (uint32_t)len < size)
        return -1;
    n = (uint32_t)len / size;
    if ((uint32_t)len % size)
        return -1;
    if (n > 1 && (io->read(io->ctx, (n - 1) * size, head, CDXL_HEAD) != CDXL_HEAD || cdxl_parse(head, &last)
                  || last.size != size || !cdxl_same(&last, &ix->first)))
        return -1;
    ix->frames = n;
    ix->fixed = size;
    ix->max_size = size;
    ix->max_audio = audio_room(&ix->first, size);
    return 0;
}

static int index_scan(const struct cdxl_io *io, struct cdxl_index *ix)
{
    uint8_t head[CDXL_HEAD];
    struct cdxl_frame f;
    uint32_t at = 0, cap = 0, n = 0;
    ix->fixed = 0;
    ix->max_size = ix->max_audio = 0;
    if (ix->offsets)
        io->free(io->ctx, ix->offsets);
    ix->offsets = 0;
    for (;;) {
        if (io->read(io->ctx, at, head, CDXL_HEAD) != CDXL_HEAD || cdxl_parse(head, &f))
            break;
        if (n && !cdxl_same(&f, &ix->first))
            break;
        if (at + f.size < at)
            break;
        if (n + 1 >= cap) {
            uint32_t *more, k;
            cap = cap ? cap * 2 : 256;
            if (!(more = io->alloc(io->ctx, cap * (uint32_t)sizeof *more)))
                break;
            for (k = 0; k < n; k++)
                more[k] = ix->offsets[k];
            if (ix->offsets)
                io->free(io->ctx, ix->offsets);
            ix->offsets = more;
        }
        if (!n)
            ix->first = f;
        ix->offsets[n++] = at;
        if (f.size > ix->max_size)
            ix->max_size = f.size;
        if (f.audio > ix->max_audio)
            ix->max_audio = f.audio;
        at += f.size;
        ix->offsets[n] = at;
    }
    ix->frames = n;
    return n ? 0 : -1;
}

int cdxl_index_open(const struct cdxl_io *io, struct cdxl_index *ix)
{
    uint8_t head[CDXL_HEAD];
    ix->frames = ix->fixed = ix->max_size = ix->max_audio = 0;
    ix->offsets = 0;
    if (io->read(io->ctx, 0, head, CDXL_HEAD) != CDXL_HEAD || cdxl_parse(head, &ix->first))
        return -1;
    if (!index_quick(io, ix))
        return 0;
    return index_scan(io, ix);
}

void cdxl_index_free(const struct cdxl_io *io, struct cdxl_index *ix)
{
    if (ix->offsets)
        io->free(io->ctx, ix->offsets);
    ix->offsets = 0;
    ix->frames = 0;
}

int cdxl_read_frame(const struct cdxl_io *io, struct cdxl_index *ix, uint32_t i, uint8_t *buf, struct cdxl_frame *f)
{
    uint32_t at, size;
    int again = ix->fixed != 0;
    for (;;) {
        if (i >= ix->frames)
            return -1;
        at = ix->fixed ? i * ix->fixed : ix->offsets[i];
        size = ix->fixed ? ix->fixed : ix->offsets[i + 1] - at;
        if (size >= CDXL_HEAD && size <= ix->max_size && io->read(io->ctx, at, buf, size) == (long)size
            && !cdxl_parse(buf, f) && f->size == size && cdxl_same(f, &ix->first))
            return 0;
        /* a frame found in constant time and not one like the first: the
         * frames' sizes vary; frame by frame then, once */
        if (!again || index_scan(io, ix))
            return -1;
        again = 0;
    }
}
