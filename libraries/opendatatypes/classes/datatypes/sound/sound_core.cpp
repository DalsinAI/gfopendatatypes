/* GFOpenSound: sound.datatype's class, lean C++ with no Amiga headers.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * Clean-room, from the sound_dtc autodoc, datatypes/soundclass.h's
 * published layouts (NDK 3.2), the audio.device autodoc and OS 3.2's
 * answers measured by tests/SoundDTClassABITest; never from Kickstart,
 * Hyperion or AROS code.
 *
 * The sound keeps what its subclass or the program set (SDTA_Sample, or
 * SDTA_LeftSample and SDTA_RightSample; 8 or 16 bits, mono or interleaved
 * with SDTA_SampleType). To play, it copies the sample into chip memory
 * as 8 bits and makes a plan for audio.device: the channel sets to ask
 * for, then requests of at most 131070 bytes, at the period, volume and
 * cycles set; the shell's player process carries the plan out and signals
 * SDTA_SignalTask at the end. As a gadget it draws the waveform and, with
 * DTA_ControlPanel, a play and a stop button under it.
 *
 * "ABI test" marks a corner the autodocs leave open; the ABI test's PROBE
 * line records OS 3.2's answer.
 */
#include "sound_core.h"

namespace {

using u8 = odt_u8;
using u16 = odt_u16;
using u32 = odt_u32;
using s32 = odt_s32;
using s8 = odt_s8;
using env_t = const odt_cenv;

template <class T> constexpr T min(T a, T b) { return a < b ? a : b; }
template <class T> constexpr T max(T a, T b) { return a > b ? a : b; }

inline const dtc_ext *X(env_t *env) { return DTC_EXT(env); }
inline snd_inst *inst(odt_IClass *cl, void *o) { return static_cast<snd_inst *>(ODT_INST_DATA(cl, o)); }
inline u32 method(void *msg) { return *static_cast<u32 *>(msg); }

void set_err(env_t *env, s32 e)
{
    if (env->set_io_err)
        env->set_io_err(env, e);
}

u32 clock_of(env_t *env)
{
    u32 c = X(env) ? X(env)->audio_clock : 0;
    return c ? c : 3546895UL;
}

/* period and rate, each from the other, rounded */
u32 period_of(env_t *env, u32 rate) { return rate ? (clock_of(env) + rate / 2) / rate : 0; }

const odt_DTMethod triggers[] = {
    { const_cast<char *>("Play"), const_cast<char *>("PLAY"), ODT_STM_PLAY },
    { const_cast<char *>("Stop"), const_cast<char *>("STOP"), ODT_STM_STOP },
    { nullptr, nullptr, 0 },
};

bool interleaved(const snd_inst *p)
{
    return !p->right && (p->sample_type == SND_SDTST_S8S || p->sample_type == SND_SDTST_S16S);
}
bool stereo(const snd_inst *p) { return (p->right && p->right != p->left) || interleaved(p); }

void free_chip(env_t *env, snd_inst *p)
{
    env->free(env, p->chip[0]);
    env->free(env, p->chip[1]);
    p->chip[0] = p->chip[1] = nullptr;
}

void stop(env_t *env, snd_inst *p)
{
    if ((p->flags & SND_F_PLAYING) && X(env)->audio_stop)
        X(env)->audio_stop(env, &p->player); /* synchronous: the player is done with chip[] */
    p->flags &= ~SND_F_PLAYING;
    free_chip(env, p);
}

bool play(env_t *env, odt_IClass *cl, void *obj, snd_inst *p)
{
    stop(env, p);
    u32 cycles = p->cycles, rep = 0;
    /* ABI test: DTA_Repeat as cycles 0, for ever */
    if (dtc_get_super(env, cl, obj, ODT_DTA_Repeat, &rep) && rep)
        cycles = 0;
    if (!snd_make_plan(env, p, cycles) || !X(env)->audio_play)
        return false;
    if (!X(env)->audio_play(env, &p->player, &p->plan)) {
        free_chip(env, p);
        return false;
    }
    p->flags |= SND_F_PLAYING;
    return true;
}

void set_free_sample(env_t *env, snd_inst *p, s8 **slot, s8 *now)
{
    s8 *old = *slot;
    *slot = now;
    if (!old || old == now || !(p->flags & SND_F_FREE))
        return;
    if (old != p->left && old != p->right)
        env->free(env, old);
}

void update_replay(env_t *env, snd_inst *p)
{
    u32 rate = p->samples_per_sec ? p->samples_per_sec : (p->period ? clock_of(env) / p->period : 0);
    if (!rate) {
        p->replay.tv_secs = p->replay.tv_micro = 0;
        return;
    }
    p->replay.tv_secs = p->length / rate;
    u32 rest = p->length % rate;
    /* rest / rate of a second, in microseconds, without overflowing 32 bits */
    p->replay.tv_micro = (rest * 1000UL / rate) * 1000UL + ((rest * 1000UL) % rate) * 1000UL / rate;
}

u32 set_attrs(env_t *env, void *obj, snd_inst *p, odt_TagItem *tags)
{
    u32 n = 0;
    odt_TagItem *t;
    (void)obj;
    while ((t = dtc_next_tag(&tags)) != nullptr) {
        u32 d = t->ti_Data;
        switch (t->ti_Tag) {
        case SND_SDTA_Sample: {
            s8 *s = reinterpret_cast<s8 *>(d);
            s8 *old = p->left;
            p->left = s;
            if (old && old != s && (p->flags & SND_F_FREE) && old != p->right)
                env->free(env, old);
            break;
        }
        case SND_SDTA_LeftSample: {
            s8 *old = p->left;
            p->left = reinterpret_cast<s8 *>(d);
            if (old && old != p->left && (p->flags & SND_F_FREE) && old != p->right)
                env->free(env, old);
            break;
        }
        case SND_SDTA_RightSample: set_free_sample(env, p, &p->right, reinterpret_cast<s8 *>(d)); break;
        case SND_SDTA_SampleLength:
            /* 0 stops a sound that plays (the attribute's contract) */
            if (!d)
                stop(env, p);
            p->length = d;
            update_replay(env, p);
            break;
        case SND_SDTA_VoiceHeader:
            /* the header copied; its rate and volume taken (tags after it win) */
            if (d) {
                const snd_VoiceHeader *v = reinterpret_cast<const snd_VoiceHeader *>(d);
                p->vh = *v;
                if (v->vh_SamplesPerSec) {
                    p->samples_per_sec = v->vh_SamplesPerSec;
                    p->period = period_of(env, v->vh_SamplesPerSec);
                    update_replay(env, p);
                }
                p->volume = v->vh_Volume >= SND_UNITY ? 64 : v->vh_Volume * 64 / SND_UNITY;
            }
            break;
        case SND_SDTA_Period:
            p->period = d;
            p->samples_per_sec = d ? (clock_of(env) + d / 2) / d : 0;
            update_replay(env, p);
            break;
        case SND_SDTA_SamplesPerSec:
            p->samples_per_sec = d;
            p->period = period_of(env, d);
            update_replay(env, p);
            break;
        case SND_SDTA_Volume: p->volume = min<u32>(d, 64); break;
        case SND_SDTA_Cycles: p->cycles = d; break;
        case SND_SDTA_SignalTask: p->signal_task = reinterpret_cast<void *>(d); break;
        case SND_SDTA_SignalBit: p->signal_mask = d; break;
        case SND_SDTA_SignalBitNumber:
            p->signal_mask = static_cast<s32>(d) >= 0 && d < 32 ? 1UL << d : 0;
            break;
        case SND_SDTA_Continuous:
            p->flags = static_cast<u16>(d ? p->flags | SND_F_CONTINUOUS : p->flags & ~SND_F_CONTINUOUS);
            break;
        case SND_SDTA_SyncSampleChange:
            p->flags = static_cast<u16>(d ? p->flags | SND_F_SYNC : p->flags & ~SND_F_SYNC);
            break;
        case SND_SDTA_FreeSampleData:
            p->flags = static_cast<u16>(d ? p->flags | SND_F_FREE : p->flags & ~SND_F_FREE);
            break;
        case SND_SDTA_Pan:
            p->pan = min<u32>(d, SND_UNITY);
            p->flags |= SND_F_PANSET;
            break;
        case SND_SDTA_SampleType: p->sample_type = d <= SND_SDTST_S16S ? d : SND_SDTST_M8S; break;
        default: continue;
        }
        n++;
    }
    return n;
}

bool get_attr(snd_inst *p, u32 id, u32 *st)
{
    switch (id) {
    case SND_SDTA_VoiceHeader: *st = reinterpret_cast<u32>(&p->vh); return true;
    case SND_SDTA_Sample:
    case SND_SDTA_LeftSample: *st = reinterpret_cast<u32>(p->left); return true;
    case SND_SDTA_RightSample: *st = reinterpret_cast<u32>(p->right); return true;
    case SND_SDTA_SampleLength: *st = p->length; return true;
    case SND_SDTA_Period: *st = p->period; return true;
    case SND_SDTA_SamplesPerSec: *st = p->samples_per_sec; return true;
    case SND_SDTA_Volume: *st = p->volume; return true;
    case SND_SDTA_Cycles: *st = p->cycles; return true;
    case SND_SDTA_SignalTask: *st = reinterpret_cast<u32>(p->signal_task); return true;
    case SND_SDTA_SignalBit: *st = p->signal_mask; return true;
    case SND_SDTA_Continuous: *st = (p->flags & SND_F_CONTINUOUS) ? 1 : 0; return true;
    case SND_SDTA_SyncSampleChange: *st = (p->flags & SND_F_SYNC) ? 1 : 0; return true;
    case SND_SDTA_FreeSampleData: *st = (p->flags & SND_F_FREE) ? 1 : 0; return true;
    case SND_SDTA_Pan: *st = p->pan; return true;
    case SND_SDTA_SampleType: *st = p->sample_type; return true;
    case SND_SDTA_ReplayPeriod: *st = reinterpret_cast<u32>(&p->replay); return true;
    case ODT_DTA_TriggerMethods: *st = reinterpret_cast<u32>(triggers); return true;
    }
    return false;
}

/* ---- the gadget ------------------------------------------------------------ */

void gadget_box(void *obj, odt_GadgetInfo *gi, odt_IBox *b)
{
    const odt_Gadget *g = static_cast<const odt_Gadget *>(obj);
    s32 dl = gi ? gi->gi_Domain.Left : 0, dt = gi ? gi->gi_Domain.Top : 0;
    s32 dw = gi ? gi->gi_Domain.Width : 0, dh = gi ? gi->gi_Domain.Height : 0;
    s32 l = g->LeftEdge + dl, t = g->TopEdge + dt, w = g->Width, h = g->Height;
    if (g->Flags & ODT_GFLG_RELRIGHT) l += dw - 1;
    if (g->Flags & ODT_GFLG_RELBOTTOM) t += dh - 1;
    if (g->Flags & ODT_GFLG_RELWIDTH) w += dw;
    if (g->Flags & ODT_GFLG_RELHEIGHT) h += dh;
    b->Left = static_cast<odt_s16>(l);
    b->Top = static_cast<odt_s16>(t);
    b->Width = static_cast<odt_s16>(max<s32>(w, 0));
    b->Height = static_cast<odt_s16>(max<s32>(h, 0));
}

u32 pen(odt_GadgetInfo *gi, u32 which)
{
    const odt_DrawInfo *dri = gi ? static_cast<const odt_DrawInfo *>(gi->gi_DrInfo) : nullptr;
    if (dri && dri->dri_Pens && which < dri->dri_NumPens)
        return dri->dri_Pens[which];
    static constexpr u8 fallback[8] = { 0, 1, 1, 2, 1, 3, 1, 0 };
    return which < 8 ? fallback[which] : 1;
}

bool has_panel(env_t *env, odt_IClass *cl, void *obj)
{
    u32 v = 0;
    return dtc_get_super(env, cl, obj, ODT_DTA_ControlPanel, &v) && v;
}

void fill(env_t *env, void *rp, u32 c, s32 x0, s32 y0, s32 x1, s32 y1)
{
    if (X(env)->fill && x0 <= x1 && y0 <= y1)
        X(env)->fill(env, rp, c, x0, y0, x1, y1);
}
void line(env_t *env, void *rp, u32 c, s32 x0, s32 y0, s32 x1, s32 y1)
{
    if (X(env)->line)
        X(env)->line(env, rp, c, x0, y0, x1, y1);
}

void button(env_t *env, void *rp, odt_GadgetInfo *gi, s32 x0, s32 y0, s32 x1, s32 y1, bool down, bool is_play)
{
    u32 hi = pen(gi, down ? DTC_SHADOWPEN : DTC_SHINEPEN), lo = pen(gi, down ? DTC_SHINEPEN : DTC_SHADOWPEN);
    fill(env, rp, pen(gi, down ? DTC_FILLPEN : DTC_BACKGROUNDPEN), x0, y0, x1, y1);
    line(env, rp, hi, x0, y0, x1, y0);
    line(env, rp, hi, x0, y0, x0, y1);
    line(env, rp, lo, x1, y0, x1, y1);
    line(env, rp, lo, x0, y1, x1, y1);
    s32 cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    s32 s = max<s32>(min<s32>(x1 - x0, y1 - y0) / 4, 1);
    u32 tp = pen(gi, DTC_TEXTPEN);
    if (is_play) {
        for (s32 k = 0; k <= s; k++)    /* a triangle pointing right */
            line(env, rp, tp, cx - s / 2 + k, cy - (s - k), cx - s / 2 + k, cy + (s - k));
    } else {
        fill(env, rp, tp, cx - s, cy - s, cx + s, cy + s);
    }
}

void render(env_t *env, odt_IClass *cl, void *obj, snd_inst *p, odt_GadgetInfo *gi, void *rp)
{
    if (!rp)
        return;
    odt_IBox b;
    gadget_box(obj, gi, &b);
    if (b.Width <= 0 || b.Height <= 0)
        return;
    bool panel = has_panel(env, cl, obj) && b.Height > SND_PANEL_HEIGHT;
    s32 wh = b.Height - (panel ? SND_PANEL_HEIGHT : 0);
    s32 x0 = b.Left, y0 = b.Top, W = b.Width;
    fill(env, rp, pen(gi, DTC_BACKGROUNDPEN), x0, y0, x0 + W - 1, y0 + wh - 1);
    if (p->left && p->length) {
        u32 tp = pen(gi, DTC_TEXTPEN);
        for (s32 x = 0; x < W; x++) {
            /* x * length / W without the product: length = q W + r */
            u32 q = p->length / static_cast<u32>(W), r = p->length % static_cast<u32>(W);
            u32 s0 = static_cast<u32>(x) * q + static_cast<u32>(x) * r / static_cast<u32>(W);
            u32 s1 = max<u32>(static_cast<u32>(x + 1) * q + static_cast<u32>(x + 1) * r / static_cast<u32>(W), s0 + 1);
            s32 lo = 127, hi = -128;
            for (u32 i = s0; i < s1 && i < p->length; i++) {
                s32 v = snd_sample8(p, 0, i);
                lo = min(lo, v);
                hi = max(hi, v);
            }
            if (hi < lo)
                continue;
            line(env, rp, tp, x0 + x, y0 + (127 - hi) * (wh - 1) / 255, x0 + x, y0 + (127 - lo) * (wh - 1) / 255);
        }
    }
    if (panel) {
        s32 py = y0 + wh, mid = x0 + W / 2;
        button(env, rp, gi, x0, py, mid - 1, py + SND_PANEL_HEIGHT - 1, p->active == 1, true);
        button(env, rp, gi, mid, py, x0 + W - 1, py + SND_PANEL_HEIGHT - 1, p->active == 2, false);
    }
}

u32 go_active(env_t *env, odt_IClass *cl, void *obj, snd_inst *p, odt_gpInput *m)
{
    odt_IBox b;
    gadget_box(obj, m->gpi_GInfo, &b);
    bool panel = has_panel(env, cl, obj) && b.Height > SND_PANEL_HEIGHT;
    s32 wh = b.Height - (panel ? SND_PANEL_HEIGHT : 0);
    if (panel && m->gpi_IEvent && m->gpi_Mouse.y >= wh) {
        bool is_play = m->gpi_Mouse.x < b.Width / 2;
        p->active = is_play ? 1 : 2;
        if (is_play)
            play(env, cl, obj, p);
        else
            stop(env, p);
    } else {
        play(env, cl, obj, p);          /* a click on the waveform plays it */
    }
    if (m->gpi_Termination)
        *m->gpi_Termination = 0;
    return ODT_GMR_NOREUSE;
}

} // namespace

