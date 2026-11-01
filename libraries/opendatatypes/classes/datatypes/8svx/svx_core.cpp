/* GFOpen8SVX: 8svx.datatype's class, lean C++ with no Amiga headers.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * Clean-room, from the IFF 8SVX specification (EA, public), the sound_dtc
 * and datatypes autodocs and the NDK 3.2 headers' layouts; never from
 * Kickstart, Hyperion or AROS code, nor any third-party datatype's.
 *
 * At OM_NEW, after sound.datatype and datatypesclass have made the object,
 * the class parses the IFFHandle (DTA_Handle) through the env's iffparse:
 * VHDR, CHAN and the text chunks as properties, stopping at the BODY. It
 * takes the first octave (oneShotHiSamples + repeatHiSamples, or the whole
 * BODY when both are 0), unpacks Fibonacci delta (sCmpFibDelta), splits a
 * stereo BODY (CHAN 6: left, then right) and hands the samples to
 * sound.datatype, which frees them. DTM_WRITE is sound.datatype's 8SVX
 * writer for both DTWM_IFF and DTWM_RAW.
 *
 * "ABI test" marks a corner the specification leaves open.
 */
#include "svx_core.h"

namespace {

using u8 = odt_u8;
using u32 = odt_u32;
using s32 = odt_s32;
using s8 = odt_s8;
using env_t = const odt_cenv;

template <class T> constexpr T min(T a, T b) { return a < b ? a : b; }
inline u32 method(void *msg) { return *static_cast<u32 *>(msg); }

void set_err(env_t *env, s32 e)
{
    if (env->set_io_err)
        env->set_io_err(env, e);
}

const u32 props[] = { SVX_ID_VHDR, SVX_ID_CHAN, SVX_ID_NAME, SVX_ID_AUTH, SVX_ID_ANNO, SVX_ID_COPY };

const s8 fib[16] = { -34, -21, -13, -8, -5, -3, -2, -1, 0, 1, 2, 3, 5, 8, 13, 21 };

void set_text(env_t *env, odt_IClass *cl, void *obj, void *iff, u32 id, u32 attr)
{
    s32 n = 0;
    const char *s = static_cast<const char *>(env->iff_find_prop(env, iff, SVX_ID_8SVX, id, &n));
    if (!s || n <= 0)
        return;
    char *c = static_cast<char *>(env->alloc(env, static_cast<u32>(n) + 1));
    if (!c)
        return;
    for (s32 i = 0; i < n; i++)
        c[i] = s[i];
    odt_TagItem t[] = { { attr, reinterpret_cast<u32>(c) }, { ODT_TAG_DONE, 0 } };
    dtc_set_super(env, cl, obj, t);
    env->free(env, c);
}

u32 load(env_t *env, odt_IClass *cl, void *obj)
{
    u32 st = 0, handle = 0;
    dtc_get_super(env, cl, obj, ODT_DTA_SourceType, &st);
    dtc_get_super(env, cl, obj, ODT_DTA_Handle, &handle);
    if (st == ODT_DTST_RAM)
        return 0;
    void *iff = reinterpret_cast<void *>(handle);
    if (!iff || !env->iff_parse || !env->iff_prop || !env->iff_stop || !env->iff_find_prop || !env->iff_current ||
        !env->iff_read_chunk)
        return DTC_ERROR_REQUIRED_ARG_MISSING;
    for (u32 id : props)
        env->iff_prop(env, iff, SVX_ID_8SVX, id);
    env->iff_stop(env, iff, SVX_ID_8SVX, SVX_ID_BODY);
    s32 r = env->iff_parse(env, iff, 0);
    if (r)
        return r == -1 ? ODT_DTERROR_NOT_ENOUGH_DATA : ODT_DTERROR_INVALID_DATA;
    u32 type = 0;
    s32 bodylen = 0;
    if (env->iff_current(env, iff, &type, &bodylen) != SVX_ID_BODY)
        return ODT_DTERROR_NOT_ENOUGH_DATA;
    s32 n = 0;
    const u8 *vh = static_cast<const u8 *>(env->iff_find_prop(env, iff, SVX_ID_8SVX, SVX_ID_VHDR, &n));
    if (!vh || n < 20)
        return ODT_DTERROR_INVALID_DATA; /* ABI test: OS 3.2's error for a missing VHDR */
    u32 oneshot = dtc_be32(vh), repeat = dtc_be32(vh + 4), percycle = dtc_be32(vh + 8);
    u32 rate = dtc_be16(vh + 12), octaves = vh[14], cmp = vh[15], vol = dtc_be32(vh + 16);
    if (cmp > SND_CMP_FIBDELTA)
        return ODT_DTERROR_UNKNOWN_COMPRESSION; /* ABI test: exponential delta (2) */
    s32 cn = 0;
    const u8 *chan = static_cast<const u8 *>(env->iff_find_prop(env, iff, SVX_ID_8SVX, SVX_ID_CHAN, &cn));
    u32 channel = chan && cn >= 4 ? dtc_be32(chan) : 0;
    u32 nch = channel == SVX_STEREO ? 2 : 1;

    u32 blen = bodylen > 0 ? static_cast<u32>(bodylen) : 0;
    u8 *body = static_cast<u8 *>(env->alloc(env, blen + 1));
    if (!body)
        return DTC_ERROR_NO_FREE_STORE;
    s32 got = env->iff_read_chunk(env, iff, body, static_cast<s32>(blen));
    if (got < 0) {
        env->free(env, body);
        return ODT_DTERROR_NOT_ENOUGH_DATA;
    }
    u32 half = static_cast<u32>(got) / nch;
    u32 avail = cmp == SND_CMP_FIBDELTA ? (half >= 2 ? 2 * (half - 2) : 0) : half;
    /* the first octave; ABI test: OS 3.2's length when VHDR asks for more than the BODY has */
    u32 want = oneshot + repeat;
    u32 len = want ? min(want, avail) : avail;
    s8 *ch[2] = { nullptr, nullptr };
    for (u32 c = 0; c < nch; c++) {
        ch[c] = static_cast<s8 *>(env->alloc(env, len ? len : 1));
        if (!ch[c]) {
            env->free(env, ch[0]);
            env->free(env, body);
            return DTC_ERROR_NO_FREE_STORE;
        }
        const u8 *src = body + c * half;
        if (cmp == SND_CMP_FIBDELTA) {
            s8 *tmp = static_cast<s8 *>(env->alloc(env, avail ? avail : 1));
            if (!tmp) {
                env->free(env, ch[0]);
                env->free(env, ch[1]);
                env->free(env, body);
                return DTC_ERROR_NO_FREE_STORE;
            }
            svx_fib_unpack(src, half, tmp);
            for (u32 i = 0; i < len; i++)
                ch[c][i] = tmp[i];
            env->free(env, tmp);
        } else {
            for (u32 i = 0; i < len; i++)
                ch[c][i] = static_cast<s8>(src[i]);
        }
    }
    env->free(env, body);

    u32 vhp = 0;
    if (dtc_get_super(env, cl, obj, SND_SDTA_VoiceHeader, &vhp) && vhp) {
        snd_VoiceHeader *v = reinterpret_cast<snd_VoiceHeader *>(vhp);
        v->vh_OneShotHiSamples = oneshot;
        v->vh_RepeatHiSamples = repeat;
        v->vh_SamplesPerHiCycle = percycle;
        v->vh_SamplesPerSec = static_cast<odt_u16>(rate);
        v->vh_Octaves = static_cast<u8>(octaves);
        v->vh_Compression = static_cast<u8>(cmp);
        v->vh_Volume = vol;
    }
    odt_TagItem t[8];
    t[0].ti_Tag = nch == 2 ? SND_SDTA_LeftSample : SND_SDTA_Sample;
    t[0].ti_Data = reinterpret_cast<u32>(ch[0]);
    t[1].ti_Tag = nch == 2 ? SND_SDTA_RightSample : ODT_TAG_IGNORE;
    t[1].ti_Data = reinterpret_cast<u32>(ch[1]);
    t[2].ti_Tag = SND_SDTA_SampleLength;
    t[2].ti_Data = len;
    t[3].ti_Tag = SND_SDTA_SamplesPerSec;
    t[3].ti_Data = rate ? rate : 8000;  /* ABI test: a VHDR rate of 0 */
    t[4].ti_Tag = SND_SDTA_Volume;
    t[4].ti_Data = vol >= SND_UNITY ? 64 : vol * 64 / SND_UNITY;
    t[5].ti_Tag = SND_SDTA_Cycles;
    t[5].ti_Data = 1;
    /* ABI test: CHAN LEFT or RIGHT as SDTA_Pan */
    t[6].ti_Tag = channel == SVX_LEFT || channel == SVX_RIGHT ? SND_SDTA_Pan : ODT_TAG_IGNORE;
    t[6].ti_Data = channel == SVX_RIGHT ? SND_UNITY : 0;
    t[7].ti_Tag = ODT_TAG_DONE;
    t[7].ti_Data = 0;
    dtc_set_super(env, cl, obj, t);
    set_text(env, cl, obj, iff, SVX_ID_NAME, ODT_DTA_ObjName);
    set_text(env, cl, obj, iff, SVX_ID_AUTH, ODT_DTA_ObjAuthor);
    set_text(env, cl, obj, iff, SVX_ID_ANNO, ODT_DTA_ObjAnnotation);
    set_text(env, cl, obj, iff, SVX_ID_COPY, ODT_DTA_ObjCopyright);
    return 0;
}

} // namespace

