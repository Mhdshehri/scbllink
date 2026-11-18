/* srv.h - SCBLLink's own game server: what Splinter Cell: Blacklist expects to find
 * behind "onlineconfigservice.ubi.com". Written from recordings of the game's own
 * traffic (docs/server-protocol.md). No sockets in here: the host program hands in
 * every UDP packet and sends what comes back, so the same code runs in tests.
 * By Mhdshehri. */
#pragma once
#include "mini.h"

enum { SRV_AUTH = 0, SRV_SECURE = 1 };          /* the two UDP ports */
#define SRV_PORT_AUTH 21126
#define SRV_PORT_SECURE 21127
#define SRV_PORT_CONFIG 80
#define SRV_PORT_FILES 8000

typedef struct {
    uint32_t ip;                                  /* the address games are told to use (network order) */
    void (*send)(int which, uint32_t ip, uint16_t port, const uint8_t *d, int n);
    void (*log)(const char *line);
    void (*random)(uint8_t *out, int n);
    bool (*login)(const char *name, const char *password); /* a lobby member's game account? */
    bool (*ubi_name)(const char *ubi, char name[64]);     /* game account id -> account name (lobby members) */
    uint64_t (*unix_time)(void);
    bool verbose;                                 /* log every call */
} SrvHost;

void srv_start(const SrvHost *h);
void srv_stop(void);
void srv_packet(int which, uint32_t ip, uint16_t port, const uint8_t *d, int n, uint64_t now_ms);
void srv_tick(uint64_t now_ms);
/* `from` invited `to` (account names): the invited game's search finds `from`'s match. */
void srv_invite(const char *from_name, const char *to_name, uint64_t now_ms);

/* The two small web answers. Each returns the body's length (or -1: not found) and sets *type. */
int srv_http_get(int port, const char *path, uint8_t *out, int cap);
