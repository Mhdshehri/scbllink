/* ownsrv.c - see ownsrv.h. */
#include "ownsrv.h"
#include "srv.h"
#include "plat.h"
#include "sys.h"

#ifdef _WIN32
long long _time64(long long *t);
#define unix_now() ((uint64_t)_time64(0))
#else
#include <time.h>
#define unix_now() ((uint64_t)time(0))
#endif

#define MAX_WEB 8
#define WEB_HEAD 2048
#define WEB_BODY_MAX (8u << 20) /* the game's own upload is about 80 KB */
#define WEB_MS 15000

typedef struct {
    sock_t s;
    int port, got;
    char head[WEB_HEAD];
    uint32_t body_left;
    bool body, done;
    u64 since;
    char out[1400];
    int out_n, out_sent;
} Web;

static bool g_run;
static sock_t g_udp[2] = { BADSOCK, BADSOCK }, g_tcp[2] = { BADSOCK, BADSOCK };
static const int TCP_PORT[2] = { SRV_PORT_CONFIG, SRV_PORT_FILES };
static Web g_web[MAX_WEB];
static void (*g_log)(const char *);

static void out(int which, uint32_t ip, uint16_t port, const uint8_t *d, int n) { if (g_udp[which] != BADSOCK) udp_sendto(g_udp[which], d, n, ip, port); }
static void rnd(uint8_t *o, int n) { if (!random_bytes(o, n)) memset(o, 0x5A, (size_t)n); }
static uint64_t unix_time(void) { return unix_now(); }

bool own_running(void) { return g_run; }

void own_stop(void)
{
    for (int i = 0; i < 2; i++) {
        if (g_udp[i] != BADSOCK) sock_close(g_udp[i]);
        if (g_tcp[i] != BADSOCK) sock_close(g_tcp[i]);
        g_udp[i] = g_tcp[i] = BADSOCK;
    }
    for (int i = 0; i < MAX_WEB; i++)
        if (g_web[i].s) { sock_close(g_web[i].s); g_web[i].s = 0; }
    srv_stop();
    g_run = false;
}

bool own_start(uint32_t bind_ip, uint32_t public_ip, bool (*login)(const char *, const char *), bool (*ubi_name)(const char *, char[64]),
               void (*log)(const char *), bool verbose)
{
    own_stop();
    g_log = log;
    g_udp[0] = udp_open(bind_ip, SRV_PORT_AUTH, 0);
    g_udp[1] = udp_open(bind_ip, SRV_PORT_SECURE, 0);
    g_tcp[0] = tcp_listen(bind_ip, SRV_PORT_CONFIG);
    g_tcp[1] = tcp_listen(bind_ip, SRV_PORT_FILES);
    if (g_udp[0] == BADSOCK || g_udp[1] == BADSOCK || g_tcp[0] == BADSOCK || g_tcp[1] == BADSOCK) {
        if (log) log("! SCBLLink's server could not open its ports (80, 8000, 21126, 21127) on the SCBLLink address: is another server running?");
        own_stop();
        return false;
    }
    SrvHost h = { .ip = public_ip, .send = out, .log = log, .random = rnd, .login = login, .ubi_name = ubi_name, .unix_time = unix_time, .verbose = verbose };
    srv_start(&h);
    g_run = true;
    return true;
}

static void web_answer(Web *w, const char *status, const uint8_t *body, int n)
{
    sfmt(w->out, sizeof w->out, "HTTP/1.0 %s\r\nContent-Type: application/octet-stream\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", status, n);
    w->out_n = (int)strlen(w->out);
    if (n > 0 && n <= (int)sizeof w->out - w->out_n) { memcpy(w->out + w->out_n, body, (size_t)n); w->out_n += n; }
    w->done = true;
}

