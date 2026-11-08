/* GFOpenDatatypes text.datatype core.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 * Clean-room; see text_core.h for what this is written from.
 */
#include "text_core.h"

/* TDTA_WordDelim's default (ABI test: OS 3.2's own set) */
const char tdt_default_delims[] = "\t *-,<>()[];\"";

const odt_u32 tdt_methods[] = {
    ODT_OM_NEW, ODT_OM_DISPOSE, ODT_OM_SET, ODT_OM_GET, ODT_OM_UPDATE,
    ODT_GM_LAYOUT, ODT_GM_RENDER, ODT_GM_GOACTIVE, ODT_GM_HANDLEINPUT, ODT_GM_GOINACTIVE, ODT_GM_HITTEST,
    ODT_DTM_PROCLAYOUT, ODT_DTM_ASYNCLAYOUT, ODT_DTM_FRAMEBOX, ODT_DTM_SELECT, ODT_DTM_CLEARSELECTED,
    ODT_DTM_COPY, ODT_DTM_WRITE, ODT_DTM_REMOVEDTOBJECT, ODT_DTM_OBTAINDRAWINFO, ODT_DTM_DRAW,
    ODT_DTM_RELEASEDRAWINFO, ~0UL
};

#define DEF_FG 1                        /* a segment's pens with no escape: the console's (ABI test) */
#define DEF_BG 0

/* ---- layout ------------------------------------------------------------------ */

/* Segments come from blocks of TDT_POOL, freed together */
struct pool_block {
    struct pool_block *next;
    odt_u32 used, pad;
    struct tdt_seg seg[TDT_POOL];
};

static struct tdt_seg *seg_alloc(const struct odt_cenv *env, struct tdt_layout *lay)
{
    struct pool_block *b = lay->pool;
    if (!b || b->used == TDT_POOL) {
        if (!(b = env->alloc(env, sizeof *b))) return 0;
        b->next = lay->pool;
        lay->pool = b;
    }
    return &b->seg[b->used++];
}

/* A character's width in one style, measured once a layout (TextLength
 * adds its characters' widths, so a row's is the sum of them) */
struct cw_table {
    odt_s16 w[256];
    odt_u8 known[32];
};

#define SUM_BAD (-1)

struct lay {
    const struct odt_cenv *env;
    const struct tdt_measure *m;
    struct tdt_layout *out;
    const char *buf;
    odt_u32 row;
    odt_s32 x;                          /* where the pending segment starts */
    odt_u32 s, n;                       /* the pending segment: buf[s], n bytes */
    odt_u8 tab;                         /* a tab before the pending segment */
    odt_u8 row_has;                     /* a segment is on this row */
    odt_u8 fg, bg;
    odt_u32 style;
    struct tdt_seg *last;
    odt_s32 err;
    /* measuring: exact (the whole pending text each time, as it always
     * was) or by the characters' widths; the pending text's width so far
     * (pw, of pn bytes from ps in pstyle); a segment whose width isn't the
     * sum of its characters' sets err to SUM_BAD and the layout is made
     * again, measuring exactly */
    odt_u8 exact;
    odt_u32 ps, pn, pstyle;
    odt_s32 pw;
    struct cw_table *cw[8];
};

/* A character's width in the current style, or -1 when there is no memory
 * for the table (the layout then measures exactly) */
static odt_s32 char_w(struct lay *L, odt_u8 c)
{
    struct cw_table *t = L->cw[L->style & 7];
    char ch = (char)c;
    if (!t) {
        if (!(t = L->env->alloc(L->env, sizeof *t))) return -1;
        L->cw[L->style & 7] = t;
    }
    if (!(t->known[c >> 3] & (1 << (c & 7)))) {
        t->w[c] = (odt_s16)L->m->len(L->m, L->style, &ch, 1);
        t->known[c >> 3] |= (odt_u8)(1 << (c & 7));
    }
    return t->w[c];
}

/* The width of the first upto bytes of the pending text */
static odt_s32 pending_width(struct lay *L, odt_u32 upto)
{
    if (!L->exact) {
        if (L->pn > upto && L->ps == L->s && L->pstyle == L->style) {
            /* fewer than were added up (a row's text checked with the
             * character after it, or cut at a space): those taken off */
            const struct cw_table *t = L->cw[L->style & 7];
            while (L->pn > upto) L->pw -= t->w[(odt_u8)L->buf[L->s + --L->pn]];
            return L->pw;
        }
        if (L->ps != L->s || L->pstyle != L->style) {
            L->ps = L->s;
            L->pstyle = L->style;
            L->pn = 0;
            L->pw = 0;
        }
        while (L->pn < upto) {
            odt_s32 w = char_w(L, (odt_u8)L->buf[L->s + L->pn]);
            if (w < 0) { L->exact = 1; break; }
            L->pw += w;
            L->pn++;
        }
        if (!L->exact) return L->pw;
    }
    return L->m->len(L->m, L->style, L->buf + L->s, upto);
}

static struct tdt_seg *emit(struct lay *L, odt_u32 brk)
{
    struct tdt_seg *g = seg_alloc(L->env, L->out);
    odt_s32 w;
    if (!g) { L->err = DTC_ERROR_NO_FREE_STORE; return 0; }
    w = L->n ? L->m->len(L->m, L->style, L->buf + L->s, L->n) : 0;
    if (!L->exact && L->n && w != pending_width(L, L->n)) L->err = SUM_BAD;
    g->l.ln_Text = (char *)L->buf + L->s;
    g->l.ln_TextLen = L->n;
    g->l.ln_XOffset = (odt_u16)L->x;
    g->l.ln_YOffset = (odt_u16)(L->row * L->m->ysize);
    g->l.ln_Width = (odt_u16)w;
    g->l.ln_Height = L->m->ysize;
    g->l.ln_Flags = brk ? TDT_LNF_LF : 0;
    g->l.ln_FgPen = (odt_s8)L->fg;
    g->l.ln_BgPen = (odt_s8)L->bg;
    g->l.ln_Style = L->style;
    g->l.ln_Data = 0;
    g->row = L->row;
    g->tab = L->tab;
    g->brk = (odt_u8)brk;
    dtc_add_tail(&L->out->lines, &g->l.ln_Link);
    L->out->segs++;
    if ((odt_u32)(L->x + w) > L->out->width) L->out->width = (odt_u32)(L->x + w);
    L->x += w;
    L->s += L->n;
    L->n = 0;
    L->tab = 0;
    L->row_has = 1;
    L->last = g;
    return g;
}

