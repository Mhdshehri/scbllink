/* srv.c - see srv.h. Layout facts are in docs/server-protocol.md.
 * Everything that arrives is a friend's data: every length is checked. By Mhdshehri. */
#include "srv.h"
#include "md5.h"
#include "inflate.h"

/* ---------- constants of the game's network library ---------- */
static const uint8_t ACCESS_KEY[] = "yl4NG7qZ"; /* packet checksum */
static const uint8_t STREAM_KEY[] = "CD&ML";    /* payload cipher */
#define KERB_PASSWORD "UbiDummyPwd"             /* ticket key: MD5 of this, 65000 + pid % 1024 times */
#define KERB_TRACKING "JaDe!"                   /* ... and for the "Tracking" login, the game's own one */

enum { T_SYN, T_CONNECT, T_DATA, T_DISCONNECT, T_PING };
enum { F_ACK = 1, F_RELIABLE = 2, F_NEED_ACK = 4, F_HAS_SIZE = 8 };
#define ME 0x31            /* the server's stream byte: port 1, secure stream */
#define OK_CODE 0x00010001u
#define ERR_NOT_IMPLEMENTED 0x80010002u
#define ERR_ACCESS_DENIED 0x80010006u
#define ERR_INVALID_ARGUMENT 0x80010004u

#define PID_SERVER 4096u   /* the secure server, as a principal */
#define PID_TRACKING 105u  /* the game's second login, "Tracking" */
#define PID_TRACKING_TARGET 2u
#define PID_FILES 1119u    /* owner of the server's own files */
#define PID_FIRST 1001u

#define MAX_CONNS 64
#define MAX_PLAYERS 16
#define MAX_TICKETS 32
#define MAX_SESSIONS 16
#define MAX_ATTRS 48
#define MAX_PARTS 8
#define MAX_MSG 65536      /* one whole call */
#define FRAG 900           /* bytes of a call per packet */
#define MAX_PACKET 1400
#define RESEND_MS 400
#define RESEND_TRIES 20
#define IDLE_MS 45000
#define TICKET_MS 120000
#define TOKEN 32

typedef struct Out { struct Out *next; uint16_t seq; int n, tries; uint64_t sent; uint8_t d[]; } Out;

typedef struct {
    bool used, connected;
    int which;
    uint32_t ip;
    uint16_t port;
    uint8_t peer, sess_mine, sess_peer;
    uint32_t sig_mine, sig_peer;
    uint16_t in_next, out_next;
    uint64_t last;
    uint32_t pid, cid;
    uint8_t *msg;
    int msg_len;
    Out *out;
} Conn;

#define MAX_URLS 4
#define URL_LEN 256
typedef struct { bool used; char name[64]; uint32_t pid; char url[MAX_URLS][URL_LEN]; int nurl; } Player;
/* "from invited to": the invited game's search and join need it (see search_with) */
typedef struct { bool used; uint32_t from, to; uint64_t made; } Invite;
#define MAX_INVITES 16
#define INVITE_MS (5 * 60 * 1000)
#define ROOM_KIND 113 /* 0: a private match's own room, 1: the anteroom every game opens */
#define MAX_RESULTS 16
typedef struct { bool used; uint8_t token[TOKEN], key[16]; uint32_t pid; uint64_t made; } Ticket;
typedef struct { uint32_t id; int32_t value; } Attr;
typedef struct {
    bool used;
    uint32_t type, id, owner;
    Attr attr[MAX_ATTRS];
    int nattr;
    uint32_t part[MAX_PARTS];
    bool part_private[MAX_PARTS];
    int npart;
} Session;

static SrvHost H;
static bool g_on;
static Conn g_conn[MAX_CONNS];
static Player g_player[MAX_PLAYERS];
static Ticket g_ticket[MAX_TICKETS];
static Session g_session[MAX_SESSIONS];
static Invite g_invite[MAX_INVITES];
static uint32_t g_next_call;
static uint32_t g_next_cid, g_next_session;
static uint64_t g_now, g_pending;
static uint8_t g_kerb_key[16];
static uint32_t g_kerb_pid = 0xFFFFFFFF;

static void say(const char *fmt, ...)
{
    char line[400];
    va_list ap;
    va_start(ap, fmt);
#ifdef _WIN32
    _vsnprintf(line, sizeof line, fmt, ap);
#else
    vsnprintf(line, sizeof line, fmt, ap);
#endif
    va_end(ap);
    line[sizeof line - 1] = 0;
    if (H.log) H.log(line);
}

static void ip_text(uint32_t ip, char out[16])
{
    const uint8_t *b = (const uint8_t *)&ip;
    sfmt(out, 16, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
}

/* ---------- reading and writing the game's values (little-endian) ---------- */
typedef struct { const uint8_t *p; int n, o; bool bad; } R;
typedef struct { uint8_t *p; int n, cap; bool over; } W;

static const uint8_t *r_take(R *r, int n)
{
    if (r->bad || n < 0 || n > r->n - r->o) { r->bad = true; return 0; }
    const uint8_t *p = r->p + r->o;
    r->o += n;
    return p;
}
static uint32_t r_u32(R *r) { const uint8_t *p = r_take(r, 4); return p ? (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24 : 0; }
static uint16_t r_u16(R *r) { const uint8_t *p = r_take(r, 2); return p ? (uint16_t)(p[0] | p[1] << 8) : 0; }
static uint8_t r_u8(R *r) { const uint8_t *p = r_take(r, 1); return p ? p[0] : 0; }
/* a string: u16 length (with its closing zero), bytes. Too long for out = bad. */
static void r_str(R *r, char *out, int cap)
{
    int n = r_u16(r);
    const uint8_t *p = r_take(r, n);
    out[0] = 0;
    if (!p || !n) return;
    if (n > cap || p[n - 1] != 0) { r->bad = true; return; }
    memcpy(out, p, (size_t)n);
}
static void r_skip_str(R *r) { r_take(r, r_u16(r)); }

static void w_raw(W *w, const void *p, int n)
{
    if (w->over || n < 0 || n > w->cap - w->n) { w->over = true; return; }
    if (!n) return;
    memcpy(w->p + w->n, p, (size_t)n);
    w->n += n;
}
static void w_u8(W *w, uint8_t v) { w_raw(w, &v, 1); }
static void w_u16(W *w, uint16_t v) { uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; w_raw(w, b, 2); }
static void w_u32(W *w, uint32_t v) { uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) }; w_raw(w, b, 4); }
static void w_u64(W *w, uint64_t v) { w_u32(w, (uint32_t)v); w_u32(w, (uint32_t)(v >> 32)); }
static void w_str(W *w, const char *s) { int n = (int)strlen(s) + 1; w_u16(w, (uint16_t)n); w_raw(w, s, n); }

/* ---------- packets ---------- */
static uint8_t checksum(const uint8_t *d, int n)
{
    uint32_t sum = 0;
    int whole = n - n % 4;
    for (int i = 0; i < whole; i += 4) sum += (uint32_t)d[i] | (uint32_t)d[i + 1] << 8 | (uint32_t)d[i + 2] << 16 | (uint32_t)d[i + 3] << 24;
    uint8_t c = (uint8_t)(sum + (sum >> 8) + (sum >> 16) + (sum >> 24));
    for (int i = whole; i < n; i++) c = (uint8_t)(c + d[i]);
    for (size_t i = 0; i < sizeof ACCESS_KEY - 1; i++) c = (uint8_t)(c + ACCESS_KEY[i]);
    return c;
}

/* Builds one packet into out (MAX_PACKET); returns its length. `extra`: the connection
   signature (SYN, CONNECT) or the fragment number (DATA). */
