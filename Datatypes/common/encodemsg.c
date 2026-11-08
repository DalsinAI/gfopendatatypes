/*
 * gfopenimage datatypes: media.encode/1's requests and answers (encodemsg.h).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "encodemsg.h"

me_u32 me_get32(const unsigned char *p)
{
    return (me_u32)p[0] << 24 | (me_u32)p[1] << 16 | (me_u32)p[2] << 8 | p[3];
}

void me_put32(unsigned char *p, me_u32 v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

void me_pack_settings(unsigned char out[ME_SETTINGS_SIZE], const struct me_settings *s)
{
    me_put32(out, 1);
    me_put32(out + 4, s->format);
    me_put32(out + 8, s->width);
    me_put32(out + 12, s->height);
    me_put32(out + 16, s->pixels);
    me_put32(out + 20, s->fps1000);
    me_put32(out + 24, s->rate);
    me_put32(out + 28, s->channels);
    me_put32(out + 32, s->samples);
    me_put32(out + 36, s->quality);
    me_put32(out + 40, s->flags);
    me_put32(out + 44, 0);
}

void me_parse_info(const unsigned char in[ME_INFO_SIZE], struct me_info *i)
{
    i->format = me_get32(in);
    i->bytes = me_get32(in + 4);
    i->width = me_get32(in + 8);
    i->height = me_get32(in + 12);
    i->frames = me_get32(in + 16);
    i->sample_frames = me_get32(in + 20);
}

int me_format_kind(me_u32 format)
{
    switch (format) {
    case ME_FORMAT_HEIC:
        return ME_KIND_PICTURE;
    case ME_FORMAT_M4A: case ME_FORMAT_AAC: case ME_FORMAT_WMA:
        return ME_KIND_SOUND;
    case ME_FORMAT_HEVC: case ME_FORMAT_WMV:
        return ME_KIND_FILM;
    default:
        return 0;
    }
}

int me_offered(const unsigned char *list, me_u32 count, me_u32 format)
{
    me_u32 i;
    for (i = 0; i < count; i++)
        if (me_get32(list + i * 4) == format)
            return 1;
    return 0;
}

me_u32 me_pick_format(int kind, me_u32 asked, me_u32 own, me_u32 hint, int source)
{
    if (asked)
        return me_format_kind(asked) == kind ? asked : 0;
    if (own == ME_FORMAT_AAC && kind == ME_KIND_SOUND)
        return hint == ME_FORMAT_AAC ? ME_FORMAT_AAC : ME_FORMAT_M4A;   /* .aac stays ADTS; .m4a, .mp4 MP4 */
    if (own && me_format_kind(own) == kind)
        return own;
    if (!source)
        return kind == ME_KIND_PICTURE ? ME_FORMAT_HEIC : kind == ME_KIND_SOUND ? ME_FORMAT_M4A
             : kind == ME_KIND_FILM ? ME_FORMAT_HEVC : 0;
    return 0;
}

int me_hand_up(int kind, me_u32 asked)
{
    return kind == ME_KIND_FILM && asked && me_format_kind(asked) != ME_KIND_FILM;
}

me_u32 me_quality(long tag)
{
    if (tag == 0)
        return 1;                              /* the smallest file */
    return tag >= 1 && tag <= 100 ? (me_u32)tag : 0;
}

/* As sound.datatype: one buffer holding both channels only with no right
 * buffer and a stereo sample type. */
static int interleaved(const struct me_sound *s)
{
    return !s->right && (s->type == 1 || s->type == 3);
}

me_u32 me_sound_channels(const struct me_sound *s)
{
    return (s->right && s->right != s->left) || interleaved(s) ? 2 : 1;
}

void me_sound_s16(const struct me_sound *s, me_u32 first, me_u32 count, unsigned char *out)
{
    me_u32 channels = me_sound_channels(s), i, ch;
    int wide = s->type == 2 || s->type == 3, inter = interleaved(s);

    for (i = first; i < first + count; i++)
        for (ch = 0; ch < channels; ch++) {
            const signed char *src = ch && s->right ? s->right : s->left;
            me_u32 k = inter ? i * 2 + ch : i;
            if (!src || i >= s->length) {
                out[0] = out[1] = 0;
            } else if (wide) {
                out[0] = (unsigned char)src[k * 2];
                out[1] = (unsigned char)src[k * 2 + 1];
            } else {
                out[0] = (unsigned char)src[k];
                out[1] = 0;
            }
            out += 2;
        }
}

me_u32 me_strip_rows(me_u32 width, me_u32 bytes_per_pixel, me_u32 height, me_u32 budget)
{
    me_u32 row = width * bytes_per_pixel, rows = row ? budget / row : height;
    if (rows < 1)
        rows = 1;
    return rows < height ? rows : height;
}
