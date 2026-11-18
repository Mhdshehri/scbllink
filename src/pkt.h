/* pkt.h - looking at the IPv4 packets that cross the SCBLLink adapter: which
 * ones may go to the other PC, and on which Steam channel. Plain C, no OS
 * calls, so the Linux test build checks it too. Addresses are in network
 * byte order (as they sit in the packet). */
#pragma once
#include "mini.h"

/* the virtual network: 10.237.51.0/24, the host is .1, friends .2 to .4 */
#define NET_A 10
#define NET_B 237
#define NET_C 51
#define HOST_LAST 1

uint32_t vip(int last);              /* 10.237.51.<last> */
bool vip_in_net(uint32_t ip);        /* 10.237.51.1 - .254 */
int vip_last(uint32_t ip);
void vip_text(uint32_t ip, char out[16]);

typedef struct {
    uint32_t src, dst;
    uint8_t proto;          /* 1 ICMP, 6 TCP, 17 UDP */
    uint16_t id;
    bool fragment;          /* part of a split packet */
    bool first_fragment;    /* fragment offset 0 (has the ports) */
    bool has_ports;
    uint16_t sport, dport;
    uint8_t icmp_type;
} Ip4;

/* false if it isn't a complete, sane IPv4 packet */
bool ip4_parse(const uint8_t *p, int n, Ip4 *out);

/* Gives a packet new addresses (developer build: this PC's own game talks to its
   server through the adapter, so that can be recorded). Fixes the IPv4 header
   checksum and, in the piece that carries it, the UDP/TCP checksum. */
bool pkt_readdress(uint8_t *p, int n, uint32_t new_src, uint32_t new_dst);

enum { PKT_DROP = 0, PKT_FAST = 1, PKT_RELIABLE = 2 };

/* May this packet cross between the PCs? Only the game's and its server's
   ports, and ping (for the link test). Pieces of a split packet are only let
   through after their first piece was. Returns PKT_DROP or the channel. */
int pkt_verdict(const Ip4 *ip, uint32_t host_ip, u64 now);

/* ---- Quazal (the game's online protocol), for the "is the host's server
   up?" check: a connection request (SYN) and whether an answer is its ACK. */
int quazal_syn(uint8_t out[16], uint8_t session);
bool quazal_is_syn_ack(const uint8_t *p, int n);
uint8_t quazal_checksum(const uint8_t *p, int n); /* over p[0..n) */
