/*
 * cdxl.datatype's core (cdxl_core.h). Written from the CDXL header layout
 * as cdxl_core.h gives it; no other player's code.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "cdxl_core.h"
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

void cdxl_rgb_to_cube(const uint8_t *frame, const struct cdxl_frame *f, uint8_t *out)
{
    static const uint8_t bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
    const uint8_t *s = frame + cdxl_image_at(f);
    unsigned x, y;

    for (y = 0; y < f->height; y++)
        for (x = 0; x < f->width; x++, s += 3) {
            unsigned d = bayer[y & 3][x & 3] * 16;
            *out++ = (uint8_t)(((s[0] * 5 + d) >> 8) * 36 + ((s[1] * 5 + d) >> 8) * 6 + ((s[2] * 5 + d) >> 8));
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