/* Ends the row with brk: the pending segment carries it, else the last one
 * on the row, else an empty segment does. use_pending 0 leaves the
 * pending text for the next row. */
static void end_row(struct lay *L, odt_u32 brk, int use_pending)
{
    if (use_pending && (L->n || L->tab)) emit(L, brk);
    else if (L->row_has) {
        L->last->brk = (odt_u8)brk;
        L->last->l.ln_Flags |= TDT_LNF_LF;
    } else {
        /* nothing on the row yet: an empty segment ends it, carrying the
         * tab that put the pending text where it was */
        odt_u32 n = L->n, s = L->s;
        if (!use_pending) L->n = 0;
        emit(L, brk);
        if (!use_pending) { L->s = s; L->n = n; }
    }
    L->row++;
    L->x = 0;
    L->row_has = 0;
}

static void sgr(struct lay *L, odt_u32 p)
{
    if (p == 0) { L->style = 0; L->fg = DEF_FG; L->bg = DEF_BG; }
    else if (p == 1) L->style |= TDT_FSF_BOLD;
    else if (p == 3) L->style |= TDT_FSF_ITALIC;
    else if (p == 4) L->style |= TDT_FSF_UNDERLINED;
    else if (p == 22) L->style &= ~(odt_u32)TDT_FSF_BOLD;
    else if (p == 23) L->style &= ~(odt_u32)TDT_FSF_ITALIC;
    else if (p == 24) L->style &= ~(odt_u32)TDT_FSF_UNDERLINED;
    else if (p >= 30 && p <= 37) L->fg = (odt_u8)(p - 30);
    else if (p == 39) L->fg = DEF_FG;
    else if (p >= 40 && p <= 47) L->bg = (odt_u8)(p - 40);
    else if (p == 49) L->bg = DEF_BG;
}

/* An escape at buf[i]: returns the index after it */
static odt_u32 escape(struct lay *L, odt_u32 i, odt_u32 len)
{
    const odt_u8 *b = (const odt_u8 *)L->buf;
    odt_u32 j = i + 1, p = 0, params[16], np = 0, k;
    int have = 0;
    if (b[i] == 0x1b) {
        if (j >= len || b[j] != '[') return j;  /* ESC alone, or ESC and not '[': only the ESC goes (ABI test) */
        j++;
    }
    for (; j < len; j++) {
        odt_u8 c = b[j];
        if (c >= '0' && c <= '9') { if (p < 10000) p = p * 10 + (c - '0'); have = 1; }
        else if (c == ';') { if (np < 16) params[np++] = p; p = 0; have = 0; }
        else if (c >= 0x40 && c <= 0x7e) {
            if (c == 'm') {
                if (np < 16 && (have || np == 0)) params[np++] = p;
                else if (np < 16) params[np++] = 0;
                if (L->n) emit(L, TDT_BRK_NONE);
                for (k = 0; k < np; k++) sgr(L, params[k]);
            }
            return j + 1;
        } else if (c < 0x20 || c > 0x3f) return j;  /* not an escape's byte: it ends (ABI test) */
    }
    return len;                         /* cut short at the end: dropped */
}

/* Each tab, character and row end is one place in reading order */
static odt_u32 places(const struct tdt_seg *g) { return g->tab + g->l.ln_TextLen + (g->brk ? 1 : 0); }

/* Each row's first segment and the places before it (none when there is
 * no memory: the walks then start at the first segment) */
static void make_index(const struct odt_cenv *env, struct tdt_layout *out)
{
    const struct odt_MinNode *n;
    odt_u32 r = 0, at = 0;
    out->row_seg = env->alloc(env, (out->rows + 1) * sizeof *out->row_seg);
    out->row_at = env->alloc(env, (out->rows + 1) * sizeof *out->row_at);
    if (!out->row_seg || !out->row_at) {
        env->free(env, out->row_seg);
        env->free(env, out->row_at);
        out->row_seg = 0;
        out->row_at = 0;
        return;
    }
    for (n = out->lines.mlh_Head; n->mln_Succ; n = n->mln_Succ) {
        struct tdt_seg *g = (struct tdt_seg *)n;
        while (r <= g->row && r < out->rows) {
            out->row_seg[r] = g;
            out->row_at[r++] = at;
        }
        at += places(g);
    }
    for (; r <= out->rows; r++) {
        out->row_seg[r] = 0;
        out->row_at[r] = at;
    }
}

