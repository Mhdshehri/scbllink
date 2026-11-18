/* inflate.c - see inflate.h. Written from RFC 1951; every read and write is bounds-checked,
 * because the data comes from the network. By Mhdshehri. */
#include "inflate.h"

typedef struct { const uint8_t *in; int n, pos; uint32_t bits; int nbits; uint8_t *out; int cap, len; bool bad; } Z;
typedef struct { uint16_t count[16], sym[288]; } Huff;

static int bits(Z *z, int need)
{
    while (z->nbits < need) {
        if (z->pos >= z->n) { z->bad = true; return 0; }
        z->bits |= (uint32_t)z->in[z->pos++] << z->nbits;
        z->nbits += 8;
    }
    int v = (int)(z->bits & ((1u << need) - 1));
    z->bits >>= need;
    z->nbits -= need;
    return v;
}

static bool build(Huff *h, const uint8_t *len, int n)
{
    uint16_t offs[16];
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++) h->count[len[i]]++;
    h->count[0] = 0;
    int left = 1;
    for (int i = 1; i < 16; i++) {
        left = (left << 1) - h->count[i];
        if (left < 0) return false; /* more codes than the lengths allow */
    }
    offs[1] = 0;
    for (int i = 1; i < 15; i++) offs[i + 1] = (uint16_t)(offs[i] + h->count[i]);
    for (int i = 0; i < n; i++)
        if (len[i]) h->sym[offs[len[i]]++] = (uint16_t)i;
    return true;
}

static int decode(Z *z, const Huff *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        code |= bits(z, 1);
        if (z->bad) return -1;
        int count = h->count[len];
        if (code - count < first) return h->sym[index + (code - first)];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    z->bad = true;
    return -1;
}

static const uint16_t LBASE[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t LEXT[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t DBASE[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const uint8_t DEXT[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static void codes(Z *z, const Huff *lit, const Huff *dist)
{
    for (;;) {
        int s = decode(z, lit);
        if (z->bad) return;
        if (s < 256) {
            if (z->len >= z->cap) { z->bad = true; return; }
            z->out[z->len++] = (uint8_t)s;
        } else if (s == 256) {
            return;
        } else {
            s -= 257;
            if (s >= 29) { z->bad = true; return; }
            int len = LBASE[s] + bits(z, LEXT[s]);
            int d = decode(z, dist);
            if (z->bad || d >= 30) { z->bad = true; return; }
            int back = DBASE[d] + bits(z, DEXT[d]);
            if (z->bad || back > z->len || len > z->cap - z->len) { z->bad = true; return; }
            for (int i = 0; i < len; i++, z->len++) z->out[z->len] = z->out[z->len - back];
        }
    }
}

static void dynamic(Z *z)
{
    static const uint8_t ORDER[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    uint8_t len[320];
    Huff a, b;
    int nlen = bits(z, 5) + 257, ndist = bits(z, 5) + 1, ncode = bits(z, 4) + 4;
    if (z->bad || nlen > 286 || ndist > 30) { z->bad = true; return; }
    memset(len, 0, sizeof len);
    for (int i = 0; i < ncode; i++) len[ORDER[i]] = (uint8_t)bits(z, 3);
    if (z->bad || !build(&a, len, 19)) { z->bad = true; return; }
    for (int i = 0; i < nlen + ndist;) {
        int s = decode(z, &a);
        if (z->bad) return;
        if (s < 16) { len[i++] = (uint8_t)s; continue; }
        int prev = 0, rep;
        if (s == 16) {
            if (!i) { z->bad = true; return; }
            prev = len[i - 1];
            rep = 3 + bits(z, 2);
        } else if (s == 17) rep = 3 + bits(z, 3);
        else rep = 11 + bits(z, 7);
        if (z->bad || i + rep > nlen + ndist) { z->bad = true; return; }
        while (rep--) len[i++] = (uint8_t)prev;
    }
    if (!len[256] || !build(&a, len, nlen) || !build(&b, len + nlen, ndist)) { z->bad = true; return; }
    codes(z, &a, &b);
}

static void fixed(Z *z)
{
    uint8_t len[288 + 30];
    Huff a, b;
    for (int i = 0; i < 288; i++) len[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
    for (int i = 0; i < 30; i++) len[288 + i] = 5;
    build(&a, len, 288);
    build(&b, len + 288, 30);
    codes(z, &a, &b);
}

static uint32_t adler(const uint8_t *p, int n)
{
    uint32_t a = 1, b = 0;
    for (int i = 0; i < n; i++) { a = (a + p[i]) % 65521; b = (b + a) % 65521; }
    return b << 16 | a;
}

int zlib_inflate(const uint8_t *in, int n, uint8_t *out, int cap)
{
    if (n < 6 || (in[0] & 15) != 8 || ((in[0] << 8 | in[1]) % 31) || (in[1] & 0x20)) return -1;
    Z z = { in + 2, n - 6, 0, 0, 0, out, cap, 0, false };
    int last;
    do {
        last = bits(&z, 1);
        int type = bits(&z, 2);
        if (z.bad) return -1;
        if (type == 0) {
            z.bits = 0; z.nbits = 0; /* to the next whole byte */
            if (z.n - z.pos < 4) return -1;
            int len = z.in[z.pos] | z.in[z.pos + 1] << 8, inv = z.in[z.pos + 2] | z.in[z.pos + 3] << 8;
            z.pos += 4;
            if ((len ^ inv) != 0xFFFF || len > z.n - z.pos || len > z.cap - z.len) return -1;
            memcpy(z.out + z.len, z.in + z.pos, (size_t)len);
            z.len += len; z.pos += len;
        } else if (type == 1) fixed(&z);
        else if (type == 2) dynamic(&z);
        else return -1;
        if (z.bad) return -1;
    } while (!last);
    const uint8_t *t = in + n - 4;
    if (adler(out, z.len) != ((uint32_t)t[0] << 24 | (uint32_t)t[1] << 16 | (uint32_t)t[2] << 8 | t[3])) return -1;
    return z.len;
}

int zlib_store(const uint8_t *in, int n, uint8_t *out, int cap)
{
    if (n < 0 || n > 65535 || cap < n + 11) return -1;
    uint32_t a = adler(in, n);
    out[0] = 0x78; out[1] = 0x9C;
    out[2] = 1; /* last block, stored */
    out[3] = (uint8_t)n; out[4] = (uint8_t)(n >> 8);
    out[5] = (uint8_t)~n; out[6] = (uint8_t)(~n >> 8);
    memcpy(out + 7, in, (size_t)n);
    out[7 + n] = (uint8_t)(a >> 24); out[8 + n] = (uint8_t)(a >> 16); out[9 + n] = (uint8_t)(a >> 8); out[10 + n] = (uint8_t)a;
    return n + 11;
}