/* ---- sample access, the plan, 8SVX ---------------------------------------------- */

extern "C" odt_s8 snd_sample8(const snd_inst *p, odt_u32 ch, odt_u32 i)
{
    bool wide = p->sample_type == SND_SDTST_M16S || p->sample_type == SND_SDTST_S16S;
    const s8 *s = ch && p->right ? p->right : p->left;
    if (!s)
        return 0;
    u32 k = interleaved(p) ? i * 2 + (ch ? 1 : 0) : i;
    return wide ? s[k * 2] : s[k];      /* 16 bits: the high byte (ABI test: OS 3.2 may round) */
}

extern "C" int snd_make_plan(const odt_cenv *env, snd_inst *p, odt_u32 cycles)
{
    dtc_audio_plan *pl = &p->plan;
    for (u32 i = 0; i < sizeof *pl; i++)
        reinterpret_cast<u8 *>(pl)[i] = 0;
    free_chip(env, p);
    if (!p->left || !p->length || !X(env)->alloc_chip)
        return 0;
    bool st = stereo(p);
    u32 bytes = (p->length + 1) & ~1UL;
    u32 maxbytes = DTC_AUDIO_MAXLEN * (DTC_AUDIO_MAXSEGS / (st ? 2 : 1));
    if (bytes > maxbytes)
        bytes = maxbytes;               /* ABI test: a sample longer than the plan holds */
    for (u32 ch = 0; ch < (st ? 2U : 1U); ch++) {
        p->chip[ch] = static_cast<s8 *>(X(env)->alloc_chip(env, bytes));
        if (!p->chip[ch]) {
            free_chip(env, p);
            set_err(env, DTC_ERROR_NO_FREE_STORE);
            return 0;
        }
        for (u32 i = 0; i < bytes && i < p->length; i++)
            p->chip[ch][i] = snd_sample8(p, ch, i);
    }
    u8 role = 1;
    if (st) {
        pl->alloc[0] = 3; pl->alloc[1] = 5; pl->alloc[2] = 10; pl->alloc[3] = 12;
        pl->nalloc = 4;
    } else if (!(p->flags & SND_F_PANSET)) {
        pl->alloc[0] = 1; pl->alloc[1] = 2; pl->alloc[2] = 4; pl->alloc[3] = 8;
        pl->nalloc = 4;
    } else if (p->pan < 0x8000) {       /* ABI test: SDTA_Pan on a mono sample */
        pl->alloc[0] = 1; pl->alloc[1] = 8;
        pl->nalloc = 2;
    } else if (p->pan > 0x8000) {
        pl->alloc[0] = 2; pl->alloc[1] = 4;
        pl->nalloc = 2;
    } else {
        pl->alloc[0] = 3; pl->alloc[1] = 5; pl->alloc[2] = 10; pl->alloc[3] = 12;
        pl->nalloc = 4;
        role = 3;
    }
    u32 period = max<u32>(p->period ? p->period : period_of(env, 8000), SND_PERIOD_MIN); /* ABI test */
    u32 chunks = (bytes + DTC_AUDIO_MAXLEN - 1) / DTC_AUDIO_MAXLEN;
    for (u32 c = 0; c < chunks; c++) {
        u32 off = c * DTC_AUDIO_MAXLEN, len = min<u32>(DTC_AUDIO_MAXLEN, bytes - off);
        for (u32 ch = 0; ch < (st ? 2U : 1U); ch++) {
            dtc_audio_seg *s = &pl->seg[pl->nseg++];
            s->roles = st ? static_cast<u8>(1 << ch) : role;
            s->volume = static_cast<u16>(min<u32>(p->volume, 64));
            s->period = static_cast<u16>(min<u32>(period, 65535));
            s->cycles = static_cast<u16>(chunks == 1 ? cycles : 1);
            s->data = p->chip[ch] + off;
            s->length = len;
        }
    }
    pl->repeat = static_cast<u16>(chunks == 1 ? 1 : cycles);
    pl->signal_task = p->signal_task;
    pl->signal_mask = p->signal_mask;
    return 1;
}