static odt_s32 lay_out(struct lay *L, const struct odt_cenv *env, const struct tdt_measure *m, const char *buf,
                       odt_u32 len, odt_u32 wrap, struct tdt_layout *out, int exact)
{
    odt_u32 i = 0;
    odt_s32 tabw;
    dtc_list_init(&out->lines);
    out->rows = out->width = out->segs = 0;
    out->pool = 0;
    out->row_seg = 0;
    out->row_at = 0;
    L->env = env;
    L->m = m;
    L->out = out;
    L->buf = buf;
    L->row = 0;
    L->x = 0;
    L->s = L->n = 0;
    L->tab = L->row_has = 0;
    L->fg = DEF_FG;
    L->bg = DEF_BG;
    L->style = 0;
    L->last = 0;
    L->err = 0;
    L->exact = (odt_u8)(exact || !wrap);  /* without wrapping nothing is decided by the sums */
    L->ps = L->pn = L->pstyle = 0;
    L->pw = 0;
    tabw = TDT_TABCHARS * m->len(m, 0, " ", 1);
    if (tabw <= 0) tabw = TDT_TABCHARS * 8;
    while (i < len && !L->err) {
        odt_u8 c = (odt_u8)buf[i];
        if (c == '\n' || c == '\r') {
            end_row(L, TDT_BRK_HARD, 1);
            i += (c == '\r' && i + 1 < len && buf[i + 1] == '\n') ? 2 : 1;
            L->s = i;
            continue;
        }
        if (c == '\t') {
            if (L->n || L->tab) emit(L, TDT_BRK_NONE);
            L->x = (L->x / tabw + 1) * tabw;
            L->tab = 1;
            L->s = ++i;
            continue;
        }
        if (c == 0x1b || c == 0x9b) {
            i = escape(L, i, len);
            if (!L->n) L->s = i;
            else {
                /* an escape that isn't SGR inside text: the text either
                 * side is one segment no more (ABI test) */
                emit(L, TDT_BRK_NONE);
                L->s = i;
            }
            continue;
        }
        if (wrap) {
            /* the pending text and this character: one more width added
             * (the common case, inline), else pending_width */
            const struct cw_table *t = L->cw[L->style & 7];
            odt_s32 w;
            if (!L->exact && L->pn == L->n && L->ps == L->s && L->pstyle == L->style && t &&
                (t->known[c >> 3] & (1 << (c & 7)))) {
                w = L->pw += t->w[c];
                L->pn++;
            } else
                w = pending_width(L, L->n + 1);
            if (L->x + w > (odt_s32)wrap && (L->n || L->row_has || L->x > 0)) {
                odt_u32 k = L->s + L->n + 1, sp = ~0UL;
                while (k-- > L->s)
                    if (buf[k] == ' ') { sp = k; break; }
                if (sp != ~0UL) {
                    /* at the last space: it goes */
                    L->n = sp - L->s;
                    end_row(L, TDT_BRK_SPACE, 1);
                    L->s = sp + 1;
                    if (sp == i) { L->n = 0; i++; L->s = i; }
                    else L->n = i - L->s;
                } else if (L->row_has || L->x > 0) {
                    /* before the word, which goes to the next row whole */
                    end_row(L, TDT_BRK_SOFT, 0);
                } else {
                    /* a word wider than the row: broken where it fills it */
                    end_row(L, TDT_BRK_SOFT, 1);
                    L->s = i;
                }
                continue;
            }
        }
        L->n++;
        i++;
    }
    if (!L->err && (L->n || L->tab)) emit(L, TDT_BRK_NONE);
    out->rows = L->row + (L->row_has ? 1 : 0);
    return L->err;
}

odt_s32 tdt_layout(const struct odt_cenv *env, const struct tdt_measure *m, const char *buf, odt_u32 len, odt_u32 wrap,
                   struct tdt_layout *out)
{
    struct lay L;
    odt_s32 r;
    int k;
    for (k = 0; k < 8; k++) L.cw[k] = 0;
    r = lay_out(&L, env, m, buf, len, wrap, out, 0);
    if (r == SUM_BAD) {
        /* a width that isn't its characters' sum: measured as before */
        tdt_free_layout(env, out);
        r = lay_out(&L, env, m, buf, len, wrap, out, 1);
    }
    for (k = 0; k < 8; k++) env->free(env, L.cw[k]);
    make_index(env, out);
    return r;
}

void tdt_free_layout(const struct odt_cenv *env, struct tdt_layout *lay)
{
    struct pool_block *b, *next;
    if (!lay->lines.mlh_Head) return;
    for (b = lay->pool; b; b = next) {
        next = b->next;
        env->free(env, b);
    }
    lay->pool = 0;
    env->free(env, lay->row_seg);
    env->free(env, lay->row_at);
    lay->row_seg = 0;
    lay->row_at = 0;
    dtc_list_init(&lay->lines);
    lay->rows = lay->width = lay->segs = 0;
}

/* ---- selection --------------------------------------------------------------- */

#define SEG(n) ((struct tdt_seg *)(n))
#define FIRST(lay) SEG((lay)->lines.mlh_Head)
#define NEXT(g) SEG((g)->l.ln_Link.mln_Succ)
#define END(g) (!(g)->l.ln_Link.mln_Succ)

/* The place at content point (x, y) */
/* Where a walk for row starts: its first segment and the places before
 * it, from the index (or the first segment, when there is none) */
static const struct tdt_seg *row_start(const struct tdt_layout *lay, odt_u32 row, odt_u32 *at)
{
    if (lay->row_seg) {
        if (row >= lay->rows) {         /* nothing there: the list's end */
            *at = lay->row_at[lay->rows];
            return (const struct tdt_seg *)&lay->lines.mlh_Tail;
        }
        *at = lay->row_at[row];
        return lay->row_seg[row];
    }
    *at = 0;
    return FIRST(lay);
}

static odt_u32 place_at(const struct tdt_measure *m, const struct tdt_layout *lay, odt_s32 x, odt_s32 y)
{
    const struct tdt_seg *g;
    odt_u32 at = 0, row = y < 0 ? 0 : (odt_u32)y / m->ysize, k;
    if (row >= lay->rows) {
        if (lay->row_at) return lay->row_at[lay->rows];
        for (g = FIRST(lay); !END(g); g = NEXT(g)) at += places(g);
        return at;                      /* past the end */
    }
    for (g = row_start(lay, row, &at); !END(g); g = NEXT(g)) {
        if (g->row < row) { at += places(g); continue; }
        if (g->row > row) return at;    /* the row had nothing there: the next row's start */
        if (x < g->l.ln_XOffset) return at;
        if (x < g->l.ln_XOffset + g->l.ln_Width) {
            odt_s32 dx = x - g->l.ln_XOffset;
            for (k = 0; k < g->l.ln_TextLen; k++)
                if (m->len(m, g->l.ln_Style, g->l.ln_Text, k + 1) > dx) break;
            if (k >= g->l.ln_TextLen) k = g->l.ln_TextLen ? g->l.ln_TextLen - 1 : 0;
            return at + g->tab + k;
        }
        at += places(g);
        if (g->brk) return at - 1;      /* past the row's end: its line end */
    }
    return at;
}

/* Splits g after k characters; the new segment (the rest) follows it */
static struct tdt_seg *split(const struct odt_cenv *env, const struct tdt_measure *m, struct tdt_layout *lay,
                             struct tdt_seg *g, odt_u32 k)
{
    struct tdt_seg *h = seg_alloc(env, lay);
    odt_s32 w;
    if (!h) return 0;
    *h = *g;
    w = k ? m->len(m, g->l.ln_Style, g->l.ln_Text, k) : 0;
    h->l.ln_Text = g->l.ln_Text + k;
    h->l.ln_TextLen = g->l.ln_TextLen - k;
    h->l.ln_XOffset = (odt_u16)(g->l.ln_XOffset + w);
    h->l.ln_Width = (odt_u16)(h->l.ln_TextLen ? m->len(m, h->l.ln_Style, h->l.ln_Text, h->l.ln_TextLen) : 0);
    h->tab = 0;
    g->l.ln_TextLen = k;
    g->l.ln_Width = (odt_u16)w;
    g->l.ln_Flags &= ~TDT_LNF_LF;
    g->brk = TDT_BRK_NONE;
    dtc_insert_after(&g->l.ln_Link, &h->l.ln_Link);
    lay->segs++;
    return h;
}

