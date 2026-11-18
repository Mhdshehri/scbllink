/* link.c - the pipe to SCBLLink, see link.h. Everything that comes in is
 * checked: a line too long, a field too long or with control characters, a
 * bad hex block - and it's dropped. By Mhdshehri. */
#include "link.h"

void overlay_invite(const char *from);
void overlay_invite_clear(void);

#define PIPE_NAME "\\\\.\\pipe\\scbllink-game"
#define LINE_MAX 4096

static CRITICAL_SECTION g_cs;
static bool g_cs_ready;
static HANDLE g_pipe = INVALID_HANDLE;
static volatile bool g_stop;

static bool g_have_account;
static char g_name[SL_NAME], g_pass[SL_PASS], g_id[SL_ID];
static uint32_t g_ip;
static uint32_t g_server; /* the host's server, 0 = not told yet (then 10.237.51.1) */

static SlFriend g_friends[SL_MAX_FRIENDS], g_next[SL_MAX_FRIENDS];
static int g_nfriends, g_nnext;
static uint32_t g_friends_version;
static bool g_in_list;

static SlEvent g_events[16];
static int g_ev_head, g_ev_count;

static char g_dir[512];
static bool g_second; /* started with -scbllink2: the second game on a developer's PC */
static volatile bool g_bind; /* bind the game's sockets to this PC's SCBLLink address (two games on one PC) */
static FILE *g_log;

static void lock(void) { EnterCriticalSection(&g_cs); }
static void unlock(void) { LeaveCriticalSection(&g_cs); }

const char *sl_dir(void) { return g_dir; }

void sl_log(const char *fmt, ...)
{
    if (!g_log) return;
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(line, sizeof line - 3, fmt, ap);
    va_end(ap);
    line[sizeof line - 3] = 0;
    WORD st[8];
    GetLocalTime(st);
    char t[16];
    sfmt(t, sizeof t, "%02u:%02u:%02u ", st[4], st[5], st[6]);
    if (g_cs_ready) lock();
    fwrite(t, 1, strlen(t), g_log);
    fwrite(line, 1, strlen(line), g_log);
    fwrite("\r\n", 1, 2, g_log);
    fflush(g_log);
    if (g_cs_ready) unlock();
}

void sl_send(const char *fmt, ...)
{
    char line[2048]; /* the longest line (a session) is about 1030; no stack probes in this DLL */
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(line, sizeof line - 2, fmt, ap);
    va_end(ap);
    line[sizeof line - 2] = 0;
    size_t n = strlen(line);
    line[n++] = '\n';
    lock();
    HANDLE p = g_pipe;
    DWORD put = 0;
    bool ok = p != INVALID_HANDLE && WriteFile(p, line, (DWORD)n, &put, 0) && put == n;
    unlock();
    if (!ok && p != INVALID_HANDLE) sl_log("! could not write to SCBLLink");
}

/* ---------- parsing ---------- */

static int split(char *line, char **f, int max)
{
    int n = 0;
    char *s = line;
    while (n < max) {
        f[n++] = s;
        char *t = strchr(s, '\t');
        if (!t) break;
        *t = 0;
        s = t + 1;
    }
    return n;
}

/* a text field we keep: printable, at most n-1 bytes */
static bool take(char *dst, int n, const char *src)
{
    int l = (int)strlen(src);
    if (l >= n) return false;
    for (int i = 0; i < l; i++)
        if ((unsigned char)src[i] < 0x20 || src[i] == 0x7f) return false;
    memcpy(dst, src, (size_t)l + 1);
    return true;
}

