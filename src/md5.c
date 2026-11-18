/* md5.c - see md5.h. MD5 after RFC 1321, HMAC after RFC 2104. By Mhdshehri. */
#include "md5.h"

static const uint8_t SHIFT[64] = { 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20,
    5, 9, 14, 20, 5, 9, 14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };
static const uint32_t K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391 };

typedef struct { uint32_t h[4]; uint8_t buf[64]; size_t fill; uint64_t total; } Md5;

static void block(Md5 *m, const uint8_t *p)
{
    uint32_t w[16], a = m->h[0], b = m->h[1], c = m->h[2], d = m->h[3];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] | (uint32_t)p[4 * i + 1] << 8 | (uint32_t)p[4 * i + 2] << 16 | (uint32_t)p[4 * i + 3] << 24;
    for (int i = 0; i < 64; i++) {
        uint32_t f;
        int g;
        if (i < 16) { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) & 15; }
        else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) & 15; }
        else { f = c ^ (b | ~d); g = (7 * i) & 15; }
        f += a + K[i] + w[g];
        a = d; d = c; c = b;
        b += f << SHIFT[i] | f >> (32 - SHIFT[i]);
    }
    m->h[0] += a; m->h[1] += b; m->h[2] += c; m->h[3] += d;
}

static void start(Md5 *m)
{
    m->h[0] = 0x67452301; m->h[1] = 0xefcdab89; m->h[2] = 0x98badcfe; m->h[3] = 0x10325476;
    m->fill = 0; m->total = 0;
}

static void add(Md5 *m, const uint8_t *p, size_t n)
{
    m->total += n;
    while (n) {
        size_t k = 64 - m->fill;
        if (k > n) k = n;
        memcpy(m->buf + m->fill, p, k);
        m->fill += k; p += k; n -= k;
        if (m->fill == 64) { block(m, m->buf); m->fill = 0; }
    }
}

static void finish(Md5 *m, uint8_t out[16])
{
    uint64_t bits = m->total * 8;
    uint8_t pad[72] = { 0x80 }, len[8];
    for (int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (8 * i));
    add(m, pad, (m->fill < 56 ? 56 : 120) - m->fill);
    add(m, len, 8);
    for (int i = 0; i < 16; i++) out[i] = (uint8_t)(m->h[i / 4] >> (8 * (i % 4)));
}

void md5(const uint8_t *data, size_t n, uint8_t out[16])
{
    Md5 m;
    start(&m);
    add(&m, data, n);
    finish(&m, out);
}

void hmac_md5(const uint8_t *key, size_t klen, const uint8_t *data, size_t n, uint8_t out[16])
{
    uint8_t k[64] = { 0 }, pad[64], inner[16];
    if (klen > 64) md5(key, klen, k);
    else memcpy(k, key, klen);
    Md5 m;
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    start(&m); add(&m, pad, 64); add(&m, data, n); finish(&m, inner);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5c;
    start(&m); add(&m, pad, 64); add(&m, inner, 16); finish(&m, out);
}

void rc4(const uint8_t *key, size_t klen, uint8_t *data, size_t n)
{
    uint8_t s[256], i = 0, j = 0, t;
    for (int k = 0; k < 256; k++) s[k] = (uint8_t)k;
    for (int k = 0; k < 256; k++) {
        j = (uint8_t)(j + s[k] + key[k % klen]);
        t = s[k]; s[k] = s[j]; s[j] = t;
    }
    j = 0;
    for (size_t k = 0; k < n; k++) {
        i++;
        j = (uint8_t)(j + s[i]);
        t = s[i]; s[i] = s[j]; s[j] = t;
        data[k] ^= s[(uint8_t)(s[i] + s[j])];
    }
}