odt_u32 tdt_select(const struct odt_cenv *env, const struct tdt_measure *m, struct tdt_layout *lay, odt_s32 x0,
                   odt_s32 y0, odt_s32 x1, odt_s32 y1)
{
    odt_u32 a = place_at(m, lay, x0, y0), b = place_at(m, lay, x1, y1), at = 0, count = 0;
    struct tdt_seg *g, *next;
    if (a > b) { odt_u32 t = a; a = b; b = t; }
    g = FIRST(lay);
    if (lay->row_at && lay->rows) {
        /* from the row a is on: the last whose places start at or before it */
        odt_u32 lo = 0, hi = lay->rows - 1;
        while (lo < hi) {
            odt_u32 mid = (lo + hi + 1) / 2;
            if (lay->row_at[mid] <= a) lo = mid;
            else hi = mid - 1;
        }
        g = lay->row_seg[lo];
        at = lay->row_at[lo];
    }
    for (; !END(g); g = next) {
        odt_u32 n = places(g), p0 = at, p1 = at + n - 1, c0 = at + g->tab;
        next = NEXT(g);
        at += n;
        if (p0 > b) break;              /* the rest come later still */
        if (!n || p1 < a) continue;
        if (a <= p0 && p1 <= b) {
            g->l.ln_Flags |= TDT_LNF_SELECTED;
            count++;
            continue;
        }
        {
            odt_u32 ca = a > p0 ? a : p0, cb = b < p1 ? b : p1;
            struct tdt_seg *mid = g;
            if (cb < p1) split(env, m, lay, g, cb + 1 - c0);
            if (ca > c0) mid = split(env, m, lay, g, ca - c0);
            else if (ca > p0) mid = split(env, m, lay, g, 0);   /* the tab stays out */
            if (mid) { mid->l.ln_Flags |= TDT_LNF_SELECTED; count++; }
        }
    }
    return count;
}

void tdt_clear_selected(struct tdt_layout *lay)
{
    struct tdt_seg *g;
    for (g = FIRST(lay); !END(g); g = NEXT(g)) g->l.ln_Flags &= ~TDT_LNF_SELECTED;
}

odt_u32 tdt_copy_text(const struct tdt_layout *lay, int all, char *buf, odt_u32 size)
{
    const struct tdt_seg *g;
    odt_u32 n = 0, k;
    int any = 0;
    if (!all)
        for (g = FIRST(lay); !END(g); g = NEXT(g))
            if (g->l.ln_Flags & TDT_LNF_SELECTED) { any = 1; break; }
#define PUT(c) do { if (buf && n < size) buf[n] = (c); n++; } while (0)
    for (g = FIRST(lay); !END(g); g = NEXT(g)) {
        if (any && !(g->l.ln_Flags & TDT_LNF_SELECTED)) continue;
        if (g->tab) PUT('\t');
        for (k = 0; k < g->l.ln_TextLen; k++) PUT(g->l.ln_Text[k]);
        if (g->brk == TDT_BRK_HARD) PUT('\n');
        else if (g->brk == TDT_BRK_SPACE) PUT(' ');
    }
#undef PUT
    return n;
}

odt_u32 tdt_ftxt(const char *text, odt_u32 n, odt_u8 *buf)
{
    odt_u32 chrs = n, form = 4 + 8 + chrs + (chrs & 1), i;
    odt_u32 ids[3] = { TDT_ID_FORM, form, TDT_ID_FTXT };
    for (i = 0; i < 12; i++) buf[i] = (odt_u8)(ids[i / 4] >> (24 - 8 * (i % 4)));
    ids[0] = TDT_ID_CHRS;
    ids[1] = chrs;
    for (i = 0; i < 8; i++) buf[12 + i] = (odt_u8)(ids[i / 4] >> (24 - 8 * (i % 4)));
    for (i = 0; i < n; i++) buf[20 + i] = (odt_u8)text[i];
    if (n & 1) buf[20 + n] = 0;
    return 20 + n + (n & 1);
}

static int is_delim(const char *d, char c)
{
    for (; *d; d++)
        if (*d == c) return 1;
    return c == '\n' || c == '\r' || c == 0;
}

odt_u32 tdt_word_at(const struct tdt_measure *m, const struct tdt_layout *lay, const char *delims, odt_s32 x, odt_s32 y,
                    char *buf, odt_u32 size)
{
    const struct tdt_seg *g;
    odt_u32 row, k, a, b, n = 0;
    if (!delims) delims = tdt_default_delims;
    if (size) buf[0] = 0;
    if (y < 0 || x < 0) return 0;
    row = (odt_u32)y / m->ysize;
    for (g = row_start(lay, row, &k); !END(g) && g->row <= row; g = NEXT(g)) {
        if (g->row != row || x < g->l.ln_XOffset || x >= g->l.ln_XOffset + g->l.ln_Width) continue;
        for (k = 0; k < g->l.ln_TextLen; k++)
            if (m->len(m, g->l.ln_Style, g->l.ln_Text, k + 1) > x - g->l.ln_XOffset) break;
        if (k >= g->l.ln_TextLen || is_delim(delims, g->l.ln_Text[k])) return 0;
        /* the word stops at the segment's ends (ABI test: across styles) */
        for (a = k; a > 0 && !is_delim(delims, g->l.ln_Text[a - 1]); a--) {}
        for (b = k + 1; b < g->l.ln_TextLen && !is_delim(delims, g->l.ln_Text[b]); b++) {}
        for (k = a; k < b && n + 1 < size; k++) buf[n++] = g->l.ln_Text[k];
        if (size) buf[n] = 0;
        return n;
    }
    return 0;
}

/* ---- drawing ------------------------------------------------------------------ */