static int hexv(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool parse_ip(const char *s, uint32_t *out)
{
    unsigned v[4];
    int k = 0;
    for (int i = 0; i < 4; i++) {
        unsigned x = 0;
        int d = 0;
        while (s[k] >= '0' && s[k] <= '9' && d < 3) { x = x * 10 + (unsigned)(s[k++] - '0'); d++; }
        if (d == 0 || x > 255) return false;
        v[i] = x;
        if (i < 3 && s[k++] != '.') return false;
    }
    if (s[k]) return false;
    *out = v[0] | v[1] << 8 | v[2] << 16 | v[3] << 24; /* network order in memory */
    return true;
}

static void on_line(char *line)
{
    char *f[8];
    int n = split(line, f, 8);
    if (!strcmp(f[0], "ACCOUNT") && n == 6) {
        char name[SL_NAME], pass[SL_PASS], id[SL_ID];
        uint32_t ip, server;
        if (!take(name, sizeof name, f[1]) || !take(pass, sizeof pass, f[2]) || !take(id, sizeof id, f[3]) || !parse_ip(f[4], &ip) ||
            !parse_ip(f[5], &server)) {
            sl_log("! bad ACCOUNT line");
            return;
        }
        lock();
        memcpy(g_name, name, sizeof name);
        memcpy(g_pass, pass, sizeof pass);
        memcpy(g_id, id, sizeof id);
        g_ip = ip;
        g_server = server;
        g_have_account = true;
        unlock();
        sl_log("account %s (%s), this PC is %s, the server %s", name, id, f[4], f[5]);
    } else if (!strcmp(f[0], "BIND") && n == 1) {
        if (!g_bind) sl_log("sockets: bound to this game's own address (two games on this PC)");
        g_bind = true;
    } else if (!strcmp(f[0], "FRIENDS") && n == 1) {
        g_in_list = true;
        g_nnext = 0;
    } else if (!strcmp(f[0], "F") && n == 6 && g_in_list) {
        if (g_nnext >= SL_MAX_FRIENDS) return;
        SlFriend *x = &g_next[g_nnext];
        memset(x, 0, sizeof *x);
        if (!take(x->id, sizeof x->id, f[1]) || !take(x->name, sizeof x->name, f[2]) || !x->id[0] || !x->name[0]) { sl_log("! bad friend line"); return; }
        x->online = f[3][0] == '1';
        uint32_t sid = 0;
        for (const char *c = f[4]; *c; c++) {
            if (*c < '0' || *c > '9' || sid > 400000000u) { sl_log("! bad session id"); return; }
            sid = sid * 10 + (uint32_t)(*c - '0');
        }
        x->session = sid;
        if (strcmp(f[5], "-") != 0) {
            if (strlen(f[5]) != SL_SESSION_DATA * 2) { sl_log("! bad session data"); return; }
            for (int i = 0; i < SL_SESSION_DATA; i++) {
                int a = hexv(f[5][2 * i]), b = hexv(f[5][2 * i + 1]);
                if (a < 0 || b < 0) { sl_log("! bad session data"); return; }
                x->data[i] = (uint8_t)(a << 4 | b);
            }
            x->has_data = true;
        }
        g_nnext++;
    } else if (!strcmp(f[0], "END") && n == 1 && g_in_list) {
        g_in_list = false;
        lock();
        memcpy(g_friends, g_next, sizeof g_next);
        g_nfriends = g_nnext;
        g_friends_version++;
        unlock();
        sl_log("friends: %d", g_nnext);
    } else if ((!strcmp(f[0], "ACCEPT") || !strcmp(f[0], "REFRESH") || !strcmp(f[0], "INVITE")) && n <= 2) {
        SlEvent e;
        memset(&e, 0, sizeof e);
        if (!strcmp(f[0], "REFRESH")) e.type = SL_EV_FRIENDS_CHANGED;
        else {
            if (n != 2 || !take(e.name, sizeof e.name, f[1]) || !e.name[0]) { sl_log("! bad %s line", f[0]); return; }
            if (!strcmp(f[0], "INVITE")) { sl_log("invitation from %s", e.name); overlay_invite(e.name); return; }
            e.type = SL_EV_INVITE_ACCEPTED;
        }
        lock();
        if (g_ev_count < 16) {
            g_events[(g_ev_head + g_ev_count) % 16] = e;
            g_ev_count++;
        }
        unlock();
        sl_log("event %d %s", e.type, e.name);
        if (e.type == SL_EV_INVITE_ACCEPTED) overlay_invite_clear(); /* accepted in SCBLLink's window */
    } else {
        sl_log("! unknown line from SCBLLink (%s)", f[0]);
    }
}

/* ---------- the pipe ---------- */

static bool connect_pipe(void)
{
    HANDLE p = CreateFileA(g_second ? PIPE_NAME "-2" : PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
    if (p == INVALID_HANDLE) return false;
    lock();
    g_pipe = p;
    unlock();
    sl_send("HELLO\t1\t%lu", GetCurrentProcessId());
    sl_log("linked to SCBLLink");
    return true;
}

static DWORD WINAPI reader(void *arg)
{
    static char buf[LINE_MAX];
    int n = 0;
    bool skipping = false;
    while (!g_stop) {
        if (g_pipe == INVALID_HANDLE) {
            if (!connect_pipe()) { Sleep(1000); continue; }
        }
        /* a plain (synchronous) pipe handle does one operation at a time: a ReadFile waiting
           for data would hold up the game's writes. So only read what's already there. */
        char c[512];
        DWORD got = 0, avail = 0;
        bool alive = PeekNamedPipe(g_pipe, 0, 0, 0, &avail, 0);
        if (alive && avail == 0) { Sleep(30); continue; }
        if (!alive || !ReadFile(g_pipe, c, avail < sizeof c ? avail : sizeof c, &got, 0) || got == 0) {
            sl_log("SCBLLink went away");
            lock();
            CloseHandle(g_pipe);
            g_pipe = INVALID_HANDLE;
            unlock();
            n = 0;
            skipping = false;
            Sleep(1000);
            continue;
        }
        for (DWORD i = 0; i < got; i++) {
            if (c[i] == '\n') {
                if (!skipping) { buf[n] = 0; if (n > 0 && buf[n - 1] == '\r') buf[n - 1] = 0; on_line(buf); }
                n = 0;
                skipping = false;
            } else if (!skipping) {
                if (n < LINE_MAX - 1) buf[n++] = c[i];
                else { skipping = true; sl_log("! line too long, dropped"); }
            }
        }
    }
    return 0;
}

/* when the DLL loads: no threads, no waiting (the loader lock is held) */
void sl_init(void)
{
    if (!g_cs_ready) { InitializeCriticalSection(&g_cs); g_cs_ready = true; }
    GetModuleFileNameA(0, g_dir, sizeof g_dir);
    char *s = g_dir + strlen(g_dir);
    while (s > g_dir && s[-1] != '\\') s--;
    *s = 0;
    char path[600];
    const char *cl = GetCommandLineA();
    g_second = cl && strstr(cl, "-scbllink2") != 0;
    if (g_second) g_bind = true;
    sfmt(path, sizeof path, g_second ? "%sscbllink-game-2.log" : "%sscbllink-game.log", g_dir);
    g_log = fopen(path, "wb");
    sl_log("SCBLLink game DLL. By Mhdshehri.%s", g_second ? " The SECOND game on this PC (developer test)." : "");
}

void sl_start(void)
{
    static bool started;
    if (started) return;
    started = true;
    connect_pipe();
    CreateThread(0, 0, reader, 0, 0, 0);
    /* the game asks for the account right after startup: wait for it a moment */
    for (int i = 0; i < 30 && !sl_account(0, 0, 0); i++) Sleep(100);
    if (!sl_account(0, 0, 0)) sl_log("! no account from SCBLLink: the game stays offline (single player works)");
}

void sl_stop(void)
{
    sl_send("BYE");
    g_stop = true;
}

bool sl_linked(void) { return g_pipe != INVALID_HANDLE; }
bool sl_second(void) { return g_second; }
bool sl_bind_own(void) { return g_bind; }

bool sl_account(char *name, char *pass, char *id)
{
    lock();
    bool ok = g_have_account;
    if (ok) {
        if (name) memcpy(name, g_name, SL_NAME);
        if (pass) memcpy(pass, g_pass, SL_PASS);
        if (id) memcpy(id, g_id, SL_ID);
    }
    unlock();
    return ok;
}

/* the host's server as text ("10.237.51.1" until SCBLLink says) */
void sl_server_text(char out[16])
{
    lock();
    uint32_t ip = g_server;
    unlock();
    const uint8_t *b = (const uint8_t *)&ip;
    if (ip) sfmt(out, 16, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    else sfmt(out, 16, "10.237.51.1");
}

uint32_t sl_my_ip(void)
{
    lock();
    uint32_t ip = g_ip;
    unlock();
    return ip;
}

int sl_friends(SlFriend *out, int max, uint32_t *version)
{
    lock();
    int n = g_nfriends < max ? g_nfriends : max;
    memcpy(out, g_friends, sizeof(SlFriend) * (size_t)n);
    if (version) *version = g_friends_version;
    unlock();
    return n;
}

bool sl_next_event(SlEvent *e)
{
    lock();
    bool ok = g_ev_count > 0;
    if (ok) {
        *e = g_events[g_ev_head];
        g_ev_head = (g_ev_head + 1) % 16;
        g_ev_count--;
    }
    unlock();
    return ok;
}

static void queue(int type, const char *name)
{
    SlEvent e;
    memset(&e, 0, sizeof e);
    e.type = type;
    if (name) sfmt(e.name, sizeof e.name, "%s", name);
    lock();
    if (g_ev_count < 16) {
        g_events[(g_ev_head + g_ev_count) % 16] = e;
        g_ev_count++;
    }
    unlock();
}

/* from the overlay: F5 on an invitation */
void sl_accept(const char *name)
{
    queue(SL_EV_INVITE_ACCEPTED, name);
    sl_send("ACCEPTED\t%s", name);
    sl_log("accepted %s's invitation (overlay)", name);
}

/* from the overlay: F6 in the panel */
void sl_refresh(void)
{
    queue(SL_EV_FRIENDS_CHANGED, 0);
    sl_log("refresh (overlay)");
}