static int build(Conn *c, uint8_t *out, int type, int flags, uint16_t seq, uint32_t extra, const uint8_t *payload, int n)
{
    W w = { out, 0, MAX_PACKET - 1, false };
    w_u8(&w, ME);
    w_u8(&w, c->peer);
    w_u8(&w, (uint8_t)(type | flags << 3));
    w_u8(&w, type == T_SYN ? 0 : c->sess_mine);
    w_u32(&w, type == T_SYN ? 0 : c->sig_peer);
    w_u16(&w, seq);
    if (type == T_SYN || type == T_CONNECT) w_u32(&w, extra);
    if (type == T_DATA) w_u8(&w, (uint8_t)extra);
    if (flags & F_HAS_SIZE) w_u16(&w, (uint16_t)n);
    w_raw(&w, payload, n);
    if (w.over) return 0;
    out[w.n] = checksum(out, w.n);
    return w.n + 1;
}

static void reply(Conn *c, int type, uint16_t seq, uint32_t extra, const uint8_t *payload, int n)
{
    uint8_t p[MAX_PACKET];
    int k = build(c, p, type, F_ACK | F_HAS_SIZE, seq, extra, payload, n);
    if (k) H.send(c->which, c->ip, c->port, p, k);
}

static void conn_free(Conn *c)
{
    while (c->out) { Out *o = c->out; c->out = o->next; free(o); }
    free(c->msg);
    memset(c, 0, sizeof *c);
}

static void session_drop_player(uint32_t pid);

static void conn_close(Conn *c, const char *why)
{
    if (c->which == SRV_SECURE && c->connected && c->pid >= PID_FIRST) {
        bool other = false; /* the same player on another connection? */
        for (int i = 0; i < MAX_CONNS; i++)
            if (g_conn[i].used && &g_conn[i] != c && g_conn[i].which == SRV_SECURE && g_conn[i].connected && g_conn[i].pid == c->pid) other = true;
        if (!other) {
            session_drop_player(c->pid);
            say("server: player %u left (%s)", (unsigned)c->pid, why);
        }
    }
    conn_free(c);
}

static Conn *conn_find(int which, uint32_t ip, uint16_t port, uint8_t peer)
{
    for (int i = 0; i < MAX_CONNS; i++) {
        Conn *c = &g_conn[i];
        if (c->used && c->which == which && c->ip == ip && c->port == port && c->peer == peer) return c;
    }
    return 0;
}

static Conn *conn_new(int which, uint32_t ip, uint16_t port, uint8_t peer)
{
    Conn *c = 0;
    for (int i = 0; i < MAX_CONNS && !c; i++)
        if (!g_conn[i].used) c = &g_conn[i];
    if (!c) { /* full: the one that has been quiet the longest goes */
        c = &g_conn[0];
        for (int i = 1; i < MAX_CONNS; i++)
            if (g_conn[i].last < c->last) c = &g_conn[i];
        conn_close(c, "no room");
    }
    c->used = true;
    c->which = which;
    c->ip = ip;
    c->port = port;
    c->peer = peer;
    c->last = g_now;
    uint8_t r[5];
    H.random(r, 5);
    memcpy(&c->sig_mine, r, 4);
    c->sess_mine = r[4];
    c->in_next = 1;
    c->out_next = 1;
    return c;
}

/* One whole answer to the game: cut into packets, kept until each is acknowledged. */
static void send_message(Conn *c, const uint8_t *m, int n)
{
    int parts = (n + FRAG - 1) / FRAG;
    for (int i = 0; i < parts; i++) {
        uint8_t body[FRAG + 16], p[MAX_PACKET];
        int len = i == parts - 1 ? n - i * FRAG : FRAG;
        body[0] = 1; /* packed (as stored blocks) */
        int z = zlib_store(m + i * FRAG, len, body + 1, sizeof body - 1);
        if (z < 0) return;
        rc4(STREAM_KEY, sizeof STREAM_KEY - 1, body, (size_t)z + 1);
        int k = build(c, p, T_DATA, F_RELIABLE | F_NEED_ACK | F_HAS_SIZE, c->out_next, i == parts - 1 ? 0 : (uint32_t)(i + 1), body, z + 1);
        Out *o = k ? malloc(sizeof(Out) + (size_t)k) : 0;
        if (!o) return;
        o->next = 0; o->seq = c->out_next++; o->n = k; o->tries = 1; o->sent = g_now;
        memcpy(o->d, p, (size_t)k);
        Out **at = &c->out;
        while (*at) at = &(*at)->next;
        *at = o;
        H.send(c->which, c->ip, c->port, p, k);
    }
}

/* ---------- accounts and tickets ---------- */
static Player *player_by_pid(uint32_t pid)
{
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (g_player[i].used && g_player[i].pid == pid) return &g_player[i];
    return 0;
}

static Player *player_for(const char *name)
{
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (g_player[i].used && !strcmp(g_player[i].name, name)) return &g_player[i];
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (!g_player[i].used) {
            g_player[i].used = true;
            sfmt(g_player[i].name, sizeof g_player[i].name, "%s", name);
            g_player[i].pid = PID_FIRST + (uint32_t)i;
            return &g_player[i];
        }
    return 0;
}

static const uint8_t *kerb_key(uint32_t pid)
{
    if (pid != g_kerb_pid) {
        uint8_t k[16];
        const char *pw = pid == PID_TRACKING ? KERB_TRACKING : KERB_PASSWORD;
        md5((const uint8_t *)pw, strlen(pw), k);
        for (uint32_t i = 1; i < 65000 + pid % 1024; i++) md5(k, 16, k);
        memcpy(g_kerb_key, k, 16);
        g_kerb_pid = pid;
    }
    return g_kerb_key;
}

/* A ticket for `pid` to the principal `target`, as the game unpacks it:
   RC4(key){ session key[16], target, u32 length, our token } then HMAC-MD5 of that.
   The token is only a number we recognise again; what it stands for stays here. */
static void w_ticket(W *w, uint32_t pid, uint32_t target)
{
    Ticket *t = &g_ticket[0];
    for (int i = 0; i < MAX_TICKETS; i++) {
        if (!g_ticket[i].used) { t = &g_ticket[i]; break; }
        if (g_ticket[i].made < t->made) t = &g_ticket[i];
    }
    t->used = true;
    t->pid = pid;
    t->made = g_now;
    H.random(t->token, TOKEN);
    H.random(t->key, 16);
    uint8_t b[16 + 8 + TOKEN + 16];
    W x = { b, 0, sizeof b, false };
    w_raw(&x, t->key, 16);
    w_u32(&x, target);
    w_u32(&x, TOKEN);
    w_raw(&x, t->token, TOKEN);
    const uint8_t *k = kerb_key(pid);
    rc4(k, 16, b, (size_t)x.n);
    hmac_md5(k, 16, b, (size_t)x.n, b + x.n);
    w_u32(w, (uint32_t)x.n + 16);
    w_raw(w, b, x.n + 16);
}

static void w_station(W *w, uint32_t target)
{
    char ip[16], url[128];
    ip_text(H.ip, ip);
    sfmt(url, sizeof url, "prudps:/address=%s;port=%d;stream=3;sid=1;PID=%u;CID=1;type=2", ip, SRV_PORT_SECURE, (unsigned)target);
    w_str(w, url);
    w_u32(w, 0);
    w_str(w, ":/address=;port=0");
    w_u16(w, 0);
}

/* ---------- sessions ---------- */
static Session *session_get(uint32_t type, uint32_t id)
{
    for (int i = 0; i < MAX_SESSIONS; i++)
        if (g_session[i].used && g_session[i].type == type && g_session[i].id == id) return &g_session[i];
    return 0;
}

static void session_leave(Session *s, uint32_t pid)
{
    for (int i = 0; i < s->npart; i++)
        if (s->part[i] == pid) {
            s->part[i] = s->part[s->npart - 1];
            s->part_private[i] = s->part_private[s->npart - 1];
            s->npart--;
            i--;
        }
}

static void session_drop_player(uint32_t pid)
{
    for (int i = 0; i < MAX_SESSIONS; i++) {
        Session *s = &g_session[i];
        if (!s->used) continue;
        session_leave(s, pid);
        if (s->owner == pid) s->used = false;
    }
}

static bool session_attr(const Session *s, uint32_t id, int32_t *v)
{
    for (int i = 0; i < s->nattr; i++)
        if (s->attr[i].id == id) { if (v) *v = s->attr[i].value; return true; }
    return false;
}