void tdt_render(const struct odt_cenv *env, const struct tdt_ext *x, const struct tdt_measure *m,
                const struct tdt_layout *lay, void *rp, const struct tdt_pens *pens, odt_s32 left, odt_s32 top,
                odt_s32 width, odt_s32 height, odt_u32 top_row, odt_s32 left_px)
{
    const struct tdt_seg *g;
    void *clip;
    odt_u32 rows = m->ysize ? (odt_u32)(height + m->ysize - 1) / m->ysize : 0, at;
    if (width <= 0 || height <= 0) return;
    clip = x->clip ? x->clip(env, rp, left, top, left + width - 1, top + height - 1) : 0;
    x->fill(env, rp, pens->back, left, top, left + width - 1, top + height - 1);
    for (g = row_start(lay, top_row, &at); !END(g); g = NEXT(g)) {
        odt_s32 sx, sy, fg;
        if (g->row < top_row) continue;
        if (g->row >= top_row + rows) break;
        sx = left + g->l.ln_XOffset - left_px;
        sy = top + (odt_s32)(g->row - top_row) * m->ysize;
        if (sx >= left + width || sx + g->l.ln_Width <= left || !g->l.ln_TextLen) continue;
        fg = g->l.ln_FgPen;
        if (g->l.ln_Flags & TDT_LNF_SELECTED) {
            x->fill(env, rp, pens->fill, sx, sy, sx + g->l.ln_Width - 1, sy + m->ysize - 1);
            fg = pens->filltext;
        } else if (g->l.ln_BgPen != DEF_BG)
            x->fill(env, rp, g->l.ln_BgPen, sx, sy, sx + g->l.ln_Width - 1, sy + m->ysize - 1);
        x->text(env, rp, m->font, g->l.ln_Style, fg, sx, sy, g->l.ln_Text, g->l.ln_TextLen);
    }
    if (clip) x->unclip(env, rp, clip);
}

/* ---- the class ------------------------------------------------------------------ */

#define EXT(env) ((const struct tdt_ext *)(env)->class_ext)

static odt_s32 ext_len(const struct tdt_measure *m, odt_u32 style, const char *s, odt_u32 n)
{
    const struct tdt_ext *x = EXT(m->env);
    if (!m->font || !x) return (odt_s32)(n * m->xsize);
    if (m->rp && x->measure_len) return x->measure_len(m->env, m->rp, style, s, n);
    if (!x->text_length) return (odt_s32)(n * m->xsize);
    return x->text_length(m->env, m->font, style, s, n);
}

static void set_font(const struct odt_cenv *env, struct tdt_inst *in, void *text_attr, struct odt_GadgetInfo *gi)
{
    const struct tdt_ext *x = EXT(env);
    if (in->font && in->open_font && x->close_font) x->close_font(env, in->font);
    in->font = 0;
    in->open_font = 0;
    if (text_attr && x->open_font && (in->font = x->open_font(env, text_attr)) != 0) in->open_font = 1;
    else if (x->default_font) in->font = x->default_font(env, gi);
    in->m.env = env;
    in->m.font = in->font;
    in->m.len = ext_len;
    in->m.ysize = 8;
    in->m.baseline = 6;
    in->m.xsize = 8;
    if (in->font && x->font_size) x->font_size(env, in->font, &in->m.ysize, &in->m.baseline, &in->m.xsize);
    if (!in->m.ysize) in->m.ysize = 1;
    if (!in->m.xsize) in->m.xsize = 1;
}

static odt_u32 slen(const char *s)
{
    odt_u32 n = 0;
    while (s[n]) n++;
    return n;
}

static void drop_layout(const struct odt_cenv *env, struct tdt_inst *in)
{
    tdt_free_layout(env, &in->lay);
    in->laid_out = 0;
}

/* The attributes the class keeps; 1 when the layout must be made again */
static int set_attrs(const struct odt_cenv *env, struct tdt_inst *in, const struct odt_TagItem *tags,
                     struct odt_GadgetInfo *gi)
{
    const struct odt_TagItem *t, *list = tags;
    const char *nbuf = 0;
    odt_u32 nlen = ~0UL;
    int given = 0, changed = 0, oom = 0;
    while ((t = dtc_next_tag(&list)) != 0) {
        switch (t->ti_Tag) {
        case TDT_TDTA_Buffer: nbuf = (const char *)t->ti_Data; given = 1; break;
        case TDT_TDTA_BufferLen: nlen = t->ti_Data; given = 1; break;
        case TDT_TDTA_WordWrap:
            if ((t->ti_Data != 0) != in->word_wrap) { in->word_wrap = t->ti_Data != 0; changed = 1; }
            break;
        case TDT_TDTA_WordDelim: in->word_delim = (char *)t->ti_Data; break;
        case TDT_TDTA_LineList: in->user_lines = (struct odt_MinList *)t->ti_Data; changed = 1; break;
        case ODT_DTA_TextAttr: set_font(env, in, (void *)t->ti_Data, gi); changed = 1; break;
        default: break;
        }
    }
    if (given) {
        /* our own copy: the caller keeps theirs (ABI test: whether OS
         * 3.2's frees a subclass's buffer) */
        char *copy = 0;
        odt_u32 i;
        if (!nbuf && nlen == ~0UL) nlen = in->buffer_len, nbuf = in->buffer;
        if (nbuf == in->buffer && nlen == ~0UL) nlen = in->buffer_len;
        if (nlen == ~0UL) nlen = nbuf ? slen(nbuf) : 0;
        if (!nbuf) nlen = 0;
        if (nbuf != in->buffer || nlen != in->buffer_len) {
            copy = env->alloc(env, nlen + 1);
            if (!copy && nlen)
                oom = 1;                    /* no memory for the copy: the old text stays, the caller fails */
            if (copy || !nlen) {
                for (i = 0; copy && i < nlen; i++) copy[i] = nbuf[i];
                drop_layout(env, in);
                if (in->buffer) env->free(env, in->buffer);
                in->buffer = copy;
                in->buffer_len = copy ? nlen : 0;
                changed = 1;
            }
        }
    }
    if (changed) drop_layout(env, in);
    return oom ? -1 : changed;
}

static void notify(const struct odt_cenv *env, void *obj, struct odt_GadgetInfo *gi, struct odt_TagItem *tags)
{
    struct odt_opUpdate u;
    u.MethodID = ODT_OM_NOTIFY;
    u.opu_AttrList = tags;
    u.opu_GInfo = gi;
    u.opu_Flags = 0;
    env->do_method(env, obj, &u);
}