extern "C" odt_u32 svx_fib_unpack(const odt_u8 *in, odt_u32 n, odt_s8 *out)
{
    if (n < 2)
        return 0;
    s8 x = static_cast<s8>(in[1]);
    u32 count = 2 * (n - 2);
    for (u32 i = 0; i < count; i++) {
        u8 d = in[2 + (i >> 1)];
        d = (i & 1) ? d & 0x0f : d >> 4;
        x = static_cast<s8>(x + fib[d]);
        out[i] = x;
    }
    return count;
}

extern "C" odt_u32 svx_inst_size(void) { return 0; }

extern "C" odt_u32 svx_dispatch(const odt_cenv *env, odt_IClass *cl, void *obj, void *msg)
{
    switch (method(msg)) {
    case ODT_OM_NEW: {
        void *o = reinterpret_cast<void *>(env->do_super(env, cl, obj, msg));
        if (!o)
            return 0;
        u32 err = load(env, cl, o);
        if (err) {
            odt_u32 dispose = ODT_OM_DISPOSE;
            env->coerce(env, cl, o, &dispose);
            set_err(env, static_cast<s32>(err));
            return 0;
        }
        return reinterpret_cast<u32>(o);
    }
    case ODT_DTM_WRITE: {
        odt_dtWrite w = *static_cast<odt_dtWrite *>(msg);
        w.dtw_Mode = ODT_DTWM_IFF;
        return env->do_super(env, cl, obj, &w);
    }
    }
    return env->do_super(env, cl, obj, msg);
}