static int room_kind(const Session *s)
{
    int32_t v;
    return session_attr(s, ROOM_KIND, &v) ? v : -1;
}

static bool session_has(const Session *s, uint32_t pid)
{
    for (int i = 0; i < s->npart; i++)
        if (s->part[i] == pid) return true;
    return false;
}

static void session_add(Session *s, uint32_t pid, bool priv)
{
    session_leave(s, pid);
    if (s->npart < MAX_PARTS) { s->part[s->npart] = pid; s->part_private[s->npart++] = priv; }
}

/* the newest invitation from `from` to `to` still standing */
static Invite *invite_find(uint32_t from, uint32_t to)
{
    Invite *best = 0;
    for (int i = 0; i < MAX_INVITES; i++) {
        Invite *v = &g_invite[i];
        if (!v->used) continue;
        if (g_now - v->made > INVITE_MS) { v->used = false; continue; }
        if (v->from == from && v->to == to && (!best || v->made > best->made)) best = v;
    }
    return best;
}

/* The room `guest` was invited into by one of `hosts`: the inviter's private match room when
   there is one, else its anteroom. */
static Session *invited_room(uint32_t guest, const uint8_t *hosts, int nhosts, uint32_t type)
{
    for (int h = 0; h < nhosts; h++) {
        uint32_t host;
        memcpy(&host, hosts + 4 * h, 4);
        if (!invite_find(host, guest)) continue;
        Session *match = 0, *ante = 0;
        for (int i = 0; i < MAX_SESSIONS; i++) {
            Session *s = &g_session[i];
            if (!s->used || s->type != type || s->owner != host) continue;
            if (room_kind(s) == 0 && !match) match = s;
            if (room_kind(s) == 1 && !ante) ante = s;
        }
        if (match || ante) return match ? match : ante;
    }
    return 0;
}

static bool r_attrs(R *r, Session *s)
{
    uint32_t n = r_u32(r);
    if (r->bad || n > MAX_ATTRS) return false;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t id = r_u32(r);
        int32_t v = (int32_t)r_u32(r);
        int k = 0;
        while (k < s->nattr && s->attr[k].id != id) k++;
        if (k == MAX_ATTRS) return false;
        s->attr[k].id = id;
        s->attr[k].value = v;
        if (k == s->nattr) s->nattr++;
    }
    return !r->bad;
}

/* A call from the server to a game: NotificationEventManager (14).ProcessNotificationEvent (1).
   7003 (category 7, kind 3) opens a private match's join gate: a game joining one waits for it
   ("creating game session..." for ever without). `about` is the player the gate is for, as the
   game compares it with its own player, and `session` the room. */
static void notify_join_gate(uint32_t about, uint32_t from, uint32_t session)
{
    for (int i = 0; i < MAX_CONNS; i++) {
        Conn *c = &g_conn[i];
        if (!c->used || !c->connected || c->which != SRV_SECURE || c->pid != about) continue;
        uint8_t m[64];
        W w = { m, 0, sizeof m, false };
        w_u32(&w, 0);
        w_u8(&w, 14 | 0x80);
        w_u32(&w, ++g_next_call);
        w_u32(&w, 1);
        w_u32(&w, from);    /* pid_source */
        w_u32(&w, 7003);    /* ui_type */
        w_u32(&w, about);   /* ui_param_1 */
        w_u32(&w, session); /* ui_param_2 */
        w_str(&w, "");      /* str_param */
        w_u32(&w, 0);       /* ui_param_3 */
        uint32_t len = (uint32_t)w.n - 4;
        memcpy(m, &len, 4);
        send_message(c, m, w.n);
        say("server: player %u may come into room %u (from player %u)", (unsigned)about, (unsigned)session, (unsigned)from);
    }
}

static void w_session_result(W *w, const Session *s)
{
    Player *host = player_by_pid(s->owner);
    w_u32(w, s->type);
    w_u32(w, s->id);
    w_u32(w, s->owner);
    w_u32(w, host ? (uint32_t)host->nurl : 0);
    for (int i = 0; host && i < host->nurl; i++) w_str(w, host->url[i]);
    w_u32(w, (uint32_t)s->nattr);
    for (int i = 0; i < s->nattr; i++) { w_u32(w, s->attr[i].id); w_u32(w, (uint32_t)s->attr[i].value); }
    w_u32(w, (uint32_t)s->npart);
    for (int i = 0; i < s->npart; i++) w_u32(w, s->part[i]);
}

/* SearchSessionsWithParticipants: the sessions the named players are in. A private match's
   room is answered only to whom it concerns (its owner, its members, the invited). An invited
   game gets exactly the invited room and the inviter's anteroom: it files them by ROOM_KIND,
   and the match room (0) takes it into the match. It's put in the room first, as a join
   accepted. */
static uint32_t search_with(Conn *c, R *r, W *w)
{
    uint32_t type = r_u32(r), n = r_u32(r);
    if (r->bad || n > 16) return ERR_INVALID_ARGUMENT;
    const uint8_t *pids = r_take(r, (int)n * 4);
    if (r->bad) return ERR_INVALID_ARGUMENT;
    Session *inv = invited_room(c->pid, pids, (int)n, type);
    if (inv && !session_has(inv, c->pid)) {
        session_add(inv, c->pid, true);
        say("server: player %u, invited, put in room %u", (unsigned)c->pid, (unsigned)inv->id);
    }
    int at = w->n;
    uint32_t found = 0;
    w_u32(w, 0);
    bool ante_given = false;
    for (int i = 0; i < MAX_SESSIONS && found < MAX_RESULTS; i++) {
        Session *s = &g_session[i];
        if (!s->used || s->type != type || !session_has(s, s->owner)) continue;
        bool named = false;
        for (uint32_t k = 0; k < n && !named; k++) {
            uint32_t pid;
            memcpy(&pid, pids + 4 * k, 4);
            named = s->owner == pid || session_has(s, pid);
        }
        if (!named) continue;
        if (inv) { /* the invited room, and one anteroom of its host */
            if (s != inv && !(room_kind(s) == 1 && s->owner == inv->owner && !ante_given)) continue;
            if (s != inv) ante_given = true;
        } else if (room_kind(s) == 0 && s->owner != c->pid && !session_has(s, c->pid)) continue;
        w_session_result(w, s);
        found++;
    }
    if (!w->over) memcpy(w->p + at, &found, 4);
    return 0;
}

/* May `by` put `pid` in the private match room `s`? */
static bool may_enter(const Session *s, uint32_t by, uint32_t pid)
{
    if (pid == s->owner) return by == s->owner;
    if (invite_find(s->owner, pid)) return by == s->owner || by == pid;
    if (by != s->owner) return false;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        const Session *a = &g_session[i];
        if (a->used && a->type == s->type && a->owner == s->owner && room_kind(a) == 1 && session_has(a, pid)) return true;
    }
    return false;
}

/* NatTraversal (3).RequestProbeInitiationExt: a game about to join asks that the games it names
   send it a probe first, so routers in between let its packets in (hole punching). On the
   SCBLLink network there's no router between games, but the game may wait for it: each named
   game is told InitiateProbe (3.2) with the asker's address, as the game expects. Only games
   that share a room with the asker, or invited it, are told, so nobody can make a game send
   packets to an address of their choosing. */
static bool share_room(uint32_t a, uint32_t b)
{
    if (invite_find(a, b) || invite_find(b, a)) return true;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        const Session *s = &g_session[i];
        if (s->used && (session_has(s, a) || s->owner == a) && (session_has(s, b) || s->owner == b)) return true;
    }
    return false;
}

static uint32_t url_rvcid(const char *u)
{
    const char *p = strstr(u, "RVCID=");
    if (!p) return 0;
    uint32_t v = 0;
    for (p += 6; *p >= '0' && *p <= '9'; p++) v = v * 10 + (uint32_t)(*p - '0');
    return v;
}

