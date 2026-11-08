/* ByteRun1 across rows. Included by ilbm_core.cpp (inside its namespace)
 * and by the host test. left > 0: literals still to copy. left < 0: repeats
 * still to copy. pend: the repeat's byte has not been read yet. */
struct br1 { u32 i; s32 left; u8 rep; u8 pend; };

static void br1_init(struct br1 *s) { s->i = 0; s->left = 0; s->rep = 0; s->pend = 0; }

static u32 br1_take(struct br1 *s, const u8 *in, u32 len, u8 *out, u32 n)
{
    u32 o = 0;
    while (o < n) {
        /* a repeat's byte is read before any of it is copied */
        if (s->pend) {
            if (s->i >= len)
                break;
            s->rep = in[s->i++];
            s->pend = 0;
        }
        if (s->left > 0) {
            u32 m = (u32)s->left;
            if (m > n - o) m = n - o;
            if (s->i >= len) break;
            if (m > len - s->i) m = len - s->i;
            for (u32 k = 0; k < m; k++)
                out[o++] = in[s->i++];
            s->left -= (s32)m;
            continue;
        }
        if (s->left < 0) {
            u32 m = (u32)(-s->left);
            u8 v = s->rep;
            if (m > n - o) m = n - o;
            for (u32 k = 0; k < m; k++)
                out[o++] = v;
            s->left += (s32)m;
            continue;
        }
        if (s->i >= len) break;
        {
            s32 c = (s32)(odt_s8)in[s->i++];
            if (c >= 0)
                s->left = c + 1;
            else if (c != -128) {
                s->left = c - 1;           /* copies = -c + 1 */
                s->pend = 1;
            }
        }
    }
    return o;
}
