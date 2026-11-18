/* hook.c - points the game at the host's SCBLLink server and makes it advertise
 * this PC's SCBLLink address, without touching the game's own code:
 *   getaddrinfo / gethostbyname ("onlineconfigservice.ubi.com") -> 10.237.51.1
 *   gethostbyname(this PC's name), GetAdaptersInfo -> only the SCBLLink address
 * Done twice over, so neither way of calling is missed: the game's import table
 * entries, and (when the function starts with the Windows hot-patch prologue)
 * the function itself. By Mhdshehri. */
#include "link.h"

#define CONFIG_HOST "onlineconfigservice.ubi.com"

typedef int(WINAPI *getaddrinfo_fn)(const char *, const char *, const void *, void **);
typedef HOSTENT *(WINAPI *gethostbyname_fn)(const char *);
typedef DWORD(WINAPI *GetAdaptersInfo_fn)(IP_ADAPTER_INFO *, DWORD *);
typedef int(WINAPI *bind_fn)(uintptr_t, const void *, int);
typedef HANDLE(WINAPI *CreateMutexA_fn)(void *, BOOL, const char *);
typedef HANDLE(WINAPI *CreateMutexW_fn)(void *, BOOL, const uint16_t *);
typedef HANDLE(WINAPI *OpenMutexA_fn)(DWORD, BOOL, const char *);
typedef HANDLE(WINAPI *OpenMutexW_fn)(DWORD, BOOL, const uint16_t *);

static getaddrinfo_fn real_getaddrinfo;
static gethostbyname_fn real_gethostbyname;
static GetAdaptersInfo_fn real_GetAdaptersInfo;
static bind_fn real_bind;
static CreateMutexA_fn real_CreateMutexA;
static CreateMutexW_fn real_CreateMutexW;
static OpenMutexA_fn real_OpenMutexA;
static OpenMutexW_fn real_OpenMutexW;

static bool same_name(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return false;
    }
    return *a == *b;
}