extern "C" int snd_write_8svx(const odt_cenv *env, snd_inst *p, odt_u32 fh)
{
    if (!p->left || !env->write) {
        set_err(env, DTC_ERROR_REQUIRED_ARG_MISSING);
        return 0;
    }
    bool st = stereo(p);
    u32 nch = st ? 2 : 1, body = p->length * nch;
    u32 total = 12 + 28 + (st ? 12 : 0) + 8 + body + 1;
    u8 *buf = static_cast<u8 *>(env->alloc(env, total));
    if (!buf) {
        set_err(env, DTC_ERROR_NO_FREE_STORE);
        return 0;
    }
    u32 o = 12;
    dtc_put32(buf, ODT_ID('F', 'O', 'R', 'M'));
    dtc_put32(buf + 8, ODT_ID('8', 'S', 'V', 'X'));
    dtc_put32(buf + o, ODT_ID('V', 'H', 'D', 'R'));
    dtc_put32(buf + o + 4, 20);
    dtc_put32(buf + o + 8, p->length);
    dtc_put32(buf + o + 12, 0);
    dtc_put32(buf + o + 16, 0);
    dtc_put16(buf + o + 20, min<u32>(p->samples_per_sec, 65535));
    buf[o + 22] = 1;
    buf[o + 23] = SND_CMP_NONE;
    dtc_put32(buf + o + 24, p->volume >= 64 ? SND_UNITY : p->volume * SND_UNITY / 64);
    o += 28;
    if (st) {
        dtc_put32(buf + o, ODT_ID('C', 'H', 'A', 'N'));
        dtc_put32(buf + o + 4, 4);
        dtc_put32(buf + o + 8, 6);
        o += 12;
    }
    dtc_put32(buf + o, ODT_ID('B', 'O', 'D', 'Y'));
    dtc_put32(buf + o + 4, body);
    o += 8;
    for (u32 ch = 0; ch < nch; ch++)
        for (u32 i = 0; i < p->length; i++)
            buf[o++] = static_cast<u8>(snd_sample8(p, ch, i));
    if (o & 1)
        buf[o++] = 0;
    dtc_put32(buf + 4, o - 8);
    s32 n = env->write(env, fh, buf, static_cast<s32>(o));
    env->free(env, buf);
    if (n != static_cast<s32>(o)) {
        set_err(env, ODT_DTERROR_COULDNT_SAVE);
        return 0;
    }
    return 1;
}

