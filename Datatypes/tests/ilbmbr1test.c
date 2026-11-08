/* ByteRun1 a row at a time matches unpacking the whole buffer, including a
 * run that crosses the row and a short input (the rest stays 0).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef int32_t s32;
typedef int8_t odt_s8;

#include "../../libraries/opendatatypes/classes/datatypes/ilbm/ilbm_byterun.inc"

static u32 naive(const u8 *in, u32 len, u8 *out, u32 n)
{
    u32 i = 0, o = 0;
    while (o < n && i < len) {
        s32 c = (s32)(odt_s8)in[i++];
        if (c >= 0) {
            for (s32 k = 0; k <= c && o < n && i < len; k++)
                out[o++] = in[i++];
        } else if (c != -128) {
            u8 v;
            if (i >= len)
                break;
            v = in[i++];
            for (s32 k = 0; k <= -c && o < n; k++)
                out[o++] = v;
        }
    }
    return i;
}

static int same(const u8 *in, u32 len, u32 n, u32 row)
{
    u8 whole[4096], rows[4096], one[4096];
    struct br1 s;
    u32 o = 0, used;
    if (n > sizeof whole || !row)
        return 0;
    memset(whole, 0, n);
    memset(rows, 0, n);
    naive(in, len, whole, n);
    br1_init(&s);
    while (o < n) {
        u32 m = row;
        u32 got;
        if (m > n - o) m = n - o;
        memset(one, 0xa5, m);
        got = br1_take(&s, in, len, one, m);
        if (got < m)
            memset(one + got, 0, m - got);
        memcpy(rows + o, one, m);
        o += m;
    }
    if (memcmp(whole, rows, n) != 0)
        return 0;
    memset(one, 0x5a, n);
    used = naive(in, len, one, n);
    /* the one-shot cursor and the naive cursor agree on a full buffer */
    {
        struct br1 t;
        u8 buf[4096];
        br1_init(&t);
        memset(buf, 0x5a, n);
        br1_take(&t, in, len, buf, n);
        if (t.i != used || memcmp(buf, one, n) != 0)
            return 0;
    }
    return 1;
}

int main(void)
{
    u8 in[512];
    u32 n, i, row;
    /* literals, a repeat, a skip, a repeat that is longer than one row */
    in[0] = 3; in[1] = 1; in[2] = 2; in[3] = 3; in[4] = 4;
    in[5] = (u8)(s32)-5; in[6] = 9;          /* 6 nines */
    in[7] = (u8)(s32)-128;
    in[8] = 0; in[9] = 7;
    in[10] = (u8)(s32)-127; in[11] = 8;      /* 128 eights, crosses every row size */
    n = 12;
    for (row = 1; row <= 40; row++) {
        if (!same(in, n, 4 + 6 + 1 + 128, row)) {
            printf("FAIL row %u\n", row);
            return 1;
        }
    }
    /* input ends in the middle of a literal and of a repeat's byte */
    in[0] = 10;
    if (!same(in, 1, 20, 3)) {
        printf("FAIL short literal\n");
        return 1;
    }
    in[0] = (u8)(s32)-3;
    if (!same(in, 1, 10, 2)) {
        printf("FAIL short repeat\n");
        return 1;
    }
    /* every byte of a 200-byte pattern, cut into rows */
    for (i = 0; i < 200; i++)
        in[i] = (u8)(i * 17 + 3);
    for (row = 1; row <= 30; row++) {
        if (!same(in, 200, 180, row)) {
            printf("FAIL pattern row %u\n", row);
            return 1;
        }
    }
    printf("ok\n");
    return 0;
}