static struct odt_IBox domain_of(const struct odt_cenv *env, void *obj)
{
    struct odt_IBox box = { 0, 0, 0, 0 };
    odt_u32 v = 0;
    if (dtc_get(env, obj, ODT_DTA_Domain, &v) && v) box = *(struct odt_IBox *)v;
    else {
        struct odt_Gadget *g = obj;
        box.Left = g->LeftEdge; box.Top = g->TopEdge; box.Width = g->Width; box.Height = g->Height;
    }
    return box;
}

/* The layout, made for the box if it is out of date; the scroll numbers
 * in the DTSpecialInfo, then told. Returns the rows. */
static odt_u32 layout(const struct odt_cenv *env, struct tdt_inst *in, void *obj, struct odt_GadgetInfo *gi,
                      odt_s32 boxw, odt_s32 boxh, int tell)
{
    struct odt_DTSpecialInfo *si = dtc_si(obj);
    odt_u32 wrap = in->word_wrap && boxw > 0 ? (odt_u32)boxw : 0, rows, cols;
    struct odt_TagItem tags[8];
    if (!in->m.len) set_font(env, in, 0, gi);
    if (in->user_lines) {
        /* a subclass's own lines: as many rows as the last one reaches */
        struct odt_MinNode *n;
        rows = 0;
        cols = 0;
        for (n = in->user_lines->mlh_Head; n && n->mln_Succ; n = n->mln_Succ) {
            struct tdt_Line *l = (struct tdt_Line *)n;
            odt_u32 r = (odt_u32)(l->ln_YOffset + l->ln_Height + in->m.ysize - 1) / in->m.ysize;
            if (r > rows) rows = r;
            if ((odt_u32)(l->ln_XOffset + l->ln_Width) > cols) cols = l->ln_XOffset + l->ln_Width;
        }
    } else {
        if (!in->laid_out || wrap != in->laid_wrap) {
            /* one RastPort for the whole layout's measuring, when the
             * shell has them */
            const struct tdt_ext *x = EXT(env);
            struct tdt_measure lm = in->m;
            lm.rp = in->font && x && x->measure_open && x->measure_len && x->measure_close ?
                    x->measure_open(env, in->font) : 0;
            drop_layout(env, in);
            tdt_layout(env, &lm, in->buffer ? in->buffer : "", in->buffer_len, wrap, &in->lay);
            if (lm.rp) x->measure_close(env, lm.rp);
            in->laid_out = 1;
            in->laid_wrap = wrap;
        }
        rows = in->lay.rows;
        cols = in->lay.width;
    }
    cols = (cols + in->m.xsize - 1) / in->m.xsize;
    if (si) {
        si->si_VertUnit = in->m.ysize;
        si->si_VisVert = boxh / in->m.ysize;
        si->si_TotVert = (odt_s32)rows;
        si->si_HorizUnit = in->m.xsize;
        si->si_VisHoriz = boxw / in->m.xsize;
        si->si_TotHoriz = (odt_s32)cols;
    }
    if (tell) {
        tags[0].ti_Tag = ODT_DTA_VisibleVert;  tags[0].ti_Data = (odt_u32)(boxh / in->m.ysize);
        tags[1].ti_Tag = ODT_DTA_TotalVert;    tags[1].ti_Data = rows;
        tags[2].ti_Tag = ODT_DTA_NominalVert;  tags[2].ti_Data = rows * in->m.ysize;
        tags[3].ti_Tag = ODT_DTA_VisibleHoriz; tags[3].ti_Data = (odt_u32)(boxw / in->m.xsize);
        tags[4].ti_Tag = ODT_DTA_TotalHoriz;   tags[4].ti_Data = cols;
        tags[5].ti_Tag = ODT_DTA_NominalHoriz; tags[5].ti_Data = cols * in->m.xsize;
        tags[6].ti_Tag = ODT_DTA_Sync;         tags[6].ti_Data = 1;
        tags[7].ti_Tag = ODT_TAG_DONE;         tags[7].ti_Data = 0;
        notify(env, obj, gi, tags);
    }
    return rows;
}

static void pens_of(const struct odt_cenv *env, struct odt_GadgetInfo *gi, struct tdt_pens *p)
{
    const struct tdt_ext *x = EXT(env);
    p->text = x->pen ? x->pen(env, gi, TDT_TEXTPEN) : 1;
    p->back = x->pen ? x->pen(env, gi, TDT_BACKGROUNDPEN) : 0;
    p->fill = x->pen ? x->pen(env, gi, TDT_FILLPEN) : 3;
    p->filltext = x->pen ? x->pen(env, gi, TDT_FILLTEXTPEN) : 1;
}

/* Draws a subclass's own lines (no tabs or wraps known) */
static void render_user(const struct odt_cenv *env, struct tdt_inst *in, void *rp, const struct tdt_pens *pens,
                        struct odt_IBox box, odt_s32 top_px, odt_s32 left_px)
{
    const struct tdt_ext *x = EXT(env);
    struct odt_MinNode *n;
    void *clip = x->clip ? x->clip(env, rp, box.Left, box.Top, box.Left + box.Width - 1, box.Top + box.Height - 1) : 0;
    x->fill(env, rp, pens->back, box.Left, box.Top, box.Left + box.Width - 1, box.Top + box.Height - 1);
    for (n = in->user_lines->mlh_Head; n && n->mln_Succ; n = n->mln_Succ) {
        struct tdt_Line *l = (struct tdt_Line *)n;
        odt_s32 sx = box.Left + l->ln_XOffset - left_px, sy = box.Top + l->ln_YOffset - top_px;
        odt_s32 fg = l->ln_FgPen;
        if (sy + l->ln_Height <= box.Top || sy >= box.Top + box.Height || !l->ln_TextLen) continue;
        if (l->ln_Flags & TDT_LNF_SELECTED) {
            x->fill(env, rp, pens->fill, sx, sy, sx + l->ln_Width - 1, sy + l->ln_Height - 1);
            fg = pens->filltext;
        }
        x->text(env, rp, in->m.font, l->ln_Style, fg, sx, sy, l->ln_Text, l->ln_TextLen);
    }
    if (clip) x->unclip(env, rp, clip);
}

