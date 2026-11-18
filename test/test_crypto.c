/* test_crypto.c - md5, hmac, rc4, inflate against known answers. */
#include "../src/md5.h"
#include "../src/inflate.h"
#include <zlib.h>
static int fails;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); fails++; } } while (0)
static void hex(const uint8_t *d, int n, char *o) { for (int i = 0; i < n; i++) sprintf(o + 2 * i, "%02x", d[i]); }
int main(void)
{
    uint8_t d[16]; char h[33];
    md5((const uint8_t *)"", 0, d); hex(d, 16, h); CHECK(!strcmp(h, "d41d8cd98f00b204e9800998ecf8427e"));
    md5((const uint8_t *)"The quick brown fox jumps over the lazy dog", 43, d); hex(d, 16, h); CHECK(!strcmp(h, "9e107d9d372bb6826bd81d3542a419d6"));
    static uint8_t big[1000]; memset(big, 'a', 1000);
    md5(big, 1000, d); hex(d, 16, h); CHECK(!strcmp(h, "cabe45dcc9ae5b66ba86600cca6b8ba8"));
    hmac_md5((const uint8_t *)"key", 3, (const uint8_t *)"The quick brown fox jumps over the lazy dog", 43, d); hex(d, 16, h);
    CHECK(!strcmp(h, "80070713463e7749b90c2dc24911e275"));
    uint8_t p[9] = "Plaintext"; rc4((const uint8_t *)"Key", 3, p, 9); hex(p, 9, h); CHECK(!strcmp(h, "bbf316e8d940af0ad3"));
    /* inflate: against zlib at every level, random and repetitive data */
    static uint8_t src[70000], z[80000], out[70000];
    unsigned seed = 1;
    for (int round = 0; round < 60; round++) {
        int n = round * 1171 % 65000 + 1;
        for (int i = 0; i < n; i++) { seed = seed * 1103515245 + 12345; src[i] = round % 3 == 0 ? (uint8_t)(seed >> 16) : (uint8_t)("abcabcabd"[i % 9] + ((seed >> 20) % (round % 5 + 1))); }
        uLongf zn = sizeof z;
        CHECK(compress2(z, &zn, src, (uLong)n, round % 10) == Z_OK);
        int got = zlib_inflate(z, (int)zn, out, sizeof out);
        CHECK(got == n && !memcmp(out, src, (size_t)n));
        CHECK(zlib_inflate(z, (int)zn, out, n - 1) == -1);             /* too small a buffer */
        z[zn / 2] ^= 0x55; int r = zlib_inflate(z, (int)zn, out, sizeof out); CHECK(r == -1 || r >= 0); /* no crash on damage */
        for (int cut = 0; cut < (int)zn; cut += (int)zn / 7 + 1) zlib_inflate(z, cut, out, sizeof out);
        int sn = zlib_store(src, n, z, sizeof z);
        uLongf on = sizeof out;
        CHECK(sn == n + 11 && uncompress(out, &on, z, (uLong)sn) == Z_OK && (int)on == n && !memcmp(out, src, (size_t)n));
    }
    printf(fails ? "test_crypto: %d FAILED\n" : "test_crypto: ok\n", fails);
    return fails != 0;
}
