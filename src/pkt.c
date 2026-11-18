/* pkt.c - see pkt.h. */
#include "pkt.h"

uint32_t vip(int last)
{
    uint8_t x[4] = { NET_A, NET_B, NET_C, (uint8_t)last };
    uint32_t v;
    memcpy(&v, x, 4);
    return v;
}

bool vip_in_net(uint32_t ip)
{
    const uint8_t *b = (const uint8_t *)&ip;
    return b[0] == NET_A && b[1] == NET_B && b[2] == NET_C && b[3] != 0 && b[3] != 255;
}

int vip_last(uint32_t ip) { return ((const uint8_t *)&ip)[3]; }

void vip_text(uint32_t ip, char out[16])
{
    const uint8_t *b = (const uint8_t *)&ip;
    sfmt(out, 16, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
}

static uint16_t be16at(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

bool ip4_parse(const uint8_t *p, int n, Ip4 *o)
{
    memset(o, 0, sizeof *o);
    if (n < 20 || (p[0] >> 4) != 4) return false;
    int hl = (p[0] & 15) * 4;
    int total = be16at(p + 2);
    if (hl < 20 || total < hl || total > n) return false;
    o->proto = p[9];
    o->id = be16at(p + 4);
    uint16_t fo = be16at(p + 6);
    bool more = (fo & 0x2000) != 0;
    int offset = fo & 0x1FFF;
    o->fragment = more || offset != 0;
    o->first_fragment = offset == 0;
    memcpy(&o->src, p + 12, 4);
    memcpy(&o->dst, p + 16, 4);
    const uint8_t *l4 = p + hl;
    int l4n = total - hl;
    if (offset == 0) {
        if ((o->proto == 6 && l4n >= 20) || (o->proto == 17 && l4n >= 8)) {
            o->has_ports = true;
            o->sport = be16at(l4);
            o->dport = be16at(l4 + 2);
        } else if (o->proto == 1 && l4n >= 8) {
            o->icmp_type = l4[0];
        } else if (o->proto == 6 || o->proto == 17 || o->proto == 1) {
            return false; /* too short to be real */
        }
    }
    return true;
}

/* UDP: the game's P2P ports (13000-13031: the server hands out
   NC_MAIN_PORT 13000 with NC_MAIN_PORT_RANGE 32), its own Quazal port (3074) and the host's
   two Quazal servers (21126 login, 21127 secure). TCP, host only: the config
   server (80) and the content server (8000). */
static const uint16_t UDP_PORTS[] = { 3074, 21126, 21127 };
#define P2P_FIRST 13000
#define P2P_LAST 13031
static const uint16_t TCP_HOST_PORTS[] = { 80, 8000 };

static bool in(const uint16_t *list, int n, uint16_t v)
{
    for (int i = 0; i < n; i++) if (list[i] == v) return true;
    return false;
}
#define IN(list, v) in(list, (int)(sizeof list / sizeof list[0]), v)
static bool udp_ok(uint16_t v) { return (v >= P2P_FIRST && v <= P2P_LAST) || IN(UDP_PORTS, v); }

/* first pieces of split packets that were let through: the later pieces
   (which carry no ports) may follow for a few seconds */
typedef struct { uint32_t src, dst; uint16_t id; uint8_t proto, verdict; u64 at; } Frag;
static Frag g_frags[32];

static int ports_verdict(const Ip4 *ip, uint32_t host_ip)
{
    if (ip->proto == 1) return (ip->icmp_type == 8 || ip->icmp_type == 0) ? PKT_FAST : PKT_DROP;
    if (!ip->has_ports) return PKT_DROP;
    if (ip->proto == 17) {
        if (!udp_ok(ip->sport) && !udp_ok(ip->dport)) return PKT_DROP;
        /* The Quazal servers never resend a lost answer, so their traffic goes
           on Steam's reliable channel. Game P2P traffic stays fast. */
        bool quazal = ip->sport == 21126 || ip->dport == 21126 || ip->sport == 21127 || ip->dport == 21127;
        return quazal ? PKT_RELIABLE : PKT_FAST;
    }
    if (ip->proto == 6) {
        bool to_host = ip->dst == host_ip && IN(TCP_HOST_PORTS, ip->dport);
        bool from_host = ip->src == host_ip && IN(TCP_HOST_PORTS, ip->sport);
        return (to_host || from_host) ? PKT_FAST : PKT_DROP; /* TCP resends by itself */
    }
    return PKT_DROP;
}

int pkt_verdict(const Ip4 *ip, uint32_t host_ip, u64 now)
{
    if (!ip->fragment) return ports_verdict(ip, host_ip);
    if (ip->first_fragment) {
        int v = ports_verdict(ip, host_ip);
        if (v != PKT_DROP) {
            int slot = 0;
            for (int i = 1; i < 32; i++) if (g_frags[i].at < g_frags[slot].at) slot = i;
            g_frags[slot] = (Frag){ ip->src, ip->dst, ip->id, ip->proto, (uint8_t)v, now ? now : 1 };
        }
        return v;
    }
    for (int i = 0; i < 32; i++) {
        Frag *f = &g_frags[i];
        if (f->at && now - f->at < 5000 && f->src == ip->src && f->dst == ip->dst && f->id == ip->id && f->proto == ip->proto)
            return f->verdict;
    }
    return PKT_DROP;
}

/* ---- Quazal PRUDP ----
   Checksum (1 byte): add the packet up as little-endian 32-bit words, add the
   four bytes of that sum together, add the bytes left over at the end, and add
   the low byte of the sum of the game's access key's characters (only for the
   secure stream type, which is the one used here). */
static const char ACCESS_KEY[] = "yl4NG7qZ"; /* Splinter Cell: Blacklist, PC */

uint8_t quazal_checksum(const uint8_t *p, int n)
{
    uint32_t words = 0;
    int full = n - n % 4;
    for (int i = 0; i < full; i += 4)
        words += (uint32_t)p[i] | (uint32_t)p[i + 1] << 8 | (uint32_t)p[i + 2] << 16 | (uint32_t)p[i + 3] << 24;
    uint8_t sum = (uint8_t)(words + (words >> 8) + (words >> 16) + (words >> 24));
    for (int i = full; i < n; i++) sum = (uint8_t)(sum + p[i]);
    uint32_t key = 0;
    for (int i = 0; ACCESS_KEY[i]; i++) key += (uint8_t)ACCESS_KEY[i];
    return (uint8_t)(sum + (uint8_t)key);
}

/* A SYN like the game's first packet: from port 15 to port 1, both on the
   secure stream type (3), "needs ack" flag, no payload. */
int quazal_syn(uint8_t out[16], uint8_t session)
{
    uint8_t p[15] = {
        0x3F, 0x31,       /* source 15, destination 1, stream type 3 */
        0x00 | (4 << 3),  /* type SYN, flag NeedAck */
        session,
        0, 0, 0, 0,       /* signature */
        0, 0,             /* sequence */
        0, 0, 0, 0,       /* connection signature */
    };
    p[14] = quazal_checksum(p, 14);
    memcpy(out, p, 15);
    return 15;
}

bool quazal_is_syn_ack(const uint8_t *p, int n)
{
    if (n < 15) return false;
    int type = p[2] & 7, flags = p[2] >> 3;
    return type == 0 && (flags & 1) && quazal_checksum(p, n - 1) == p[n - 1];
}

/* ---------- new addresses for a packet (developer build) ---------- */

static uint32_t sum16(const uint8_t *p, int n, uint32_t sum)
{
    for (int i = 0; i + 1 < n; i += 2) sum += (uint32_t)(p[i] << 8 | p[i + 1]);
    if (n & 1) sum += (uint32_t)p[n - 1] << 8;
    return sum;
}

static uint16_t fold(uint32_t sum)
{
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)sum;
}

bool pkt_readdress(uint8_t *p, int n, uint32_t new_src, uint32_t new_dst)
{
    Ip4 ip;
    if (!ip4_parse(p, n, &ip)) return false;
    int ihl = (p[0] & 0x0f) * 4;
    uint8_t old[8];
    memcpy(old, p + 12, 8);
    memcpy(p + 12, &new_src, 4);
    memcpy(p + 16, &new_dst, 4);
    /* the IPv4 header's own checksum: again, in full */
    p[10] = p[11] = 0;
    uint16_t h = (uint16_t)~fold(sum16(p, ihl, 0));
    p[10] = (uint8_t)(h >> 8);
    p[11] = (uint8_t)h;
    /* UDP/TCP checksums cover the addresses too. Only the first piece of a split packet
       has that checksum; it's corrected by the difference (RFC 1624), so the rest of the
       data needn't be here. */
    if (((p[6] & 0x1f) << 8 | p[7]) != 0) return true; /* a later piece */
    int at = ip.proto == 17 ? ihl + 6 : ip.proto == 6 ? ihl + 16 : -1;
    if (at < 0) return true;
    if (at + 2 > n) return false;
    uint16_t c = (uint16_t)(p[at] << 8 | p[at + 1]);
    if (ip.proto == 17 && c == 0) return true; /* UDP without a checksum */
    uint32_t sum = (uint16_t)~c;
    for (int i = 0; i < 8; i += 2) sum += (uint16_t)~(old[i] << 8 | old[i + 1]);
    sum = sum16(p + 12, 8, sum);
    c = (uint16_t)~fold(sum);
    if (ip.proto == 17 && c == 0) c = 0xffff; /* UDP: 0 would mean "none" */
    p[at] = (uint8_t)(c >> 8);
    p[at + 1] = (uint8_t)c;
    return true;
}