static void draw(const struct odt_cenv *env, struct tdt_inst *in, void *obj, struct odt_GadgetInfo *gi, void *rp,
                 struct odt_IBox box, odt_s32 top_vert, odt_s32 top_horiz)
{
    struct tdt_pens pens;
    pens_of(env, gi, &pens);
    if (top_vert < 0) top_vert = 0;
    if (top_horiz < 0) top_horiz = 0;
    if (!in->user_lines && !in->laid_out) layout(env, in, obj, gi, box.Width, box.Height, 0);
    if (in->user_lines)
        render_user(env, in, rp, &pens, box, top_vert * in->m.ysize, top_horiz * in->m.xsize);
    else
        tdt_render(env, EXT(env), &in->m, &in->lay, rp, &pens, box.Left, box.Top, box.Width, box.Height,
                   (odt_u32)top_vert, top_horiz * in->m.xsize);
}

/* Draws the object again where it is, after a selection changes */
static void redraw(const struct odt_cenv *env, void *obj, struct odt_GadgetInfo *gi)
{
    struct odt_gpRender r;
    void *rp;
    if (!gi || !env->obtain_rp || !(rp = env->obtain_rp(env, gi))) return;
    r.MethodID = ODT_GM_RENDER;
    r.gpr_GInfo = gi;
    r.gpr_RPort = rp;
    r.gpr_Redraw = ODT_GREDRAW_UPDATE;
    env->do_method(env, obj, &r);
    env->release_rp(env, rp);
}

static odt_u32 copy_to_clip(const struct odt_cenv *env, struct tdt_inst *in)
{
    odt_u32 n = tdt_copy_text(&in->lay, 0, 0, 0), ok = 0;
    char *text = env->alloc(env, n + 1);
    void *iff;
    if (!text) { env->set_io_err(env, DTC_ERROR_NO_FREE_STORE); return 0; }
    tdt_copy_text(&in->lay, 0, text, n);
    /* unit 0, PRIMARY_CLIP (ABI test) */
    if (env->iff_open_clip && (iff = env->iff_open_clip(env, 0, 1)) != 0) {
        ok = env->iff_push(env, iff, TDT_ID_FTXT, TDT_ID_FORM, -1) == 0 &&
             env->iff_push(env, iff, TDT_ID_FTXT, TDT_ID_CHRS, (odt_s32)n) == 0 &&
             env->iff_write_chunk(env, iff, text, (odt_s32)n) == (odt_s32)n &&
             env->iff_pop(env, iff) == 0 && env->iff_pop(env, iff) == 0;
        env->iff_close(env, iff);
    }
    env->free(env, text);
    return ok;
}

static odt_u32 write_out(const struct odt_cenv *env, struct tdt_inst *in, void *obj, struct odt_dtWrite *w)
{
    if (!w->dtw_FileHandle) return 0;
    if (w->dtw_Mode == ODT_DTWM_RAW) {
        /* the text as it was given, escapes and all */
        odt_s32 n = (odt_s32)in->buffer_len;
        return !n || env->write(env, w->dtw_FileHandle, in->buffer, n) == n;
    } else {
        odt_u32 n, len, ok;
        odt_u8 *form;
        char *text;
        if (!in->laid_out && !in->user_lines) {
            struct odt_IBox box = domain_of(env, obj);
            layout(env, in, obj, w->dtw_GInfo, box.Width, box.Height, 0);
        }
        n = tdt_copy_text(&in->lay, 1, 0, 0);
        text = env->alloc(env, n + 1);
        form = env->alloc(env, n + 22);
        if (!text || !form) {
            env->free(env, text);
            env->free(env, form);
            env->set_io_err(env, DTC_ERROR_NO_FREE_STORE);
            return 0;
        }
        tdt_copy_text(&in->lay, 1, text, n);
        len = tdt_ftxt(text, n, form);
        ok = env->write(env, w->dtw_FileHandle, form, (odt_s32)len) == (odt_s32)len;
        env->free(env, text);
        env->free(env, form);
        return ok;
    }
}

static void word_select(const struct odt_cenv *env, struct tdt_inst *in, void *obj, struct odt_gpInput *gp)
{
    struct odt_InputEvent *ie = gp->gpi_IEvent;
    struct odt_DTSpecialInfo *si = dtc_si(obj);
    const struct tdt_ext *x = EXT(env);
    odt_s32 cx, cy;
    if (!ie || ie->ie_Class != ODT_IECLASS_RAWMOUSE || ie->ie_Code != ODT_SELECTDOWN || !in->laid_out) return;
    cx = gp->gpi_Mouse.x + (si ? si->si_TopHoriz : 0) * in->m.xsize;
    cy = gp->gpi_Mouse.y + (si ? si->si_TopVert : 0) * in->m.ysize;
    if (x->double_click && (in->last_secs || in->last_micro) &&
        x->double_click(env, in->last_secs, in->last_micro, ie->ie_Secs, ie->ie_Micro) &&
        cx / in->m.xsize == in->last_x / in->m.xsize && cy / in->m.ysize == in->last_y / in->m.ysize) {
        struct odt_TagItem tags[2];
        tdt_word_at(&in->m, &in->lay, in->word_delim, cx, cy, in->word, sizeof in->word);
        tags[0].ti_Tag = TDT_TDTA_WordSelect;
        tags[0].ti_Data = (odt_u32)in->word;
        tags[1].ti_Tag = ODT_TAG_DONE;
        tags[1].ti_Data = 0;
        notify(env, obj, gp->gpi_GInfo, tags);
        in->last_secs = in->last_micro = 0;
        return;
    }
    in->last_secs = ie->ie_Secs;
    in->last_micro = ie->ie_Micro;
    in->last_x = cx;
    in->last_y = cy;
}

odt_u32 tdt_inst_size(void) { return sizeof(struct tdt_inst); }

