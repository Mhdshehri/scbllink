#include "../src/pkt.h"
#include <assert.h>
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int mk(uint8_t *p, uint8_t proto, uint32_t s, uint32_t d, uint16_t sp, uint16_t dp, int paylen, uint16_t id, uint16_t fo)
{
    int hl = 20, l4 = proto == 6 ? 20 : proto == 17 ? 8 : 8;
    int total = hl + l4 + paylen;
    memset(p, 0, (size_t)total);
    p[0] = 0x45; p[2] = (uint8_t)(total >> 8); p[3] = (uint8_t)total;
    p[4] = (uint8_t)(id >> 8); p[5] = (uint8_t)id; p[6] = (uint8_t)(fo >> 8); p[7] = (uint8_t)fo;
    p[8] = 64; p[9] = proto; memcpy(p + 12, &s, 4); memcpy(p + 16, &d, 4);
    if (proto == 1) { p[20] = (uint8_t)sp; }
    else { p[20] = (uint8_t)(sp >> 8); p[21] = (uint8_t)sp; p[22] = (uint8_t)(dp >> 8); p[23] = (uint8_t)dp; }
    return total;
}

/* a full checksum, the slow way, to check pkt_readdress's quick one against */
static uint16_t full_sum(const uint8_t *p, int n, uint32_t sum)
{
    for (int i = 0; i + 1 < n; i += 2) sum += (uint32_t)(p[i] << 8 | p[i + 1]);
    if (n & 1) sum += (uint32_t)p[n - 1] << 8;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}
static uint16_t l4_sum(const uint8_t *p, int n)
{
    int l4 = n - 20;
    uint32_t ps = 0;
    for (int i = 12; i < 20; i += 2) ps += (uint32_t)(p[i] << 8 | p[i + 1]);
    ps += p[9];
    ps += (uint32_t)l4;
    return full_sum(p + 20, l4, ps);
}
static void seal(uint8_t *p, int n)
{
    int at = p[9] == 17 ? 26 : 36;
    for (int i = 28; i < n; i++) p[i] ^= (uint8_t)(i * 7 + 3); /* some payload */
    p[at] = p[at + 1] = 0;
    uint16_t c = l4_sum(p, n);
    if (p[9] == 17 && c == 0) c = 0xffff;
    p[at] = (uint8_t)(c >> 8); p[at + 1] = (uint8_t)c;
    p[10] = p[11] = 0;
    uint16_t h = full_sum(p, 20, 0);
    p[10] = (uint8_t)(h >> 8); p[11] = (uint8_t)h;
}

static void readdress_tests(void)
{
    uint8_t p[2000];
    uint32_t A = vip(1), S = vip(200), C = vip(250);
    for (int proto = 6; proto <= 17; proto += 11) {
        for (int len = 0; len < 70; len += 23) {
            int n = mk(p, (uint8_t)proto, A, S, 51000, 21126, len, 9, 0);
            seal(p, n);
            CHECK(l4_sum(p, n) == 0 && full_sum(p, 20, 0) == 0);   /* the test's own packet is right */
            CHECK(pkt_readdress(p, n, C, A));
            uint32_t s2, d2;
            memcpy(&s2, p + 12, 4); memcpy(&d2, p + 16, 4);
            CHECK(s2 == C && d2 == A);
            CHECK(full_sum(p, 20, 0) == 0);                          /* IPv4 header checksum */
            CHECK(l4_sum(p, n) == 0);                                /* UDP/TCP checksum still right */
            CHECK(p[20] == (51000 >> 8) && p[23] == (21126 & 0xff)); /* ports untouched */
        }
    }
    /* UDP without a checksum stays without */
    int n = mk(p, 17, A, S, 1, 21127, 10, 9, 0);
    CHECK(pkt_readdress(p, n, C, A) && p[26] == 0 && p[27] == 0 && full_sum(p, 20, 0) == 0);
    /* a later piece of a split packet: only the header */
    n = mk(p, 17, A, S, 0, 0, 100, 9, 185);
    p[30] = 0xAB; p[31] = 0xCD;
    CHECK(pkt_readdress(p, n, C, A) && p[30] == 0xAB && p[31] == 0xCD && full_sum(p, 20, 0) == 0);
    /* not a packet */
    CHECK(!pkt_readdress(p, 10, C, A));
}