static uint32_t probe_ask(Conn *c, R *r)
{
    char target[8][URL_LEN], me[URL_LEN];
    uint32_t n = r_u32(r);
    if (r->bad || n > 8) return ERR_INVALID_ARGUMENT;
    for (uint32_t i = 0; i < n; i++) r_str(r, target[i], URL_LEN);
    r_str(r, me, URL_LEN);
    if (r->bad || !me[0]) return ERR_INVALID_ARGUMENT;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t cid = url_rvcid(target[i]);
        for (int k = 0; cid && k < MAX_CONNS; k++) {
            Conn *t = &g_conn[k];
            if (!t->used || !t->connected || t->which != SRV_SECURE || t->cid != cid || t == c || !t->pid) continue;
            if (!share_room(c->pid, t->pid)) { say("server: player %u asked a probe from %u, not in its room; ignored", (unsigned)c->pid, (unsigned)t->pid); continue; }
            uint8_t m[16 + URL_LEN];
            W w = { m, 0, sizeof m, false };
            w_u32(&w, 0);
            w_u8(&w, 3 | 0x80);
            w_u32(&w, ++g_next_call);
            w_u32(&w, 2);
            w_str(&w, me);
            if (w.over) continue;
            uint32_t len = (uint32_t)w.n - 4;
            memcpy(m, &len, 4);
            send_message(t, m, w.n);
            say("server: player %u will probe player %u", (unsigned)t->pid, (unsigned)c->pid);
        }
    }
    return 0;
}

/* ---------- the calls ---------- */
/* Server settings the game reads at login: names and values as the game expects them.
   kind: 1 whole number, 2 decimal, 4 text. Names that are only a number are hashes of names. */
typedef struct { const char *name; uint8_t kind; double num; const char *text; } Setting;
static const Setting SETTINGS[] = {
    { "VER_SERVER_STAGE", 1, 10, 0 }, { "2593515025", 1, 1, 0 }, { "2626349757", 1, 1, 0 },
    { "NC_CONNECTION_TICKET_TIMEOUT", 2, 10, 0 }, { "NC_CONNECTION_HANDSHAKING_TIMEOUT", 2, 5, 0 },
    { "314486871", 1, 1, 0 }, { "3759634546", 1, 1, 0 }, { "NC_MAIN_PORT_RANGE", 1, 32, 0 }, { "2449304206", 1, 1, 0 },
    { "OVERLORD_VERSION", 4, 0, "0.8.0.0" }, { "2685611256", 1, 1, 0 }, { "3376331533", 1, 1, 0 },
    { "VER_SERVER_CODE", 1, 3007, 0 }, { "FILESERVICE_UNLOCKS_UPLOAD_ENABLED", 1, 1, 0 }, { "2739184075", 1, 1, 0 },
    { "2942435614", 1, 1, 0 }, { "SN_FRIENDCHALLENGES_MAX_READ_INTERVAL", 2, 900, 0 }, { "NC_CONNECTION_JOIN_TIMEOUT", 2, 15, 0 },
    { "1027449109", 1, 1, 0 }, { "4175756708", 1, 1, 0 }, { "NC_CONNECTION_INACTIVITY_THRESHOLD", 2, 8, 0 },
    { "FILESERVICE_ADMIN_RDVID", 1, PID_FILES, 0 }, { "SN_WEEKLYCHALLENGES_ENABLE", 1, 1, 0 }, { "1597953054", 1, 1, 0 },
    { "2156388390", 1, 1, 0 }, { "3785106560", 1, 1, 0 }, { "STATS_WRITE_INTERVAL", 2, 1, 0 }, { "3835530207", 1, 1, 0 },
    { "1492891464", 1, 1, 0 }, { "SN_DAILYCHALLENGES_ENABLE", 1, 1, 0 }, { "2505766166", 1, 1, 0 }, { "11866509", 1, 1, 0 },
    { "SN_FRIENDCHALLENGES_ENABLE", 1, 1, 0 }, { "FILESERVICE_UNLOCKS_UPLOAD_INTERVAL", 2, 3600, 0 }, { "2524360986", 1, 1, 0 },
    { "721797971", 1, 1, 0 }, { "1525666223", 1, 1, 0 }, { "COMMUNITYEVENT_DOUBLECASH", 1, 0, 0 },
    { "UPLAY_MAX_RANK_LIMITED_MODE", 1, 5, 0 }, { "SN_GONEDARKCHALLENGES_ENABLE", 1, 1, 0 },
    { "_OSDK_VERSION", 4, 0, "1.4.16.32918" }, { "NC_MAIN_PORT", 1, 13000, 0 }, { "COMMUNITYEVENT_DOUBLEXP", 1, 0, 0 },
    { "3804368594", 1, 1, 0 }, { "NC_CONNECTION_CLOSING_TIMEOUT", 2, 2, 0 }, { "NC_CONNECTION_ESTABLISHED_TIMEOUT", 2, 10, 0 },
};
/* statistics the game expects as decimals (the rest are whole numbers) */
static bool stat_is_decimal(uint32_t id) { return id == 102 || id == 106 || id == 108 || id == 125 || id == 215 || id == 228 || id == 229; }

/* where no value is stored yet: 0, but for two "best so far, lower is better" ones */
static uint64_t stat_default(uint32_t board, uint32_t id) { return (board == 10 && id == 213) || (board == 23 && id == 154) ? 0x7FFFFFFF : 0; }
#define MAX_STAT_RESULTS 100 /* as many as the recorded server gave for one call, and the game was content */

static void w_f64(W *w, double v) { uint64_t u; memcpy(&u, &v, 8); w_u64(w, u); }

#define TYPE_BALANCING 0x80000002u /* the server's multiplayer balancing file */
#define TYPE_PLAYER_FILE 0x80000003u

static const char BALANCING[] =
    "[Echelon.EPlayerPawn]\r\nSpyWeaponEffectDurationLookUpTable=3.0f;\r\nSpyWeaponEffectDurationLookUpTable=2.0f;\r\n"
    "SpyWeaponEffectDurationLookUpTable=1.0f;\r\n\r\n[Echelon.GI_Adv_AntiGrenadeSystem]\r\nm_AdversarialInventoryLimit=2\r\n\r\n"
    "[MPArmorHelmetTech]\r\nm_fBMSRefreshDelayMultiplier=0.8\r\n\r\n[EXPLOSION_STICKYCAM_ADV]\r\nGroundZeroRadius=280\r\n"
    "GroundZeroZoneDamage=150\r\nBlastZoneDamage=80\r\nNPCGroundZeroRadius=280\r\nNPCBlastRadius=600\r\n"
    "NPCGroundZeroZoneDamage=150\r\nNPCBlastZoneDamage=80\r\n\r\n[EXPLOSION_PROXMINE_ADV]\r\nGroundZeroZoneDamage=150\r\n\r\n"
    "[EXPLOSION_GRENADE_ADV]\r\nGroundZeroZoneDamage=150\r\n\r\n[EXPLOSION_ADVDRONE]\r\nGroundZeroZoneDamage=150\r\n";

static void w_files_host(W *w)
{
    char ip[16], host[32];
    ip_text(H.ip, ip);
    sfmt(host, sizeof host, "%s:%d", ip, SRV_PORT_FILES);
    w_str(w, "http://");
    w_str(w, host);
}

/* TicketGranting.Login / LoginEx */
static uint32_t do_login(Conn *c, R *r, W *w, bool ex)
{
    char name[64], user[64], key[64], pass[64];
    r_str(r, name, sizeof name);
    uint32_t pid;
    if (!ex) {
        if (r->bad || strcmp(name, "Tracking") != 0) return ERR_ACCESS_DENIED;
        pid = PID_TRACKING;
    } else {
        r_skip_str(r); /* the type's name */
        r_u32(r);
        r_u32(r);
        r_str(r, user, sizeof user);
        r_str(r, key, sizeof key);
        r_str(r, pass, sizeof pass);
        if (r->bad || !name[0] || strcmp(name, user) != 0) return ERR_INVALID_ARGUMENT;
        if (H.login && !H.login(name, pass)) { say("server: login refused for \"%s\" (not a lobby member's game account)", name); return ERR_ACCESS_DENIED; }
        Player *p = player_for(name);
        if (!p) return ERR_ACCESS_DENIED;
        pid = p->pid;
        say("server: %s logged in (player %u)", name, (unsigned)pid);
    }
    c->pid = pid;
    w_u32(w, OK_CODE);
    w_u32(w, pid);
    w_ticket(w, pid, PID_SERVER);
    w_station(w, ex ? PID_SERVER : PID_TRACKING_TARGET);
    return 0;
}