/* the whole request head is in w->head */
static void web_request(Web *w)
{
    char *sp = strchr(w->head, ' ');
    char *path = sp ? sp + 1 : 0, *end = path ? strchr(path, ' ') : 0;
    if (!end) { web_answer(w, "400 Bad Request", 0, 0); return; }
    bool get = !strncmp(w->head, "GET ", 4), put = !strncmp(w->head, "PUT ", 4);
    *end = 0;
    if (get) {
        static uint8_t body[1200];
        char *q = strchr(path, '?');
        if (q && w->port != SRV_PORT_CONFIG) *q = 0;
        int n = srv_http_get(w->port, path, body, sizeof body);
        if (n < 0) web_answer(w, "404 Not Found", 0, 0);
        else web_answer(w, "200 OK", body, n);
        return;
    }
    if (put && w->port == SRV_PORT_FILES && !strncmp(path, "/ugc/", 5)) {
        /* the game's own file: taken and dropped for now (nothing reads it back yet) */
        uint32_t len = 0;
        for (char *l = end + 1; (l = strchr(l, '\n')) != 0;) {
            l++;
            if ((l[0] == 'C' || l[0] == 'c') && !strncmp(l + 1, "ontent-", 7) && (l[8] == 'L' || l[8] == 'l') && !strncmp(l + 9, "ength:", 6)) {
                const char *d = l + 15;
                while (*d == ' ') d++;
                while (*d >= '0' && *d <= '9' && len < WEB_BODY_MAX) len = len * 10 + (uint32_t)(*d++ - '0');
            }
        }
        if (len > WEB_BODY_MAX) { web_answer(w, "413 Too Large", 0, 0); return; }
        static const char go[] = "HTTP/1.1 100 Continue\r\n\r\n";
        tcp_send(w->s, go, sizeof go - 1);
        w->body = true;
        w->body_left = len;
        return;
    }
    web_answer(w, "404 Not Found", 0, 0);
}

static void web_close(Web *w) { sock_close(w->s); memset(w, 0, sizeof *w); }

static void web_poll(Web *w, u64 now)
{
    if (now - w->since > WEB_MS) { web_close(w); return; }
    if (w->done) {
        int r = tcp_send(w->s, w->out + w->out_sent, w->out_n - w->out_sent);
        if (r < 0) { web_close(w); return; }
        w->out_sent += r;
        if (w->out_sent == w->out_n) web_close(w);
        return;
    }
    for (int rounds = 0; rounds < 64; rounds++) {
        if (w->body) {
            static char sink[4096];
            if (!w->body_left) {
                sfmt(w->out, sizeof w->out, "HTTP/1.0 200 OK\r\n\r\n");
                w->out_n = (int)strlen(w->out);
                w->done = true;
                return;
            }
            int r = tcp_recv(w->s, sink, w->body_left < sizeof sink ? (int)w->body_left : (int)sizeof sink);
            if (r < 0) { web_close(w); return; }
            if (!r) return;
            w->body_left -= (uint32_t)r;
            continue;
        }
        /* the head, a byte at a time near its end so nothing of a body is taken with it */
        int r = tcp_recv(w->s, w->head + w->got, 1);
        if (r < 0) { web_close(w); return; }
        if (!r) return;
        w->got += r;
        w->head[w->got] = 0;
        if (w->got >= 4 && !memcmp(w->head + w->got - 4, "\r\n\r\n", 4)) { web_request(w); if (!w->s) return; continue; }
        if (w->got >= WEB_HEAD - 1) { web_close(w); return; }
        rounds = 0; /* the head is short: read it in one go */
    }
}

void own_invite(const char *from_name, const char *to_name)
{
    if (g_run) srv_invite(from_name, to_name, now_ms());
}

void own_poll(void)
{
    if (!g_run) return;
    u64 now = now_ms();
    for (int i = 0; i < 2; i++) {
        for (int k = 0; k < 64; k++) {
            uint8_t b[2048];
            uint32_t ip;
            uint16_t port;
            int n = udp_recvfrom(g_udp[i], b, sizeof b, &ip, &port);
            if (n < 0) break;
            if (n > 0) srv_packet(i, ip, port, b, n, now);
        }
        for (;;) {
            uint32_t ip;
            uint16_t port;
            sock_t s = tcp_accept(g_tcp[i], &ip, &port);
            if (s == BADSOCK) break;
            Web *w = 0;
            for (int k = 0; k < MAX_WEB && !w; k++)
                if (!g_web[k].s) w = &g_web[k];
            if (!w) { sock_close(s); continue; }
            memset(w, 0, sizeof *w);
            w->s = s;
            w->port = TCP_PORT[i];
            w->since = now;
        }
    }
    for (int i = 0; i < MAX_WEB; i++)
        if (g_web[i].s) web_poll(&g_web[i], now);
    srv_tick(now);
}