extern "C" odt_u32 snd_inst_size(void) { return sizeof(snd_inst); }

extern "C" odt_u32 snd_dispatch(const odt_cenv *env, odt_IClass *cl, void *obj, void *msg)
{
    switch (method(msg)) {
    case ODT_OM_NEW: {
        void *o = reinterpret_cast<void *>(env->do_super(env, cl, obj, msg));
        if (!o)
            return 0;
        snd_inst *p = inst(cl, o);
        p->volume = 64;
        p->cycles = 1;
        p->pan = 0x8000;
        p->flags = SND_F_FREE;          /* ABI test: OS 3.2's SDTA_FreeSampleData default */
        set_attrs(env, o, p, static_cast<odt_opSet *>(msg)->ops_AttrList);
        return reinterpret_cast<u32>(o);
    }
    case ODT_OM_DISPOSE: {
        snd_inst *p = inst(cl, obj);
        stop(env, p);
        if (p->flags & SND_F_FREE) {
            if (p->right && p->right != p->left)
                env->free(env, p->right);
            env->free(env, p->left);
        }
        p->left = p->right = nullptr;
        return env->do_super(env, cl, obj, msg);
    }
    case ODT_OM_SET:
    case ODT_OM_UPDATE: {
        u32 r = env->do_super(env, cl, obj, msg);
        return r + set_attrs(env, obj, inst(cl, obj), static_cast<odt_opSet *>(msg)->ops_AttrList);
    }
    case ODT_OM_GET: {
        odt_opGet *g = static_cast<odt_opGet *>(msg);
        if (get_attr(inst(cl, obj), g->opg_AttrID, g->opg_Storage))
            return 1;
        return env->do_super(env, cl, obj, msg);
    }
    case ODT_GM_LAYOUT: {
        u32 r = env->do_super(env, cl, obj, msg);
        snd_inst *p = inst(cl, obj);
        u32 imm = 0;
        if (static_cast<odt_gpLayout *>(msg)->gpl_Initial && !(p->flags & SND_F_PLAYED) &&
            dtc_get_super(env, cl, obj, ODT_DTA_Immediate, &imm) && imm) {
            p->flags |= SND_F_PLAYED;
            play(env, cl, obj, p);
        }
        return r;
    }
    case ODT_GM_RENDER: {
        odt_gpRender *m = static_cast<odt_gpRender *>(msg);
        render(env, cl, obj, inst(cl, obj), m->gpr_GInfo, m->gpr_RPort);
        return 1;
    }
    case ODT_GM_HITTEST:
        return ODT_GMR_GADGETHIT;
    case ODT_GM_GOACTIVE: {
        odt_gpInput *m = static_cast<odt_gpInput *>(msg);
        snd_inst *p = inst(cl, obj);
        u32 r = go_active(env, cl, obj, p, m);
        if (p->active && env->obtain_rp) {
            void *rp = env->obtain_rp(env, m->gpi_GInfo);
            render(env, cl, obj, p, m->gpi_GInfo, rp);
            p->active = 0;
            render(env, cl, obj, p, m->gpi_GInfo, rp);
            if (env->release_rp)
                env->release_rp(env, rp);
        }
        p->active = 0;
        return r;
    }
    case ODT_GM_HANDLEINPUT:
        return ODT_GMR_NOREUSE;
    case ODT_DTM_TRIGGER: {
        odt_dtTrigger *t = static_cast<odt_dtTrigger *>(msg);
        snd_inst *p = inst(cl, obj);
        switch (t->dtt_Function & ODT_STMF_METHOD_MASK) {
        case ODT_STM_PLAY:
        case ODT_STM_RESUME:            /* ABI test: OS 3.2 resumes from the start */
            return play(env, cl, obj, p) ? 1 : 0;
        case ODT_STM_STOP:
        case ODT_STM_PAUSE:             /* ABI test: a pause as a stop */
            stop(env, p);
            return 1;
        }
        return 0;
    }
    case ODT_DTM_WRITE: {
        odt_dtWrite *w = static_cast<odt_dtWrite *>(msg);
        if (w->dtw_Mode != ODT_DTWM_IFF) {
            set_err(env, DTC_ERROR_NOT_IMPLEMENTED); /* ABI test: sound.datatype's own DTWM_RAW */
            return 0;
        }
        return snd_write_8svx(env, inst(cl, obj), w->dtw_FileHandle) ? 1 : 0;
    }
    }
    return env->do_super(env, cl, obj, msg);
}