static uint32_t do_register(Conn *c, W *w, bool ex)
{
    char ip[16], url[128];
    ip_text(c->ip, ip);
    if (!c->cid) c->cid = g_next_cid++;
    if (ex) sfmt(url, sizeof url, "prudp:/address=%s;port=%u;type=3;sid=%u", ip, (unsigned)c->port, (unsigned)(c->peer & 15));
    else sfmt(url, sizeof url, "prudp:/address=%s;port=%u;sid=%u;type=2", ip, (unsigned)c->port, (unsigned)(c->peer & 15));
    w_u32(w, OK_CODE);
    w_u32(w, c->cid);
    w_str(w, url);
    return 0;
}

/* ---------- statistics the game writes (kept while the server runs) ---------- */
#define MAX_STATS 8192
typedef struct { uint32_t pid, board, ctx, id; uint8_t kind; uint64_t value; } Stat;
static Stat g_stat[MAX_STATS];
static int g_nstat;
static uint64_t g_stat_time;

static Stat *stat_find(uint32_t pid, uint32_t board, uint32_t ctx, uint32_t id)
{
    for (int i = 0; i < g_nstat; i++) {
        Stat *s = &g_stat[i];
        if (s->pid == pid && s->board == board && s->ctx == ctx && s->id == id) return s;
    }
    return 0;
}

static uint32_t do_write_stats(Conn *c, R *r)
{
    uint32_t nb = r_u32(r);
    if (r->bad || nb > 512) return ERR_INVALID_ARGUMENT;
    for (uint32_t b = 0; b < nb; b++) {
        uint32_t board = r_u32(r), nctx = r_u32(r);
        if (r->bad || nctx > 64) return ERR_INVALID_ARGUMENT;
        const uint8_t *ctx = r_take(r, (int)nctx * 4);
        uint32_t ns = r_u32(r);
        if (r->bad || ns > 1024) return ERR_INVALID_ARGUMENT;
        for (uint32_t i = 0; i < ns; i++) {
            uint32_t id = r_u32(r);
            uint8_t kind = r_u8(r);
            if (kind != 1 && kind != 2 && kind != 6) return ERR_INVALID_ARGUMENT; /* whole, decimal, unsigned */
            const uint8_t *v = r_take(r, 8);
            if (r->bad) return ERR_INVALID_ARGUMENT;
            uint64_t value;
            memcpy(&value, v, 8);
            for (uint32_t k = 0; k < nctx; k++) {
                uint32_t cx;
                memcpy(&cx, ctx + 4 * k, 4);
                Stat *s = stat_find(c->pid, board, cx, id);
                if (!s && g_nstat < MAX_STATS) s = &g_stat[g_nstat++];
                if (!s) continue;
                s->pid = c->pid; s->board = board; s->ctx = cx; s->id = id; s->kind = kind; s->value = value;
            }
        }
    }
    g_stat_time = H.unix_time();
    return 0;
}

static uint32_t do_stats(R *r, W *w)
{
    uint32_t npid = r_u32(r);
    if (npid > 64) return ERR_INVALID_ARGUMENT;
    const uint8_t *pids = r_take(r, (int)npid * 4);
    uint32_t nq = r_u32(r), results = 0;
    if (r->bad || nq > 4096) return ERR_INVALID_ARGUMENT;
    int at = w->n;
    w_u32(w, 0);
    for (uint32_t q = 0; q < nq; q++) {
        uint32_t board = r_u32(r), nctx = r_u32(r);
        if (r->bad || nctx > 256) return ERR_INVALID_ARGUMENT;
        const uint8_t *ctx = r_take(r, (int)nctx * 4);
        uint32_t freq = r_u32(r), nstat = r_u32(r);
        if (r->bad || nstat > 1024) return ERR_INVALID_ARGUMENT;
        const uint8_t *stat = r_take(r, (int)nstat * 4);
        uint32_t nsort = r_u32(r);
        if (r->bad || nsort > 64) return ERR_INVALID_ARGUMENT;
        r_take(r, (int)nsort * 5); /* statistic, direction */
        if (r->bad) return ERR_INVALID_ARGUMENT;
        for (uint32_t k = 0; k < (nctx ? nctx : 1) && results < MAX_STAT_RESULTS; k++) { /* no context named: the general one, 0 */
            uint32_t cid = 0;
            if (nctx) memcpy(&cid, ctx + 4 * k, 4);
            w_u32(w, board);
            w_u32(w, cid);
            w_u32(w, freq);
            int sets_at = w->n;
            uint32_t sets = 0;
            w_u32(w, 0);
            for (uint32_t pi = 0; pi < npid; pi++) { /* the values each asked-for player has written */
                uint32_t pid;
                memcpy(&pid, pids + 4 * pi, 4);
                Player *pl = player_by_pid(pid);
                int have = 0;
                for (uint32_t i = 0; i < nstat; i++) {
                    uint32_t id;
                    memcpy(&id, stat + 4 * i, 4);
                    if (stat_find(pid, board, cid, id)) have++;
                }
                if (!pl || !have) continue;
                w_u32(w, pid);
                w_str(w, pl->name);
                w_u64(w, g_stat_time);
                w_u32(w, (uint32_t)have);
                for (uint32_t i = 0; i < nstat; i++) {
                    uint32_t id;
                    memcpy(&id, stat + 4 * i, 4);
                    Stat *st = stat_find(pid, board, cid, id);
                    if (!st) continue;
                    w_u32(w, id); w_u8(w, st->kind); w_u64(w, st->value);
                }
                sets++;
            }
            if (!w->over) memcpy(w->p + sets_at, &sets, 4);
            w_u32(w, nstat);
            for (uint32_t i = 0; i < nstat; i++) {
                uint32_t id;
                memcpy(&id, stat + 4 * i, 4);
                w_u32(w, id);
                w_u8(w, stat_is_decimal(id) ? 2 : 1);
                w_u64(w, stat_default(board, id));
            }
            results++;
        }
    }
    if (!w->over) memcpy(w->p + at, &results, 4);
    return 0;
}