odt_u32 tdt_dispatch(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, void *msg)
{
    odt_u32 id = *(odt_u32 *)msg;
    struct tdt_inst *in = (id == ODT_OM_NEW) ? 0 : ODT_INST_DATA(cl, obj);
    switch (id) {
    case ODT_OM_NEW: {
        struct odt_opSet *ops = msg;
        odt_u32 ta = 0;
        void *o = (void *)env->do_super(env, cl, obj, msg);
        if (!o) return 0;
        in = ODT_INST_DATA(cl, o);
        dtc_list_init(&in->lay.lines);
        /* the font: DTA_TextAttr (ours or the superclass's), else the screen's */
        ta = dtc_tag_data(ops->ops_AttrList, ODT_DTA_TextAttr, 0);
        if (!ta) dtc_get(env, o, ODT_DTA_TextAttr, &ta);
        set_font(env, in, (void *)ta, ops->ops_GInfo);
        if (set_attrs(env, in, ops->ops_AttrList, ops->ops_GInfo) < 0) {
            /* TDTA_Buffer couldn't be copied: no object without its text */
            struct odt_dtGeneral d;
            d.MethodID = ODT_OM_DISPOSE;
            d.dtg_GInfo = 0;
            env->coerce(env, cl, o, &d);
            env->set_io_err(env, DTC_ERROR_NO_FREE_STORE);
            return 0;
        }
        return (odt_u32)o;
    }
    case ODT_OM_DISPOSE:
        drop_layout(env, in);
        if (in->buffer) env->free(env, in->buffer);
        in->buffer = 0;
        if (in->font && in->open_font && EXT(env)->close_font) EXT(env)->close_font(env, in->font);
        in->font = 0;
        return env->do_super(env, cl, obj, msg);
    case ODT_OM_SET:
    case ODT_OM_UPDATE: {
        struct odt_opSet *ops = msg;
        odt_u32 r = env->do_super(env, cl, obj, msg);
        if (env->sem_obtain && dtc_si(obj)) env->sem_obtain(env, &dtc_si(obj)->si_Lock);
        {
            int c = set_attrs(env, in, ops->ops_AttrList, ops->ops_GInfo);
            if (c < 0) env->set_io_err(env, DTC_ERROR_NO_FREE_STORE);   /* the update failed: IoErr says so */
            else r += (odt_u32)c;
        }
        if (env->sem_release && dtc_si(obj)) env->sem_release(env, &dtc_si(obj)->si_Lock);
        return r;
    }
    case ODT_OM_GET: {
        struct odt_opGet *g = msg;
        switch (g->opg_AttrID) {
        case TDT_TDTA_Buffer: *g->opg_Storage = (odt_u32)in->buffer; return 1;
        case TDT_TDTA_BufferLen: *g->opg_Storage = in->buffer_len; return 1;
        case TDT_TDTA_LineList:
            *g->opg_Storage = (odt_u32)(in->user_lines ? in->user_lines : &in->lay.lines);
            return 1;
        case TDT_TDTA_WordSelect: *g->opg_Storage = (odt_u32)in->word; return 1;
        case TDT_TDTA_WordDelim:
            *g->opg_Storage = (odt_u32)(in->word_delim ? in->word_delim : tdt_default_delims);
            return 1;
        case TDT_TDTA_WordWrap: *g->opg_Storage = in->word_wrap; return 1;
        case ODT_DTA_Methods: *g->opg_Storage = (odt_u32)tdt_methods; return 1;
        case ODT_DTA_TextFont: *g->opg_Storage = (odt_u32)in->font; return 1;
        default: return env->do_super(env, cl, obj, msg);
        }
    }
    case ODT_DTM_PROCLAYOUT:
    case ODT_DTM_ASYNCLAYOUT: {
        struct odt_gpLayout *gpl = msg;
        struct odt_IBox box;
        odt_u32 rows;
        int lock = id == ODT_DTM_PROCLAYOUT && env->sem_obtain && dtc_si(obj);
        if (id == ODT_DTM_PROCLAYOUT) env->do_super(env, cl, obj, msg);
        /* DTM_ASYNCLAYOUT runs with si_Lock held by the layout process */
        if (lock) env->sem_obtain(env, &dtc_si(obj)->si_Lock);
        box = domain_of(env, obj);
        rows = layout(env, in, obj, gpl->gpl_GInfo, box.Width, box.Height, 1);
        if (lock) env->sem_release(env, &dtc_si(obj)->si_Lock);
        return rows;
    }
    case ODT_GM_RENDER: {
        struct odt_gpRender *r = msg;
        struct odt_DTSpecialInfo *si = dtc_si(obj);
        struct odt_IBox box;
        /* a layout running holds the lock: it draws when it is done */
        if (si && env->sem_attempt && !env->sem_attempt(env, &si->si_Lock)) return 0;
        box = domain_of(env, obj);
        draw(env, in, obj, r->gpr_GInfo, r->gpr_RPort, box, si ? si->si_TopVert : 0, si ? si->si_TopHoriz : 0);
        if (si && env->sem_attempt) env->sem_release(env, &si->si_Lock);
        return 1;
    }
    case ODT_DTM_DRAW: {
        struct odt_dtDraw *d = msg;
        struct odt_IBox box;
        box.Left = (odt_s16)d->dtd_Left;
        box.Top = (odt_s16)d->dtd_Top;
        box.Width = (odt_s16)d->dtd_Width;
        box.Height = (odt_s16)d->dtd_Height;
        if (!in->user_lines && (!in->laid_out || (in->word_wrap && in->laid_wrap != (odt_u32)d->dtd_Width)))
            layout(env, in, obj, 0, d->dtd_Width, d->dtd_Height, 0);
        draw(env, in, obj, 0, d->dtd_RPort, box, d->dtd_TopVert, d->dtd_TopHoriz);
        return 1;
    }
    case ODT_DTM_SELECT: {
        struct odt_dtSelect *s = msg;
        odt_u32 n;
        if (in->user_lines || !in->laid_out) return 0;
        tdt_clear_selected(&in->lay);
        n = tdt_select(env, &in->m, &in->lay, s->dts_Select.MinX, s->dts_Select.MinY, s->dts_Select.MaxX,
                       s->dts_Select.MaxY);
        env->do_super(env, cl, obj, msg);
        redraw(env, obj, s->dts_GInfo);
        return n;
    }
    case ODT_DTM_CLEARSELECTED: {
        struct odt_dtGeneral *g = msg;
        if (!in->user_lines) tdt_clear_selected(&in->lay);
        env->do_super(env, cl, obj, msg);
        redraw(env, obj, g->dtg_GInfo);
        return 1;
    }
    case ODT_DTM_COPY:
        if (in->user_lines) return 0;
        if (!in->laid_out) {
            struct odt_IBox box = domain_of(env, obj);
            layout(env, in, obj, 0, box.Width, box.Height, 0);
        }
        return copy_to_clip(env, in);
    case ODT_DTM_WRITE:
        return write_out(env, in, obj, msg);
    case ODT_GM_GOACTIVE:
        word_select(env, in, obj, msg);
        return env->do_super(env, cl, obj, msg);
    default:
        return env->do_super(env, cl, obj, msg);
    }
}