static void ip_text(uint32_t ip, char *out)
{
    const uint8_t *b = (const uint8_t *)&ip;
    sfmt(out, 16, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
}

static int WINAPI my_getaddrinfo(const char *node, const char *service, const void *hints, void **res)
{
    if (node && same_name(node, CONFIG_HOST)) {
        char server[16];
        sl_server_text(server);
        sl_log("getaddrinfo(%s, %s) -> %s", node, service ? service : "-", server);
        return real_getaddrinfo(server, service, hints, res);
    }
    sl_log("getaddrinfo(%s, %s)", node ? node : "-", service ? service : "-");
    return real_getaddrinfo(node, service, hints, res);
}

static HOSTENT *WINAPI my_gethostbyname(const char *name)
{
    if (name && same_name(name, CONFIG_HOST)) {
        char server[16];
        sl_server_text(server);
        sl_log("gethostbyname(%s) -> %s", name, server);
        return real_gethostbyname(server);
    }
    HOSTENT *h = real_gethostbyname(name);
    char me[256];
    uint32_t ip = sl_my_ip();
    if (!h || !name || !ip || gethostname(me, sizeof me) != 0 || !same_name(name, me)) {
        sl_log("gethostbyname(%s)", name ? name : "-");
        return h;
    }
    /* this PC's own name: keep only the SCBLLink address, the one friends can reach */
    for (char **a = h->h_addr_list; a && *a; a++) {
        uint32_t v;
        memcpy(&v, *a, 4);
        if (v == ip) {
            h->h_addr_list[0] = *a;
            h->h_addr_list[1] = 0;
            char t[16];
            ip_text(ip, t);
            sl_log("gethostbyname(this PC) -> %s only", t);
            return h;
        }
    }
    sl_log("! gethostbyname(this PC): the SCBLLink address isn't there yet");
    return h;
}

static DWORD WINAPI my_GetAdaptersInfo(IP_ADAPTER_INFO *info, DWORD *size)
{
    DWORD r = real_GetAdaptersInfo(info, size);
    uint32_t ip = sl_my_ip();
    if (r != 0 || !info || !ip) return r;
    char want[16];
    ip_text(ip, want);
    for (IP_ADAPTER_INFO *a = info; a; a = a->next)
        for (IP_ADDR_STRING *s = &a->ip_list; s; s = s->next)
            if (!strcmp(s->ip, want)) {
                if (a != info) {
                    /* the whole entry to the front of the caller's buffer (entries lie one after
                       another there, so the first one's length is where the second begins) */
                    size_t len = info->next ? (size_t)((char *)info->next - (char *)info) : 0;
                    if (len < sizeof *info || len > 2048) len = 640; /* IP_ADAPTER_INFO on 32-bit Windows */
                    memmove(info, a, len);
                }
                info->next = 0; /* the others gone */
                /* this PC's address first in the adapter's list (a second game on this PC has two) */
                IP_ADDR_STRING *first = &info->ip_list;
                for (IP_ADDR_STRING *t = first->next; t; t = t->next)
                    if (!strcmp(t->ip, want)) {
                        char ta[16], tm[16];
                        memcpy(ta, first->ip, 16); memcpy(tm, first->mask, 16);
                        memcpy(first->ip, t->ip, 16); memcpy(first->mask, t->mask, 16);
                        memcpy(t->ip, ta, 16); memcpy(t->mask, tm, 16);
                        break;
                    }
                static bool told;
                if (!told) { sl_log("GetAdaptersInfo -> only %s (%s)", want, info->description); told = true; }
                return r;
            }
    sl_log("! GetAdaptersInfo: no adapter with %s", want);
    return r;
}

/* Two games on one PC (developer test): each binds its sockets to its own SCBLLink address,
   so both can have the game's ports. Only sockets bound to "any address" are moved. */
static int WINAPI my_bind(uintptr_t s, const void *addr, int len)
{
    uint32_t ip = sl_my_ip();
    if (sl_bind_own() && ip && addr && len >= (int)sizeof(SOCKADDR_IN)) {
        SOCKADDR_IN a;
        memcpy(&a, addr, sizeof a);
        if (a.family == 2 && a.addr == 0) {
            a.addr = ip;
            int r = real_bind(s, &a, (int)sizeof a);
            sl_log("bind port %u -> this game's own address: %s", (unsigned)((a.port >> 8) | ((a.port & 0xFF) << 8)), r == 0 ? "ok" : "FAILED");
            return r;
        }
    }
    return real_bind(s, addr, len);
}

/* The second game: its named mutexes get their own names, so a check for "already running"
   doesn't see the first game. */
static void name2(const char *n, char *out, int cap) { sfmt(out, cap, "%s-scbl2", n); }
static void name2w(const uint16_t *n, uint16_t *out, int cap)
{
    int i = 0;
    for (; n[i] && i < cap - 7; i++) out[i] = n[i];
    const char *x = "-scbl2";
    for (int k = 0; x[k]; k++) out[i++] = (uint16_t)x[k];
    out[i] = 0;
}
static HANDLE WINAPI my_CreateMutexA(void *sa, BOOL own, const char *n)
{
    char b[300];
    if (n && sl_second() && strlen(n) < 280) { name2(n, b, sizeof b); sl_log("mutex %s -> %s", n, b); n = b; }
    return real_CreateMutexA(sa, own, n);
}
static HANDLE WINAPI my_OpenMutexA(DWORD acc, BOOL inh, const char *n)
{
    char b[300];
    if (n && sl_second() && strlen(n) < 280) { name2(n, b, sizeof b); n = b; }
    return real_OpenMutexA(acc, inh, n);
}
static HANDLE WINAPI my_CreateMutexW(void *sa, BOOL own, const uint16_t *n)
{
    uint16_t b[300];
    if (n && sl_second()) { name2w(n, b, 300); sl_log("mutex (wide) renamed for the second game"); n = b; }
    return real_CreateMutexW(sa, own, n);
}
static HANDLE WINAPI my_OpenMutexW(DWORD acc, BOOL inh, const uint16_t *n)
{
    uint16_t b[300];
    if (n && sl_second()) { name2w(n, b, 300); n = b; }
    return real_OpenMutexW(acc, inh, n);
}

/* ---------- putting them in place ---------- */

bool write_code(void *at, const void *bytes, size_t n)
{
    DWORD old;
    if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(at, bytes, n);
    VirtualProtect(at, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, n);
    return true;
}

/* Windows' hot-patch shape: 5 spare bytes (int3 or nop) before the function and
   "mov edi,edi" (8B FF) at its start. Then: a long jump in the spare bytes, a
   short jump back to them over "mov edi,edi"; the original goes on at +2. */
void *hotpatch(void *fn, void *mine)
{
    uint8_t *f = fn;
    if (!f || f[0] != 0x8B || f[1] != 0xFF) return 0;
    for (int i = 1; i <= 5; i++)
        if (f[-i] != 0xCC && f[-i] != 0x90) return 0;
    uint8_t jmp[5] = { 0xE9 };
    int32_t rel = (int32_t)((uint8_t *)mine - f);
    memcpy(jmp + 1, &rel, 4);
    uint8_t back[2] = { 0xEB, 0xF9 }; /* jmp -7: to the long jump */
    if (!write_code(f - 5, jmp, 5) || !write_code(f, back, 2)) return 0;
    return f + 2;
}

/* the game's import table entry for dll!name, pointed at mine */
static bool patch_import(HMODULE exe, const char *dll, const char *name, void *real, void *mine)
{
    uint8_t *base = exe;
    uint32_t pe = *(uint32_t *)(base + 0x3c);
    uint32_t rva = *(uint32_t *)(base + pe + 24 + 104); /* x86 optional header: import directory */
    if (!rva) return false;
    for (uint32_t *d = (uint32_t *)(base + rva); d[3]; d += 5) {
        if (!same_name((char *)base + d[3], dll)) continue;
        uint32_t *names = d[0] ? (uint32_t *)(base + d[0]) : 0; /* the import names, when kept */
        for (void **iat = (void **)(base + d[4]); *iat; iat++) {
            bool hit = *iat == real;
            if (!hit && names) {
                uint32_t e = names[iat - (void **)(base + d[4])];
                hit = e && !(e & 0x80000000u) && !strcmp((char *)base + e + 2, name); /* by name, not by ordinal */
            }
            if (hit) {
                void *p = mine;
                return write_code(iat, &p, sizeof p);
            }
        }
    }
    return false;
}

static void put_in(HMODULE exe, const char *dll, const char *name, void **real, void *mine, bool may_load)
{
    if (*real) return; /* done already */
    HMODULE m = GetModuleHandleA(dll);
    if (!m && may_load) m = LoadLibraryA(dll);
    if (!m) return; /* not loaded yet: done at UPLAY_Startup (no loading while the DLL itself loads) */
    void *fn = GetProcAddress(m, name);
    if (!fn) { sl_log("! %s!%s not found", dll, name); return; }
    bool iat = patch_import(exe, dll, name, fn, mine);
    void *cont = hotpatch(fn, mine);
    *real = cont ? cont : fn; /* hot-patched: the original goes on past its first 2 bytes */
    sl_log("hook %s: %s%s", name, iat ? "import table" : "", cont ? (iat ? " + function" : "function") : (iat ? "" : "NOT HOOKED"));
}

/* may_load: false while the DLL loads (the loader lock is held), true from UPLAY_Startup */
void hooks_install(bool may_load)
{
    HMODULE exe = GetModuleHandleA(0);
    put_in(exe, "ws2_32.dll", "getaddrinfo", (void **)&real_getaddrinfo, (void *)my_getaddrinfo, may_load);
    put_in(exe, "ws2_32.dll", "gethostbyname", (void **)&real_gethostbyname, (void *)my_gethostbyname, may_load);
    put_in(exe, "iphlpapi.dll", "GetAdaptersInfo", (void **)&real_GetAdaptersInfo, (void *)my_GetAdaptersInfo, may_load);
    put_in(exe, "ws2_32.dll", "bind", (void **)&real_bind, (void *)my_bind, may_load);
    if (sl_second()) {
        put_in(exe, "kernel32.dll", "CreateMutexA", (void **)&real_CreateMutexA, (void *)my_CreateMutexA, may_load);
        put_in(exe, "kernel32.dll", "CreateMutexW", (void **)&real_CreateMutexW, (void *)my_CreateMutexW, may_load);
        put_in(exe, "kernel32.dll", "OpenMutexA", (void **)&real_OpenMutexA, (void *)my_OpenMutexA, may_load);
        put_in(exe, "kernel32.dll", "OpenMutexW", (void **)&real_OpenMutexW, (void *)my_OpenMutexW, may_load);
    }
}

/* A function someone (Steam's overlay) already detoured with a long jump at its start:
   ours goes in front, and the original jump becomes the way on. */
void *chain_jump(void *fn, void *mine)
{
    uint8_t *f = fn;
    if (!f || f[0] != 0xE9) return 0;
    int32_t rel;
    memcpy(&rel, f + 1, 4);
    void *on = f + 5 + rel; /* where their jump went */
    uint8_t jmp[5] = { 0xE9 };
    int32_t r2 = (int32_t)((uint8_t *)mine - (f + 5));
    memcpy(jmp + 1, &r2, 4);
    return write_code(f, jmp, 5) ? on : 0;
}