/* Returns 0 (answer in w) or an error code. */
static uint32_t call(Conn *c, uint32_t proto, uint32_t method, R *r, W *w)
{
    if (c->which == SRV_AUTH) {
        if (proto != 10) return ERR_ACCESS_DENIED;
        if (method == 1 || method == 2) return do_login(c, r, w, method == 2);
        if (method == 3) { /* RequestTicket(source, target) */
            uint32_t src = r_u32(r), dst = r_u32(r);
            if (r->bad || !c->pid || src != c->pid) return ERR_ACCESS_DENIED;
            w_u32(w, OK_CODE);
            w_ticket(w, src, dst);
            return 0;
        }
        return ERR_NOT_IMPLEMENTED;
    }
    if (!c->pid) return ERR_ACCESS_DENIED;
    switch (proto << 8 | method) {
    case 11 << 8 | 1: return do_register(c, w, false);
    case 11 << 8 | 4: return do_register(c, w, true);
    case 5003 << 8 | 1: /* the server's settings */
        w_u32(w, sizeof SETTINGS / sizeof SETTINGS[0]);
        for (size_t i = 0; i < sizeof SETTINGS / sizeof SETTINGS[0]; i++) {
            const Setting *s = &SETTINGS[i];
            w_str(w, s->name);
            w_u8(w, s->kind);
            if (s->kind == 1) w_u64(w, (uint64_t)s->num);
            else if (s->kind == 2) w_f64(w, s->num);
            else w_str(w, s->text);
        }
        return 0;
    case 39 << 8 | 2: return 0;                       /* Localization.SetLocaleCode */
    case 1001 << 8 | 1: w_u32(w, 0); return 0;        /* tracking: user group */
    case 1001 << 8 | 2: w_u32(w, 0); return 0;        /* tracking: the group's tags */
    case 107 << 8 | 1: w_u32(w, (uint32_t)H.unix_time()); return 0;
    case 106 << 8 | 5:                                /* clan of a player: none */
        w_u32(w, 0xFFFFFFFF); w_str(w, ""); w_str(w, ""); w_str(w, "");
        return 0;
    case 106 << 8 | 8: w_u32(w, 0); return 0;         /* a clan's members */
    case 106 << 8 | 9: w_u32(w, 0); return 0;         /* clan challenges */
    case 105 << 8 | 2: w_u32(w, 0); return 0;         /* friend challenges */
    case 35 << 8 | 1:                                 /* privileges: 1 = may play online */
        w_u32(w, 1); w_u32(w, 1); w_u32(w, 1); w_str(w, "PlayOnline");
        return 0;
    case 36 << 8 | 4: w_u32(w, 0); return 0;
    case 5007 << 8 | 1: w_u32(w, 0); return 0;        /* challenges by filter */
    case 5002 << 8 | 1:                               /* the news shown in the menu */
        w_u32(w, 1);
        w_u32(w, 1); w_u32(w, 9); w_u32(w, 2); w_u32(w, 2);
        w_str(w, "SCBLLink");
        w_u64(w, 0); w_u64(w, 0); w_u64(w, 0);
        w_str(w, "SCBLLINK");
        w_str(w, "https://github.com/Mhdshehri/scbllink");
        w_str(w, "A private game with your Steam friends. By Mhdshehri.");
        return 0;
    case 29 << 8 | 12: { /* account names of players */
        uint32_t n = r_u32(r);
        if (r->bad || n > 32) return ERR_INVALID_ARGUMENT;
        int at = w->n;
        uint32_t found = 0;
        w_u32(w, 0);
        for (uint32_t i = 0; i < n; i++) {
            Player *p = player_by_pid(r_u32(r));
            if (r->bad) return ERR_INVALID_ARGUMENT;
            if (!p) continue;
            w_u32(w, p->pid);
            w_str(w, p->name);
            found++;
        }
        if (!w->over) memcpy(w->p + at, &found, 4);
        return 0;
    }
    case 29 << 8 | 11: { /* game account ids -> player numbers */
        uint32_t n = r_u32(r);
        if (r->bad || n > 32) return ERR_INVALID_ARGUMENT;
        int at = w->n;
        uint32_t found = 0;
        w_u32(w, 0);
        for (uint32_t i = 0; i < n; i++) {
            char ubi[80], name[64];
            r_str(r, ubi, sizeof ubi);
            if (r->bad) return ERR_INVALID_ARGUMENT;
            Player *p = 0;
            if (H.ubi_name && H.ubi_name(ubi, name)) p = player_for(name);
            else /* some calls name the player instead */
                for (int k = 0; k < MAX_PLAYERS && !p; k++)
                    if (g_player[k].used && !strcmp(g_player[k].name, ubi)) p = &g_player[k];
            if (!p) continue;
            w_str(w, ubi);
            w_u32(w, p->pid);
            found++;
        }
        if (!w->over) memcpy(w->p + at, &found, 4);
        return 0;
    }
    case 29 << 8 | 14: { /* account ids (or names) -> names */
        uint32_t n = r_u32(r);
        if (r->bad || n > 32) return ERR_INVALID_ARGUMENT;
        int at = w->n;
        uint32_t found = 0;
        w_u32(w, 0);
        for (uint32_t i = 0; i < n; i++) {
            char ubi[80], name[64];
            r_str(r, ubi, sizeof ubi);
            if (r->bad) return ERR_INVALID_ARGUMENT;
            bool ok = H.ubi_name && H.ubi_name(ubi, name);
            for (int k = 0; k < MAX_PLAYERS && !ok; k++)
                if (g_player[k].used && !strcmp(g_player[k].name, ubi)) { sfmt(name, sizeof name, "%s", ubi); ok = true; }
            if (!ok) continue;
            w_str(w, ubi);
            w_str(w, name);
            found++;
        }
        if (!w->over) memcpy(w->p + at, &found, 4);
        return 0;
    }
    case 3 << 8 | 1: return 0; /* NAT probes between players: on one network, none needed */
    case 3 << 8 | 3: return probe_ask(c, r);
    case 55 << 8 | 1: return do_write_stats(c, r);
    case 29 << 8 | 22: w_u8(w, 1); w_u32(w, 0); return 0; /* the terms are accepted */
    case 49 << 8 | 2: case 49 << 8 | 6: case 49 << 8 | 7: case 49 << 8 | 9: w_u32(w, 0); return 0; /* Uplay actions and rewards: none */
    case 53 << 8 | 1: { /* files of a kind */
        uint32_t type = r_u32(r);
        if (r->bad) return ERR_INVALID_ARGUMENT;
        if (type != TYPE_BALANCING) { w_u32(w, 0); return 0; }
        w_u32(w, 1);
        w_u32(w, TYPE_BALANCING); w_u64(w, 1); /* its key */
        w_u32(w, PID_FILES);
        w_u32(w, 4);
        w_u32(w, 6); w_u8(w, 1); w_u64(w, sizeof BALANCING - 1); /* size */
        w_u32(w, 4); w_u8(w, 5); w_u64(w, 135133060054ull);      /* made */
        w_u32(w, 5); w_u8(w, 5); w_u64(w, 135133074425ull);      /* changed */
        w_u32(w, 7); w_u8(w, 4); w_str(w, "A6E32CFD0C2B2CFFE2D0C785830B7C49");
        return 0;
    }
    case 53 << 8 | 9: { /* where a file is */
        uint32_t type = r_u32(r);
        if (r->bad || type != TYPE_BALANCING) return ERR_INVALID_ARGUMENT;
        w_files_host(w);
        w_str(w, "/mp_balancing.ini");
        return 0;
    }
    case 53 << 8 | 6: { /* the game wants to upload its file */
        char path[64];
        g_pending++;
        sfmt(path, sizeof path, "/ugc/%u-%u", (unsigned)c->pid, (unsigned)g_pending);
        w_files_host(w);
        w_str(w, path);
        w_u64(w, g_pending);
        w_u32(w, 0);
        return 0;
    }
    case 53 << 8 | 7: w_u32(w, TYPE_PLAYER_FILE); w_u64(w, c->pid); return 0; /* upload done */
    case 55 << 8 | 2: return do_stats(r, w);
    case 42 << 8 | 21: { /* RegisterURLs: where this game is reached */
        Player *p = player_by_pid(c->pid);
        uint32_t n = r_u32(r);
        if (r->bad || n > 16) return ERR_INVALID_ARGUMENT;
        char url[URL_LEN];
        int k = 0;
        for (uint32_t i = 0; i < n; i++) {
            r_str(r, url, sizeof url);
            if (r->bad) return ERR_INVALID_ARGUMENT;
            if (p && k < MAX_URLS) sfmt(p->url[k++], URL_LEN, "%s", url);
        }
        if (p) p->nurl = k;
        return 0;
    }
    case 42 << 8 | 24: return search_with(c, r, w);
    case 42 << 8 | 22: { /* JoinSession */
        uint32_t type = r_u32(r), id = r_u32(r);
        Session *s = session_get(type, id);
        if (r->bad || !s) return ERR_INVALID_ARGUMENT;
        if (room_kind(s) == 0 && !session_has(s, c->pid) && !invite_find(s->owner, c->pid)) return ERR_ACCESS_DENIED;
        if (!session_has(s, c->pid)) session_add(s, c->pid, false);
        return 0;
    }
    case 42 << 8 | 28: { /* SplitSession: a new session like the one given, of the caller's own */
        uint32_t type = r_u32(r), id = r_u32(r);
        if (r->bad) return ERR_INVALID_ARGUMENT;
        Session *old = session_get(type, id), *s = 0;
        for (int i = 0; i < MAX_SESSIONS && !s; i++)
            if (!g_session[i].used) s = &g_session[i];
        if (!s) return ERR_INVALID_ARGUMENT;
        if (old) *s = *old;
        else { memset(s, 0, sizeof *s); s->type = type; }
        s->used = true;
        s->id = g_next_session++;
        s->owner = c->pid;
        s->npart = 0;
        if (old && old->owner == c->pid) old->used = false;
        w_u32(w, s->type);
        w_u32(w, s->id);
        return 0;
    }
    case 42 << 8 | 30: { /* ReportUnsuccessfulJoinSessions */
        uint32_t n = r_u32(r);
        for (uint32_t i = 0; i < n && i < 8 && !r->bad; i++) {
            uint32_t type = r_u32(r), id = r_u32(r), cat = r_u32(r), code = r_u32(r);
            (void)type;
            if (!r->bad) say("server: player %u couldn't join room %u (error %u/%08x)", (unsigned)c->pid, (unsigned)id, (unsigned)cat, (unsigned)code);
        }
        return 0;
    }
    case 42 << 8 | 1: { /* CreateSession */
        Session *s = 0;
        for (int i = 0; i < MAX_SESSIONS && !s; i++)
            if (!g_session[i].used) s = &g_session[i];
        if (!s) return ERR_INVALID_ARGUMENT;
        memset(s, 0, sizeof *s);
        s->type = r_u32(r);
        if (!r_attrs(r, s)) return ERR_INVALID_ARGUMENT;
        s->used = true;
        s->id = g_next_session++;
        s->owner = c->pid;
        w_u32(w, s->type);
        w_u32(w, s->id);
        return 0;
    }
    case 42 << 8 | 2: { /* UpdateSession */
        uint32_t type = r_u32(r), id = r_u32(r);
        Session *s = session_get(type, id);
        if (r->bad || !s || s->owner != c->pid || !r_attrs(r, s)) return ERR_INVALID_ARGUMENT;
        return 0;
    }
    case 42 << 8 | 8: { /* AddParticipants: public list, private list */
        uint32_t type = r_u32(r), id = r_u32(r);
        Session *s = session_get(type, id);
        if (r->bad || !s) return ERR_INVALID_ARGUMENT;
        for (int priv = 0; priv < 2; priv++) {
            uint32_t n = r_u32(r);
            if (r->bad || n > MAX_PARTS) return ERR_INVALID_ARGUMENT;
            for (uint32_t i = 0; i < n; i++) {
                uint32_t pid = r_u32(r);
                if (r->bad) return ERR_INVALID_ARGUMENT;
                /* Into a private match's room only who belongs there: its owner, someone the owner
                   invited, or a member of the owner's anteroom (the party). Each one, the owner
                   adding itself included, gets the 7003 join gate. */
                if (room_kind(s) == 0 && !may_enter(s, c->pid, pid)) {
                    say("server: player %u may not add %u to room %u; refused", (unsigned)c->pid, (unsigned)pid, (unsigned)s->id);
                    return ERR_ACCESS_DENIED;
                }
                session_add(s, pid, priv);
                if (room_kind(s) == 0) notify_join_gate(pid, c->pid, s->id);
            }
        }
        return 0;
    }
    case 42 << 8 | 3: case 42 << 8 | 5: case 42 << 8 | 23: { /* Delete, Leave, Abandon */
        uint32_t type = r_u32(r), id = r_u32(r);
        Session *s = session_get(type, id);
        if (r->bad) return ERR_INVALID_ARGUMENT;
        if (!s) return 0;
        if (method == 3 && s->owner == c->pid) s->used = false;
        else session_leave(s, c->pid);
        return 0;
    }
    }
    return ERR_NOT_IMPLEMENTED;
}

