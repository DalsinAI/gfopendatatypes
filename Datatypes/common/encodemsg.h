/*
 * gfopenimage datatypes: the media.encode/1 service's requests and answers
 * (DalsinAI/gfopenservice docs/MEDIA_ENCODE.md), and the choices a
 * datatype makes before it sends them: which file to write, and the
 * object's samples as the service takes them. Plain C, no Amiga headers and
 * no writable data, so the host tests run it as it runs on the 68k
 * (tests/test-encodemsg.sh).
 *
 * HEIC, AAC/M4A, WMA, HEVC and WMV are written by the service's FFmpeg, in
 * its own process on the host (the Team's ruling, 6 November 2026,
 * docs/DESIGN.md): nothing here encodes, it only describes.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef ENCODEMSG_H
#define ENCODEMSG_H

typedef unsigned int me_u32;         /* 32 bits on the 68k and on the hosts */

#define ME_SERVICE "media.encode/1"

/* Ops: from 16, clear of media.decode/1's. */
#define ME_FORMATS 16   /* buf1 (out) the formats, big-endian u32s; result how many */
#define ME_BEGIN   17   /* buf0 the settings -> result the job */
#define ME_PUT     18   /* arg the job, extra[0] the stream, buf0 the bytes -> result the stream's bytes so far */
#define ME_FINISH  19   /* arg the job -> buf1 info; result the file's bytes; aux ME_AUX_WORKING: send again */
#define ME_READ    20   /* arg the job, extra[0] the offset -> buf1 a piece; result its bytes (0 at the end) */
#define ME_CLOSE   21   /* arg the job */

#define ME_AUX_WORKING 1

#define ME_STREAM_PICTURE 0
#define ME_STREAM_SOUND   1

#define ME_SETTINGS_SIZE 48
#define ME_INFO_SIZE 24

#define ME_PIX_ARGB 0
#define ME_PIX_RGB  1
#define ME_SAMPLE_S16 0
#define ME_SAMPLE_S8  1
#define ME_F_ALPHA 1

/* The files it writes, and the formats media.decode/1 names on reading. */
#define ME_FORMAT_HEIC 0x48454943u   /* 'HEIC' */
#define ME_FORMAT_M4A  0x4d344120u   /* 'M4A ': AAC in MP4 */
#define ME_FORMAT_AAC  0x41414320u   /* 'AAC ': AAC as ADTS; media.decode/1's name for any AAC */
#define ME_FORMAT_WMA  0x574d4120u   /* 'WMA ' */
#define ME_FORMAT_HEVC 0x48455643u   /* 'HEVC': HEVC in MP4 */
#define ME_FORMAT_WMV  0x574d5620u   /* 'WMV ' */

/* Kinds, as media.decode/1's. */
#define ME_KIND_PICTURE 1
#define ME_KIND_FILM    2
#define ME_KIND_SOUND   3

struct me_settings {
    me_u32 format, width, height, pixels, fps1000, rate, channels, samples, quality, flags;
};

/* The settings as BEGIN takes them: version 1, big-endian. */
void me_pack_settings(unsigned char out[ME_SETTINGS_SIZE], const struct me_settings *s);

struct me_info {
    me_u32 format, bytes, width, height, frames, sample_frames;
};
void me_parse_info(const unsigned char in[ME_INFO_SIZE], struct me_info *i);

me_u32 me_get32(const unsigned char *p);
void me_put32(unsigned char *p, me_u32 v);

/* The kind of file a format is (ME_KIND_*), 0 for one it doesn't write. */
int me_format_kind(me_u32 format);

/* Does FORMATS' answer (count big-endian u32s) hold format? */
int me_offered(const unsigned char *list, me_u32 count, me_u32 format);

/* The file to write, for an object of kind:
 *   asked     OIA_WriteFormat from DTM_WRITE's attributes, 0 when not given;
 *   own       the format media.decode/1 named when the object was read, 0 for none;
 *   hint      the object's name's extension as dt_name_hint() gives it ('AAC ');
 *   source    the object was read from a file (else it is one made to be written).
 * The format asked for when it is of this kind; else the object's own when
 * the service writes it (AAC as M4A, unless the name is .aac); else, for an
 * object with no source, the kind's first (HEIC, M4A, HEVC). 0 when none:
 * an AVIF, say, which media.encode/1 does not write. */
me_u32 me_pick_format(int kind, me_u32 asked, me_u32 own, me_u32 hint, int source);

/* OIA_Quality's value as the service takes it: 1 to 100, else 0 (the default). */
me_u32 me_quality(long tag);

/* A sound.datatype object's samples (SDTA_Sample or SDTA_LeftSample,
 * SDTA_RightSample, SDTA_SampleLength in sample frames, SDTA_SampleType:
 * 0 8-bit mono, 1 8-bit stereo interleaved, 2 16-bit mono, 3 16-bit stereo
 * interleaved, big-endian), read as sound.datatype reads them. */
struct me_sound {
    const signed char *left, *right;  /* right NULL or the same as left: one buffer */
    me_u32 length;
    me_u32 type;
};
me_u32 me_sound_channels(const struct me_sound *s);
/* Sample frames first .. first + count - 1 as 16-bit big-endian, the
 * channels interleaved: count x me_sound_channels() x 2 bytes into out.
 * 8-bit samples become their value x 256. */
void me_sound_s16(const struct me_sound *s, me_u32 first, me_u32 count, unsigned char *out);

/* Rows of a picture (or a film's frame) to send in one PUT: as many as fit
 * in budget bytes, at least one. */
me_u32 me_strip_rows(me_u32 width, me_u32 bytes_per_pixel, me_u32 height, me_u32 budget);

#endif