int main(void)
{
    readdress_tests();
    uint8_t syn[16];
    /* known SYN from a capture: session 0 -> checksum 0x40 */
    CHECK(quazal_syn(syn, 0) == 15);
    static const uint8_t ref[15] = { 0x3f, 0x31, 0x20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x40 };
    CHECK(memcmp(syn, ref, 15) == 0);
    /* other captured packets: checksum is the last byte */
    static const uint8_t a[] = "\x3e\x31\x31\x96\x60\x30\x0d\xd5\x01\x00\x37\x00\xba\xd2\x1c";
    CHECK(quazal_checksum(a, 14) == 0x1c);
    static const uint8_t b[] = "\x3f\x31\x32\x7c\x60\x30\x0d\xd5\x02\x00\x00\x0f\x93\x44\xdb\x13\x75\xe2\x50\x05\xa2"
                               "\x60\xfd\x2a\x16\xfb\xb1\xab\x24\x87\x96\xfc\x3f\xcc\x7b\x5a\x7f";
    CHECK(quazal_checksum(b, (int)sizeof b - 2) == 0x7f);
    /* a SYN-ACK built the same way: type 0, flags Ack|HasSize */
    uint8_t sa[17] = { 0x31, 0x3f, 0x00 | (9 << 3), 0x11, 0,0,0,0, 0,0, 0x78,0x56,0x34,0x12, 0,0 };
    sa[16] = quazal_checksum(sa, 16);
    CHECK(quazal_is_syn_ack(sa, 17));
    sa[16]++;
    CHECK(!quazal_is_syn_ack(sa, 17));
    CHECK(!quazal_is_syn_ack(syn, 15)); /* our own SYN has no Ack flag */

    uint32_t H = vip(1), J = vip(2);
    char t[16]; vip_text(J, t); CHECK(!strcmp(t, "10.237.51.2"));
    CHECK(vip_in_net(H) && vip_in_net(J) && !vip_in_net(vip(255)) && !vip_in_net(vip(0)));
    uint8_t p[2000]; Ip4 ip; int n;

    n = mk(p, 17, J, H, 3074, 21126, 40, 1, 0);
    CHECK(ip4_parse(p, n, &ip) && ip.has_ports && ip.sport == 3074 && ip.dport == 21126);
    CHECK(pkt_verdict(&ip, H, 100) == PKT_RELIABLE);
    n = mk(p, 17, H, J, 21127, 3074, 40, 2, 0); ip4_parse(p, n, &ip);
    CHECK(pkt_verdict(&ip, H, 100) == PKT_RELIABLE);
    n = mk(p, 17, J, H, 13000, 13000, 900, 3, 0); ip4_parse(p, n, &ip);
    CHECK(pkt_verdict(&ip, H, 100) == PKT_FAST);
    n = mk(p, 17, J, H, 13031, 13017, 300, 30, 0); ip4_parse(p, n, &ip); /* rest of the P2P range */
    CHECK(pkt_verdict(&ip, H, 100) == PKT_FAST);
    n = mk(p, 17, J, H, 13032, 12999, 300, 31, 0); ip4_parse(p, n, &ip); /* just outside it */
    CHECK(pkt_verdict(&ip, H, 100) == PKT_DROP);
    n = mk(p, 17, J, H, 5353, 5353, 40, 4, 0); ip4_parse(p, n, &ip);   /* mDNS chatter */
    CHECK(pkt_verdict(&ip, H, 100) == PKT_DROP);
    n = mk(p, 17, J, H, 137, 137, 40, 5, 0); ip4_parse(p, n, &ip);     /* NetBIOS */
    CHECK(pkt_verdict(&ip, H, 100) == PKT_DROP);
    n = mk(p, 6, J, H, 50000, 80, 0, 6, 0); ip4_parse(p, n, &ip);
    CHECK(pkt_verdict(&ip, H, 100) == PKT_FAST);
    n = mk(p, 6, H, J, 80, 50000, 0, 7, 0); ip4_parse(p, n, &ip);
    CHECK(pkt_verdict(&ip, H, 100) == PKT_FAST);
    n = mk(p, 6, H, J, 50000, 80, 0, 8, 0); ip4_parse(p, n, &ip);      /* host reaching into the friend's port 80 */
    CHECK(pkt_verdict(&ip, H, 100) == PKT_DROP);
    n = mk(p, 6, J, H, 50000, 445, 0, 9, 0); ip4_parse(p, n, &ip);     /* file sharing */
    CHECK(pkt_verdict(&ip, H, 100) == PKT_DROP);
    n = mk(p, 6, J, H, 50000, 3389, 0, 10, 0); ip4_parse(p, n, &ip);   /* remote desktop */
    CHECK(pkt_verdict(&ip, H, 100) == PKT_DROP);
    n = mk(p, 1, J, H, 8, 0, 32, 11, 0); ip4_parse(p, n, &ip);         /* ping */
    CHECK(pkt_verdict(&ip, H, 100) == PKT_FAST);
    n = mk(p, 1, J, H, 13, 0, 32, 12, 0); ip4_parse(p, n, &ip);        /* ICMP timestamp */
    CHECK(pkt_verdict(&ip, H, 100) == PKT_DROP);
    /* split packet: first piece allowed, then the rest */
    n = mk(p, 17, J, H, 13000, 13000, 600, 77, 0x2000); ip4_parse(p, n, &ip);
    CHECK(ip.fragment && pkt_verdict(&ip, H, 200) == PKT_FAST);
    n = mk(p, 17, J, H, 0, 0, 600, 77, 75); ip4_parse(p, n, &ip);      /* offset 600 bytes, no ports */
    CHECK(ip.fragment && !ip.has_ports && pkt_verdict(&ip, H, 300) == PKT_FAST);
    CHECK(pkt_verdict(&ip, H, 9000) == PKT_DROP);                       /* too late */
    n = mk(p, 17, J, H, 0, 0, 600, 78, 75); ip4_parse(p, n, &ip);      /* rest of something never allowed */
    CHECK(pkt_verdict(&ip, H, 300) == PKT_DROP);
    /* broken packets */
    n = mk(p, 17, J, H, 13000, 13000, 40, 1, 0);
    CHECK(!ip4_parse(p, n - 1, &ip));     /* shorter than its length field */
    p[0] = 0x65; CHECK(!ip4_parse(p, n, &ip));
    p[0] = 0x44; CHECK(!ip4_parse(p, n, &ip));  /* header length 16 */
    CHECK(!ip4_parse(p, 10, &ip));
    printf(fails ? "%d FAILED\n" : "all pkt tests passed\n", fails);
    return fails != 0;
}