/* One whole message from the game: size, protocol, call id, method, values. */
static void message(Conn *c, const uint8_t *m, int n)
{
    static uint8_t out[MAX_MSG];
    R r = { m, n, 0, false };
    uint32_t size = r_u32(&r);
    uint32_t proto = r_u8(&r);
    if (r.bad || size != (uint32_t)n - 4 || !(proto & 0x80)) return; /* only requests come to a server */
    proto &= 0x7F;
    if (proto == 0x7F) proto = r_u16(&r);
    uint32_t id = r_u32(&r), method = r_u32(&r);
    if (r.bad) return;
    W w = { out, 0, sizeof out, false };
    w_u32(&w, 0);
    if (proto < 0x7F) w_u8(&w, (uint8_t)proto);
    else { w_u8(&w, 0x7F); w_u16(&w, (uint16_t)proto); }
    int head = w.n;
    w_u8(&w, 1);
    w_u32(&w, id);
    w_u32(&w, method | 0x8000);
    uint32_t err = call(c, proto, method, &r, &w);
    if (!err && w.over) err = ERR_INVALID_ARGUMENT;
    if (err) {
        w.n = head;
        w.over = false;
        w_u8(&w, 0);
        w_u32(&w, err);
        w_u32(&w, id);
        if (err == ERR_NOT_IMPLEMENTED) {
            char hex[161];
            int k = n - r.o > 80 ? 80 : n - r.o;
            for (int i = 0; i < k; i++) sfmt(hex + 2 * i, 3, "%02x", m[r.o + i]);
            hex[2 * k] = 0;
            say("server: ! call %u.%u from player %u is not built yet (%d bytes: %s)", (unsigned)proto, (unsigned)method, (unsigned)c->pid, n - r.o, hex);
        } else say("server: call %u.%u from player %u refused (%08x)", (unsigned)proto, (unsigned)method, (unsigned)c->pid, (unsigned)err);
    } else if (H.verbose) say("server: call %u.%u from player %u, %d bytes back", (unsigned)proto, (unsigned)method, (unsigned)c->pid, w.n);
    uint32_t len = (uint32_t)w.n - 4;
    memcpy(out, &len, 4);
    send_message(c, out, w.n);
}

/* The secure port's CONNECT: { u32 n, token } { u32 n, RC4(session key){ pid, connection id, challenge } }.
   The answer is { u32 4, challenge + 1 }. */
static bool secure_connect(Conn *c, const uint8_t *payload, int n, uint8_t *answer, int *answer_n)
{
    uint8_t b[256];
    if (n < 2 || n > (int)sizeof b) return false;
    memcpy(b, payload, (size_t)n);
    rc4(STREAM_KEY, sizeof STREAM_KEY - 1, b, (size_t)n);
    if (b[0] != 0) return false; /* small enough that the game never packs it */
    R r = { b + 1, n - 1, 0, false };
    uint32_t tn = r_u32(&r);
    const uint8_t *token = r_take(&r, (int)tn);
    uint32_t qn = r_u32(&r);
    const uint8_t *q = r_take(&r, (int)qn);
    if (r.bad || tn != TOKEN || qn < 12 || qn > 64) return false;
    for (int i = 0; i < MAX_TICKETS; i++) {
        Ticket *t = &g_ticket[i];
        if (!t->used || memcmp(t->token, token, TOKEN) != 0) continue;
        if (g_now - t->made > TICKET_MS) return false;
        uint8_t req[64];
        memcpy(req, q, qn);
        rc4(t->key, 16, req, qn);
        R x = { req, (int)qn, 0, false };
        uint32_t pid = r_u32(&x);
        r_u32(&x);
        uint32_t challenge = r_u32(&x);
        if (pid != t->pid) return false;
        t->used = false; /* a ticket opens one connection */
        c->pid = pid;
        W w = { answer, 0, 16, false };
        w_u8(&w, 0);
        w_u32(&w, 4);
        w_u32(&w, challenge + 1);
        rc4(STREAM_KEY, sizeof STREAM_KEY - 1, answer, (size_t)w.n);
        *answer_n = w.n;
        return true;
    }
    return false;
}

static void data(Conn *c, int frag, const uint8_t *payload, int n)
{
    static uint8_t plain[MAX_PACKET], piece[8192];
    if (n < 1 || n > MAX_PACKET) return;
    memcpy(plain, payload, (size_t)n);
    rc4(STREAM_KEY, sizeof STREAM_KEY - 1, plain, (size_t)n);
    const uint8_t *p = plain + 1;
    int k = n - 1;
    if (plain[0]) { /* packed */
        k = zlib_inflate(plain + 1, n - 1, piece, sizeof piece);
        if (k < 0) { say("server: a packet from player %u could not be unpacked", (unsigned)c->pid); c->msg_len = 0; return; }
        p = piece;
    }
    if (k > MAX_MSG - c->msg_len) { c->msg_len = 0; return; }
    if (!c->msg) c->msg = malloc(MAX_MSG);
    if (!c->msg) return;
    memcpy(c->msg + c->msg_len, p, (size_t)k);
    c->msg_len += k;
    if (frag == 0) { /* the last piece */
        int len = c->msg_len;
        c->msg_len = 0;
        message(c, c->msg, len);
    }
}

void srv_packet(int which, uint32_t ip, uint16_t port, const uint8_t *d, int n, uint64_t now_ms)
{
    if (!g_on || n < 11 || n > MAX_PACKET) return;
    g_now = now_ms;
    if (which == SRV_SECURE && n == 26 && d[0] == 0x71 && d[1] == 0x71) {
        /* the game's NAT echo, from its peer socket: its 15 bytes back, and the address it was
           seen at (on one network: the one it has) */
        uint8_t out[96];
        char ip_s[16], url[64];
        ip_text(ip, ip_s);
        sfmt(url, sizeof url, "udp:/address=%s;port=%u", ip_s, (unsigned)port);
        int ul = (int)strlen(url) + 1;
        memcpy(out, d + 10, 15);
        memcpy(out + 15, url, (size_t)ul);
        H.send(which, ip, port, out, 15 + ul);
        return;
    }
    if (d[1] != ME || (d[0] & 0xF0) != 0x30 || checksum(d, n - 1) != d[n - 1]) return;
    int type = d[2] & 7, flags = d[2] >> 3;
    R r = { d, n - 1, 3, false };
    uint8_t sess = r_u8(&r);
    uint32_t sig = r_u32(&r);
    uint16_t seq = r_u16(&r);
    Conn *c = conn_find(which, ip, port, d[0]);

    if (type == T_SYN) {
        if (flags & F_ACK) return;
        if (c && !c->connected) { /* the game sends its SYN a few times: each gets the same answer */
            c->last = g_now;
            reply(c, T_SYN, 0, c->sig_mine, 0, 0);
            return;
        }
        if (c) conn_close(c, "connecting again");
        c = conn_new(which, ip, port, d[0]);
        reply(c, T_SYN, 0, c->sig_mine, 0, 0);
        return;
    }
    if (!c) return;
    if (type == T_CONNECT) {
        uint32_t theirs = r_u32(&r);
        if (r.bad || (flags & F_ACK) || sig != c->sig_mine) return;
        uint8_t answer[16];
        int an = 0;
        if (!c->connected) {
            c->sig_peer = theirs;
            c->sess_peer = sess;
            if (which == SRV_SECURE && !secure_connect(c, d + r.o, n - 1 - r.o, answer, &an)) {
                say("server: a connection without a good ticket was turned away");
                conn_free(c);
                return;
            }
            c->connected = true;
            c->in_next = 2;
        } else if (which == SRV_SECURE) return; /* a repeat: the first answer's challenge is gone; the game asks again from SYN */
        c->last = g_now;
        reply(c, T_CONNECT, seq, 0, answer, an);
        return;
    }
    if (!c->connected || sig != c->sig_mine) return;
    c->last = g_now;
    if (type == T_PING) {
        if (!(flags & F_ACK)) reply(c, T_PING, seq, 0, 0, 0);
        return;
    }
    if (type == T_DISCONNECT) {
        if (flags & F_ACK) return;
        reply(c, T_DISCONNECT, seq, 0, 0, 0);
        conn_close(c, "left");
        return;
    }
    if (type != T_DATA) return;
    int frag = r_u8(&r);
    if (r.bad) return;
    if (flags & F_ACK) { /* one of ours arrived */
        for (Out **at = &c->out; *at; at = &(*at)->next)
            if ((*at)->seq == seq) { Out *o = *at; *at = o->next; free(o); break; }
        return;
    }
    int len = n - 1 - r.o;
    if (flags & F_HAS_SIZE) {
        len = r_u16(&r);
        if (r.bad || len > n - 1 - r.o) return;
    }
    int16_t ahead = (int16_t)(seq - c->in_next);
    if (ahead > 0) return; /* one before it is missing: the game sends again */
    uint8_t ack[MAX_PACKET];
    int k = build(c, ack, T_DATA, F_ACK | F_HAS_SIZE, seq, 0, 0, 0);
    if (k) H.send(which, ip, port, ack, k);
    if (ahead < 0) return; /* had it already */
    c->in_next++;
    data(c, frag, d + r.o, len);
}

void srv_invite(const char *from_name, const char *to_name, uint64_t now_ms)
{
    if (!g_on) return;
    g_now = now_ms;
    Player *a = player_for(from_name), *b = player_for(to_name);
    if (!a || !b) return;
    Invite *v = &g_invite[0];
    for (int i = 0; i < MAX_INVITES; i++) {
        if (!g_invite[i].used) { v = &g_invite[i]; break; }
        if (g_invite[i].made < v->made) v = &g_invite[i];
    }
    v->used = true;
    v->from = a->pid;
    v->to = b->pid;
    v->made = now_ms;
    say("server: %s invited %s", from_name, to_name);
}

void srv_tick(uint64_t now_ms)
{
    if (!g_on) return;
    g_now = now_ms;
    for (int i = 0; i < MAX_CONNS; i++) {
        Conn *c = &g_conn[i];
        if (!c->used) continue;
        if (now_ms - c->last > IDLE_MS) { conn_close(c, "silent"); continue; }
        for (Out *o = c->out; o; o = o->next) {
            if (now_ms - o->sent < RESEND_MS) continue;
            if (o->tries >= RESEND_TRIES) { conn_close(c, "not answering"); break; }
            o->tries++;
            o->sent = now_ms;
            H.send(c->which, c->ip, c->port, o->d, o->n);
        }
    }
}

void srv_start(const SrvHost *h)
{
    srv_stop();
    H = *h;
    uint8_t r[4];
    H.random(r, 4);
    g_next_cid = 0x10000000u | (uint32_t)r[0] << 16 | (uint32_t)r[1] << 8 | r[2];
    g_next_session = 1;
    g_pending = 0;
    g_on = true;
}

void srv_stop(void)
{
    for (int i = 0; i < MAX_CONNS; i++)
        if (g_conn[i].used) conn_free(&g_conn[i]);
    memset(g_player, 0, sizeof g_player);
    memset(g_ticket, 0, sizeof g_ticket);
    memset(g_session, 0, sizeof g_session);
    memset(g_invite, 0, sizeof g_invite);
    g_nstat = 0;
    g_on = false;
}

int srv_http_get(int port, const char *path, uint8_t *out, int cap)
{
    if (port == SRV_PORT_CONFIG) {
        if (strncmp(path, "/OnlineConfigService.svc/GetOnlineConfig", 40) != 0) return -1;
        char ip[16], body[200];
        ip_text(H.ip, ip);
        sfmt(body, sizeof body, "[{\"Name\":\"SandboxUrl\",\"Values\":[\"prudp:/address=%s;port=%d\"]},{\"Name\":\"SandboxUrlWS\",\"Values\":[\"%s\"]}]", ip, SRV_PORT_AUTH, ip);
        int n = (int)strlen(body);
        if (n > cap) return -1;
        memcpy(out, body, (size_t)n);
        return n;
    }
    if (port == SRV_PORT_FILES && !strcmp(path, "/mp_balancing.ini")) {
        int n = sizeof BALANCING - 1;
        if (n > cap) return -1;
        memcpy(out, BALANCING, (size_t)n);
        return n;
    }
    return -1;
}
