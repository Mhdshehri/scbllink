/* scbllink.c - SCBLLink: Splinter Cell: Blacklist co-op between Steam friends.
 *
 * Each player's PC gets a small virtual network adapter ("SCBLLink", Wintun)
 * with an address in 10.237.51.0/24: the host is 10.237.51.1, friends .2-.8.
 * Packets the game sends into that adapter go to the other PC through Steam
 * (Spacewar, app 480, always through Valve's relay - ICE is turned off, so
 * nobody learns anyone's IP address). Only people in the host's friends-only
 * Steam lobby who are also on the host's Steam friends list get through, and
 * only the game's own ports (pkt.c decides).
 *
 * The game's online server is SCBLLink's own (srv.c), run by the host on its
 * SCBLLink address only, and the game reaches it through SCBLLink's own game
 * DLL (dll/), put in the game folder by "Install SCBL Fix".
 *
 * By Mhdshehri. */
#include "mini.h"
#include "plat.h"
#include "steam.h"
#include "lobbycode.h"
#include "wintun.h"
#include "sys.h"
#include "pkt.h"
#include "ui.h"
#include "account.h"
#include "fusion.h"
#include "pec.h"
#include "camera.h"
#include "gamepipe.h"
#include "ownsrv.h"

#define VERSION SCBLLINK_VERSION
#define PROTO '3'             /* link protocol; both players need the same */
#define SPACEWAR_APPID 480
#define BL_APPID 235600       /* Splinter Cell: Blacklist on Steam */
#define MAX_PEERS 7 /* friends: with the host 8, as many as Spies vs Mercs Blacklist (4v4) takes */
#define CH_FAST 0
#define CH_CTL 1
#define CH_RELIABLE 2
#define MAX_PKT 1500          /* = the adapter MTU: anything bigger is dropped */
#define MAX_CTL 8192 /* the roster of game sessions is the biggest (each kind is checked on its own) */
#define MAX_PKTS_PER_SEC 4000
#define KEEPALIVE_MS 1000
#define PEER_TIMEOUT_MS 30000
/* Like a normal network: the game's own packets fit whole. With 1150 the
   co-op mission's ~1460-byte packets were split in two by Windows (or dropped
   if the game forbids splitting), and the friend never finished joining.
   Steam carries 1500-byte messages fine. */
#define MTU 1500

/* ---------- state ---------- */

static int32_t g_pipe;
static void *U, *F, *MM, *NM, *NU, *AP;
static u64 g_me;
static bool g_steam_ok;

static int g_mode;
static u64 g_lobby, g_joining, g_host, g_host_last;
static u64 g_rejected[16];
static int g_nrejected;
static uint32_t g_my_ip;           /* this PC's address on the SCBLLink adapter, 0 = not set up */
static bool g_net_up;              /* adapter open, address set, firewall rules in */
static u64 g_last_hello;
static char g_game_dir[600];       /* ...\Splinter Cell Blacklist */
static char g_sys_dir[700];        /* ...\src\SYSTEM */
static char g_name[NAME_MAX_LEN + 1]; /* this player's name in the game */
static Account g_my_acct;          /* this player's account on the host's server */
static bool g_have_acct;

typedef struct { u64 window; int count; } Rate;

typedef struct {
    u64 id;
    int last;                      /* last number of the friend's address (2-4), host = 1 */
    uint32_t ip;
    bool welcomed;
    bool ff_sent;          /* host: the co-op settings went to them */
    u64 last_heard, last_ka;
    double rtt; bool have_rtt;
    Rate rate;
    u64 up, down, dropped;
    Account acct;                  /* host: the account made for them on its server */
    bool acct_ok;
    /* host: their game, as their SCBLLink tells it ('G') */
    bool in_game, inv_only, has_data;
    uint32_t session;
    uint8_t data[496];
} Peer;
static Peer g_peers[MAX_PEERS];
static int g_npeers;
static bool g_presence_dirty = true; /* the game's roster needs sending again (see game_tick) */

/* ---------- developer build (SCBL_DEV): never given to players ----------
   It records the game's talk with the server (see "recording" below), also alone:
   with "Record alone" on, this PC's own game doesn't reach its server directly (that
   never touches the adapter, so it can't be seen), but through the adapter, like a
   friend's game would: the game is told the server is 10.237.51.200, and SCBLLink
   turns those packets around to the real server on 10.237.51.1, which sees the game
   as 10.237.51.250. Friends can't play in that mode. */
#define SOLO_SERVER_LAST 200
#define SOLO_GAME_LAST 250
#ifdef SCBL_DEV
static bool g_solo;    /* the "Record alone" button */
static bool g_solo_on; /* hosting with it */
/* "2nd game": a second copy of the game on this PC plays a second player (10.237.51.20), a
   friend of the host's that lives here instead of behind Steam. Both games bind their sockets
   to their own address, so both have the game's ports. */
static bool g_two;     /* the button */
static bool g_two_on;  /* hosting with it */
#define GUEST_LAST 20
#define GUEST_NAME "Player2"
static Account g_g_acct;                     /* its account */
static bool g_g_ok, g_g_told, g_g_launched;
static bool g_g_game_on, g_g_inv_only, g_g_has_data;
static uint32_t g_g_session;
static uint8_t g_g_data[496];
static u64 g_g_launch_at;
static proc_t g_g_proc;
#define GUEST_STEAM (g_me + 1)
#else
enum { g_solo = 0, g_solo_on = 0, g_two = 0, g_two_on = 0 };
#endif
static bool g_account_told;          /* the game DLL has this player's account */
static void game_reset(void);

static u64 g_kicked[32], g_kick_told[32];
static int g_nkicked;

/* ---------- output ---------- */

static void say(const char *fmt, ...)
{
    char b[1200];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    b[sizeof b - 1] = 0;
    ui_log(b);
    log_line(b);
}

static void note(const char *fmt, ...)
{
    char b[1200];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    b[sizeof b - 1] = 0;
    char l[1220];
    sfmt(l, sizeof l, "  . %s", b);
    log_line(l);
}

static const char *idstr(u64 v)
{
    static char ring[4][24];
    static int k;
    char *o = ring[k++ & 3];
    u64_to_str(v, o);
    return o;
}

/* Steam names are chosen by other people: no control characters. */
static const char *clean(const char *n)
{
    static char ring[4][72];
    static int k;
    char *o = ring[k++ & 3];
    int i = 0;
    for (; n && n[i] && i < 64; i++) {
        unsigned char c = (unsigned char)n[i];
        o[i] = (c < 0x20 || c == 0x7f) ? '?' : (char)c;
    }
    if (n && n[i]) {
        int j = i;
        while (j > 0 && ((unsigned char)o[j - 1] & 0xC0) == 0x80) j--;
        if (j > 0) {
            unsigned char lead = (unsigned char)o[j - 1];
            int need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
            if (j - 1 + need > i) i = j - 1;
        }
    }
    o[i] = 0;
    return o;
}

static const char *who(u64 id)
{
    if (id == g_me) return clean(S.Friends_GetPersonaName(F));
    const char *n = S.Friends_GetFriendPersonaName(F, id);
    return (n && *n && strcmp(n, "[unknown]") != 0) ? clean(n) : idstr(id);
}

static const char *codestr(u64 lobby)
{
    static char ring[4][16];
    static int k;
    char *o = ring[k++ & 3];
    lobby_code(lobby, o);
    return o;
}

static const char *iptext(uint32_t ip)
{
    static char ring[4][16];
    static int k;
    char *o = ring[k++ & 3];
    vip_text(ip, o);
    return o;
}

static u64 rd64(const uint8_t *p) { u64 v; memcpy(&v, p, 8); return v; }
static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

/* ---------- Steam messages ---------- */

static void ident_of(uint8_t b[IDENT_SIZE], u64 id)
{
    memset(b, 0, IDENT_SIZE);
    S.Ident_SetSteamID64(b, id);
}

static void send_msg(u64 to, const void *p, int n, bool reliable, int ch)
{
    uint8_t id[IDENT_SIZE];
    ident_of(id, to);
    int flags = (reliable ? SEND_RELIABLE : SEND_UNRELIABLE) | SEND_NONAGLE;
    for (int tries = 0; tries < 200; tries++) {
        int r = S.NM_SendMessageToUser(NM, id, p, (uint32_t)n, flags, ch);
        if (r == 1 || !reliable) return;
        if (r != 25 /* k_EResultLimitExceeded: send buffer full */) return;
        S.MD_RunFrame(g_pipe);
        sleep_ms(5);
    }
}

static void send_ctl(u64 to, const void *p, int n) { send_msg(to, p, n, true, CH_CTL); }
static void send_ctl1(u64 to, char c) { char m[2] = { c, PROTO }; send_ctl(to, m, 2); }

static void close_session(u64 with)
{
    uint8_t id[IDENT_SIZE];
    ident_of(id, with);
    S.NM_CloseSessionWithUser(NM, id);
}

static bool in_lobby(u64 id)
{
    if (!g_lobby) return false;
    int n = S.MM_GetNumLobbyMembers(MM, g_lobby);
    for (int i = 0; i < n; i++)
        if (S.MM_GetLobbyMemberByIndex(MM, g_lobby, i) == id) return true;
    return false;
}

static bool rate_ok(Rate *r, u64 t)
{
    if (t - r->window >= 1000) { r->window = t; r->count = 0; }
    return ++r->count <= MAX_PKTS_PER_SEC;
}

/* ---------- peers ---------- */

static Peer *find_peer(u64 id)
{
    for (int i = 0; i < g_npeers; i++)
        if (g_peers[i].id == id) return &g_peers[i];
    return 0;
}

static Peer *peer_of_ip(uint32_t ip)
{
    for (int i = 0; i < g_npeers; i++)
        if (g_peers[i].welcomed && g_peers[i].ip == ip) return &g_peers[i];
    return 0;
}

static int free_last(void)
{
    for (int last = 2; last < 2 + MAX_PEERS; last++) {
        bool used = false;
        for (int i = 0; i < g_npeers; i++) used |= g_peers[i].last == last;
        if (!used) return last;
    }
    return 0;
}

static Peer *add_peer(u64 id, int last)
{
    if (g_npeers >= MAX_PEERS) return 0;
    Peer *p = &g_peers[g_npeers++];
    memset(p, 0, sizeof *p);
    p->id = id;
    p->last = last;
    p->ip = last ? vip(last) : 0;
    p->last_heard = now_ms();
    return p;
}

static void drop_peer(u64 id, const char *why)
{
    for (int i = 0; i < g_npeers; i++) {
        if (g_peers[i].id != id) continue;
        say("- %s disconnected (%s). Packets: %s sent, %s received, %s blocked.", who(id), why,
            idstr(g_peers[i].up), idstr(g_peers[i].down), idstr(g_peers[i].dropped));
        close_session(id);
        g_peers[i] = g_peers[--g_npeers];
        g_presence_dirty = true;
        return;
    }
}

static int kicked_index(u64 id)
{
    for (int i = 0; i < g_nkicked; i++)
        if (g_kicked[i] == id) return i;
    return -1;
}
static bool kicked(u64 id) { return kicked_index(id) >= 0; }

/* Who may reach the host: in the lobby, on the host's own friends list, not kicked. */
static bool allowed_peer(u64 id)
{
    return id && id != g_me && !kicked(id) && in_lobby(id) && S.Friends_HasFriend(F, id, FRIEND_IMMEDIATE);
}

static void tell_kicked(u64 id)
{
    int k = kicked_index(id);
    if (k >= 0 && now_ms() - g_kick_told[k] > 2000) { send_ctl1(id, 'Z'); g_kick_told[k] = now_ms(); }
}

static void note_ignored(u64 id)
{
    if (kicked(id)) { tell_kicked(id); return; }
    static u64 told[16];
    for (int i = 0; i < 16; i++) if (told[i] == id) return;
    for (int i = 0; i < 16; i++) if (!told[i]) { told[i] = id; break; }
    say("! Blocked %s: not in your lobby or not on your Steam friends list.", who(id));
}

/* ---------- the adapter: address, firewall ---------- */

#define ADAPTER "SCBLLink"
#define FW_RULE "SCBLLink"

static int cmd(const char *fmt, ...)
{
    char b[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    b[sizeof b - 1] = 0;
    int r = run_hidden(b, 20000);
    note("run: %s -> %d", b, r);
    return r;
}

static void firewall_remove(void)
{
    cmd("netsh advfirewall firewall delete rule name=\"" FW_RULE "\"");
}

/* Let in only the game's ports, only on the SCBLLink network. Nothing else on
   the PC is reachable through it, and the rules are gone when SCBLLink stops. */
static void firewall_add(void)
{
    firewall_remove();
    const char *net = "10.237.51.0/24";
    cmd("netsh advfirewall firewall add rule name=\"" FW_RULE "\" dir=in action=allow protocol=UDP "
        "localport=3074,13000-13031,21126,21127 localip=%s remoteip=%s profile=any", net, net);
    if (g_mode == MODE_HOST)
        cmd("netsh advfirewall firewall add rule name=\"" FW_RULE "\" dir=in action=allow protocol=TCP "
            "localport=80,8000 localip=%s remoteip=%s profile=any", net, net);
    cmd("netsh advfirewall firewall add rule name=\"" FW_RULE "\" dir=in action=allow protocol=icmpv4:8,any "
        "localip=%s remoteip=%s profile=any", net, net);
}

/* What Host (or Join) is doing right now, shown as the status while SCBLLink sets up, so the
   window never looks stuck. publish() sends it to the window at once. */
static char g_step[100];
static void publish(void);
static void step(const char *what)
{
    sfmt(g_step, sizeof g_step, "%s", what);
    publish();
}

/* Turning IPv6 off on the adapter goes through PowerShell, which takes 2-3 s to start: it runs
   on its own thread while the rest goes on. Its result is logged by the engine (v6_report). */
#define V6_CMD "powershell -NoProfile -NonInteractive -Command \"Disable-NetAdapterBinding -Name '" ADAPTER "' -ComponentID ms_tcpip6 -ErrorAction SilentlyContinue\""
static volatile int g_v6_state; /* 0 idle, 1 running, 2 done */
static volatile int g_v6_rc;
static void v6_thread(void *arg)
{
    (void)arg;
    g_v6_rc = run_hidden(V6_CMD, 20000);
    g_v6_state = 2;
}
static void v6_start(void)
{
    g_v6_state = 1;
    if (!thread_start(v6_thread, 0)) { g_v6_rc = run_hidden(V6_CMD, 20000); g_v6_state = 2; }
}
static void v6_report(void)
{
    if (g_v6_state != 2) return;
    g_v6_state = 0;
    note("run: %s -> %d", V6_CMD, g_v6_rc);
}

static bool net_up(int last)
{
    if (g_net_up && g_my_ip == vip(last)) return true;
    step(g_mode == MODE_HOST ? "Hosting: making the SCBLLink network adapter..." : "Making the SCBLLink network adapter...");
    const char *e = tun_open();
    if (e) { g_step[0] = 0; say("! %s", e); return false; }
    note("wintun.dll sha256 %s", tun_dll_hash());
    char ip[16];
    vip_text(vip(last), ip);
    step(g_mode == MODE_HOST ? "Hosting: giving the adapter its address..." : "Giving the adapter its address...");
    /* IPv4 only, a fixed address, no gateway: the PC keeps using its own
       internet connection for everything else. */
    if (cmd("netsh interface ipv4 set address name=\"" ADAPTER "\" static %s 255.255.255.0", ip) != 0) {
        sleep_ms(1500); /* a new adapter can need a moment before Windows lists it */
        if (cmd("netsh interface ipv4 set address name=\"" ADAPTER "\" static %s 255.255.255.0", ip) != 0) {
            say("! Could not give the SCBLLink adapter its address (%s). Is SCBLLink running as administrator?", ip);
            tun_close();
            g_step[0] = 0;
            return false;
        }
    }
    cmd("netsh interface ipv4 set subinterface \"" ADAPTER "\" mtu=%d store=active", MTU);
    cmd("netsh interface ipv4 set interface \"" ADAPTER "\" metric=500");
    /* the game reads the first address of an adapter: keep IPv6 off it (in the background;
       the first server check waits for it, so the game never starts before it's done) */
    v6_start();
    g_my_ip = vip(last);
    step(g_mode == MODE_HOST ? "Hosting: firewall rules for the game's ports..." : "Firewall rules for the game's ports...");
    firewall_add();
    g_net_up = true;
    g_step[0] = 0;
    say("SCBLLink adapter is up: this PC is %s on the SCBLLink network.", ip);
    return true;
}

static void net_down(void)
{
    if (!g_net_up && !tun_is_open()) return;
    firewall_remove();
    tun_close();
    g_net_up = false;
    g_my_ip = 0;
}

/* ---------- the game folder ---------- */

static bool find_game(void)
{
    if (g_sys_dir[0]) return true;
    char dir[600] = { 0 };
    if (g_steam_ok && AP && S.Apps_GetAppInstallDir)
        S.Apps_GetAppInstallDir(AP, BL_APPID, dir, sizeof dir);
    if (!dir[0])
        reg_read_hklm("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 235600", "InstallLocation", dir, sizeof dir);
    if (!dir[0]) return false;
    char sys[700], exe[800];
    sfmt(sys, sizeof sys, "%s\\src\\SYSTEM", dir);
    sfmt(exe, sizeof exe, "%s\\Blacklist_game.exe", sys);
    if (!file_exists(exe)) {
        sfmt(exe, sizeof exe, "%s\\Blacklist_DX11_game.exe", sys);
        if (!file_exists(exe)) return false;
    }
    sfmt(g_game_dir, sizeof g_game_dir, "%s", dir);
    sfmt(g_sys_dir, sizeof g_sys_dir, "%s", sys);
    return true;
}

static bool game_running(void)
{
    return game_is_running("Blacklist_game") || game_is_running("Blacklist_DX11_game");
}

static void sys_path(char *out, int n, const char *file) { sfmt(out, (size_t)n, "%s\\%s", g_sys_dir, file); }

bool sha256_file(const char *path, char hex[65]);

static bool file_is(const char *path, const char *sha)
{
    char hex[65];
    return sha256_file(path, hex) && strcmp(hex, sha) == 0;
}

/* ---------- the SCBL Fix: SCBLLink's own game DLL ----------
   Shipped next to SCBLLink.exe as scbllink-game.dll. Install puts it in the game folder as
   uplay_r1_loader.dll, keeping the game's own as uplay_r1_loader.orig.dll (the DLL passes the
   calls it doesn't answer itself on to it). Remove puts it back. */
#define OUR_DLL "scbllink-game.dll"
static char g_our_sha[65];
static bool our_dll(char *path, size_t n)
{
    char p[800];
    sfmt(p, sizeof p, "%s\\" OUR_DLL, exe_dir());
    if (path) sfmt(path, n, "%s", p);
    if (!g_our_sha[0] && !sha256_file(p, g_our_sha)) g_our_sha[0] = 0;
    return g_our_sha[0] != 0;
}
/* Our DLL is in place, with the game's own kept beside it. The answer is kept for 5 s.
   g_fix_old: another SCBLLink's game DLL is there (an older version): "Update SCBL Fix". */
static u64 g_fix_at;
static bool g_fix_old;
static u64 *fix_cache_at(void) { return &g_fix_at; }
static bool looks_like_ubisofts(const char *path);
static bool fix_ready(void)
{
    u64 at = g_fix_at;
    static bool ok;
    if (at && now_ms() - at < 5000) return ok;
    g_fix_at = now_ms();
    char orig[800], dll[800];
    sys_path(orig, sizeof orig, "uplay_r1_loader.orig.dll");
    sys_path(dll, sizeof dll, "uplay_r1_loader.dll");
    bool base = g_sys_dir[0] && our_dll(0, 0) && file_exists(orig);
    ok = base && file_is(dll, g_our_sha);
    g_fix_old = base && !ok && file_exists(dll) && !looks_like_ubisofts(dll);
    return ok;
}

/* Ubisoft's uplay_r1_loader.dll is about 400 KB; ours is far smaller, other loaders far bigger. */
static long file_size(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    long n = fseek(f, 0, 2) == 0 ? ftell(f) : -1;
    fclose(f);
    return n;
}

static bool looks_like_ubisofts(const char *path)
{
    char hex[65];
    if (!sha256_file(path, hex)) return false;
    our_dll(0, 0);
    if (g_our_sha[0] && !strcmp(hex, g_our_sha)) return false;
    long n = file_size(path);
    return n > 200 * 1024 && n < 2 * 1024 * 1024;
}

static bool download_from(const char *base, const char *what, const char *name, const char *sha, const char *to)
{
    if (file_is(to, sha)) return true;
    say("Downloading %s from %s ...", name, what);
    remove(to);
    char cmdl[2400];
    sfmt(cmdl, sizeof cmdl, "curl.exe -L --fail --silent --show-error --max-time 600 -o \"%s\" \"%s%s\"", to, base, name);
    int r = run_hidden(cmdl, 620000);
    note("curl %s -> %d", name, r);
    if (r != 0) { say("! The download failed (code %d). Check your internet and try again.", r); return false; }
    if (!file_is(to, sha)) {
        say("! %s isn't the file SCBLLink " VERSION " checked (a newer release?). Not installed.", name);
        remove(to);
        return false;
    }
    return true;
}

/* ---------- Fusion Fix (ThirteenAG, github.com/ThirteenAG/WidescreenFixesPack, GPL-3.0; its
   ASI loader version.dll is MIT): skips the intro, wide screens, mouse, unlocks the DLC and the
   4th Echelon missions, co-op options. Downloaded from its own release page, checked, never
   shipped with SCBLLink. Its settings: fusion.c. ---------- */

#define FF_BASE "https://github.com/ThirteenAG/WidescreenFixesPack/releases/download/scb/"
#define FF_ZIP "SplinterCellBlacklist.FusionFix.zip"
#define FF_ZIP_SHA "e7ab5a39f4861e90f1ea56c6f63321823f4d20b154977f563af3a4e34f5feb44"
#define FF_ASI "scripts\\SplinterCellBlacklist.FusionFix.asi"
#define FF_ASI_SHA "924fb076866ff8245f5e64f426782eaccaeaa2fb4791e8dc428f0184208be73c"
#define FF_LOADER "version.dll"
#define FF_LOADER_SHA "d5a059aa467a7a7127c8f6169f79fa63ff0f55986ee9eb2fd9a281bebf2aa2e6"
#define FF_INI "scripts\\SplinterCellBlacklist.FusionFix.ini"

/* everything in the zip, under src\SYSTEM (the .ini is the player's: kept if there) */
static const char *const FF_FILES[] = {
    FF_LOADER, FF_ASI, FF_INI,
    "update\\difficultyconfiguration.ini",
    "update\\Data\\ExtractionWaveConfigs\\DefaultWaveConfig.xml",
    "update\\Data\\ExtractionWaveConfigs\\D_Amman.xml",
    "update\\Data\\ExtractionWaveConfigs\\D_Bratislava.xml",
    "update\\Data\\ExtractionWaveConfigs\\D_Kigali.xml",
    "update\\Data\\ExtractionWaveConfigs\\D_Sanaa.xml",
    "update\\Data\\ExtractionWaveConfigs\\Random\\DefaultWaveConfig.xml",
    "update\\Data\\ExtractionWaveConfigs\\Random\\D_Amman.xml",
    "update\\Data\\ExtractionWaveConfigs\\Random\\D_Bratislava.xml",
    "update\\Data\\ExtractionWaveConfigs\\Random\\D_Kigali.xml",
    "update\\Data\\ExtractionWaveConfigs\\Random\\D_Sanaa.xml",
    0
};
static const char *const FF_DIRS[] = { /* deepest first, for removing */
    "update\\Data\\ExtractionWaveConfigs\\Random", "update\\Data\\ExtractionWaveConfigs", "update\\Data", "update", "scripts", 0
};

static bool g_ff_ok;        /* Fusion Fix is in the game folder */
static bool g_ff_checked;
static bool g_ff_blocked;   /* another mod's version.dll is there: Fusion Fix is left out */
static bool g_ff_host_rules; /* joined: the host's co-op settings were written */

static bool ff_check(void)
{
    char a[800], l[800];
    g_ff_checked = true;
    g_ff_ok = false;
    if (!g_sys_dir[0] && !find_game()) return false;
    sys_path(a, sizeof a, FF_ASI);
    sys_path(l, sizeof l, FF_LOADER);
    g_ff_ok = file_is(a, FF_ASI_SHA) && file_is(l, FF_LOADER_SHA);
    g_ff_blocked = !g_ff_ok && file_exists(l) && !file_is(l, FF_LOADER_SHA);
    return g_ff_ok;
}

/* Fusion Fix's .ini: read, changed by fn, made safe, written. */
static bool ff_edit(bool (*fn)(char **ini, void *arg), void *arg)
{
    char path[800];
    sys_path(path, sizeof path, FF_INI);
    int n = 0;
    char *ini = file_read_all(path, &n);
    if (!ini) { say("! Fusion Fix's settings file is missing. Click Remove, then Install SCBL Fix again."); return false; }
    bool ok = fn(&ini, arg) && ff_harden(&ini);
    if (ok && !file_write_text(path, ini)) { say("! Could not save Fusion Fix's settings (is the file read-only?)."); ok = false; }
    free(ini);
    return ok;
}

static bool ff_edit_nothing(char **ini, void *arg) { (void)ini; (void)arg; return true; }

/* The game's own videoSettings.ini (Documents\Ubisoft\Blacklist): motion blur and occlusion,
   which the game's menu doesn't offer. Only those keys' values change; the game made the file. */
static bool video_ini_path(char *out, size_t n)
{
    char d[300];
    if (!documents_dir(d, sizeof d)) return false;
    sfmt(out, n, "%s\\Ubisoft\\Blacklist\\videoSettings.ini", d);
    return file_exists(out);
}

/* returns how many changed, -1 when the file couldn't be written */
static int video_apply(const uint8_t *m, int n)
{
    char path[400];
    if (!video_ini_path(path, sizeof path)) return 0; /* the game hasn't made it yet: nothing to change */
    int len = 0;
    char *ini = file_read_all(path, &len);
    if (!ini) return 0;
    int changed = ff_apply(&ini, m, n, FF_VIDEO);
    if (changed > 0 && !file_write_text(path, ini)) changed = -1;
    free(ini);
    return changed;
}

/* ---------- Balaclava and Crosshair: the game's own config files, changed on this PC ----------
   The game DLL saves the game's own file in %APPDATA%\SCBLLink the first time the game runs
   (dll/pec.c, dll/cfgcap.c). From it SCBLLink writes the changed file into the game folder
   (marked as ours) while the choice is on, and removes it (only ours) when it's off. The game
   reads it when it starts. Nothing of the game's is shipped. */
/* [SCBLLink] key = val in Fusion Fix's .ini (where Game settings keeps SCBLLink's own choices) */
static bool ff_choice(const char *key, const char *val)
{
    char path[800], v[8];
    sys_path(path, sizeof path, FF_INI);
    int n = 0;
    char *ini = file_read_all(path, &n);
    bool on = ini && ff_ini_get(ini, FF_SCBL_SEC, key, v, sizeof v) && !strcmp(v, val);
    free(ini);
    return on;
}

static bool bala_sam(void) { return ff_choice("Balaclava", "1"); }
static bool bala_briggs(void) { return ff_choice("BalaclavaBriggs", "1"); }
static bool bala_any(void) { return bala_sam() || bala_briggs(); }
static char *bala_make(const char *orig) { return pec_balaclava(orig, bala_sam(), bala_briggs()); }
static bool cross_off(void) { return ff_choice("Crosshair", "0"); }

typedef struct {
    const char *name;        /* "Balaclava" */
    bool (*wanted)(void);    /* write our file? */
    const char *file;        /* under src\SYSTEM */
    const char *orig;        /* in %APPDATA%\SCBLLink */
    char *(*make)(const char *orig);
    bool (*ours)(const char *text);
    const char *on_title, *on_line, *off_title, *off_line;   /* the overlay's card */
    bool said_other, said_wait;
} GameOpt;

static GameOpt g_opts[] = {
    { "Balaclava", bala_any, PEC_FILE, PEC_ORIG_FILE, bala_make, pec_ours,
      "Balaclava is ready", "Restart Blacklist to see it", "Balaclava is off", "Restart Blacklist to remove it", false, false },
    { "Crosshair off", cross_off, CAM_FILE, CAM_ORIG_FILE, cam_crosshair_off, cam_ours,
      "Crosshair off is ready", "Restart Blacklist to hide it", "Crosshair is back on", "Restart Blacklist to see it", false, false },
};
#define NOPTS ((int)(sizeof g_opts / sizeof g_opts[0]))

static bool opt_orig_path(const GameOpt *o, char *out, size_t n)
{
    const char *app = getenv("APPDATA");
    if (!app) return false;
    sfmt(out, n, "%s\\SCBLLink\\%s", app, o->orig);
    return true;
}

/* tell: the player just saved the settings, so say what happens; otherwise only news */
static void opt_sync(GameOpt *o, bool tell)
{
    if (!g_sys_dir[0]) return;
    char path[800], orig_path[800];
    sys_path(path, sizeof path, o->file);
    int n = 0;
    char *cur = file_read_all(path, &n);
    bool ours = o->ours(cur), want = o->wanted();
    if (!want) {
        if (ours) {
            if (remove(path) == 0) {
                say("%s: undone, from the next start of Blacklist.", o->name);
                if (game_running()) gp_send("NOTICE\t%s\t%s", o->off_title, o->off_line);
            } else say("! Could not remove %s (%s): is it read-only?", o->file, o->name);
        }
        free(cur);
        return;
    }
    if (cur && !ours) {
        if (tell || !o->said_other)
            say("! There is already a %s in the game folder (a mod?). %s was left out so it isn't changed: remove that file first.", o->file, o->name);
        o->said_other = true;
        free(cur);
        return;
    }
    if (!opt_orig_path(o, orig_path, sizeof orig_path)) return;
    char *orig = file_read_all(orig_path, &n);
    if (!orig) {
        free(cur);
        if (tell || !o->said_wait)
            say("%s: start Blacklist once and go to the menu. SCBLLink then gets the game's own settings, "
                "and it counts from the start after that.", o->name);
        o->said_wait = true;
        return;
    }
    char *made = o->make(orig);
    free(orig);
    if (made && ours && !strcmp(made, cur)) {   /* ours, and as the choices want it */
        free(made);
        free(cur);
        if (tell) say("%s is on.", o->name);
        return;
    }
    free(cur);   /* not there, or ours but made for other choices: written (again) below */
    if (!made) {
        /* not the whole file: never written (a partial config breaks the game and its saves); the game DLL tries again */
        remove(orig_path);
        say("! %s: the settings SCBLLink got from the game weren't complete. Start Blacklist once more and go to the menu.", o->name);
        return;
    }
    if (strchr(o->file, '\\')) {   /* update\...: the folder may not be there yet */
        char dir[800];
        sfmt(dir, sizeof dir, "%s", path);
        char *cut = 0;
        for (char *q = dir; *q; q++) if (*q == '\\') cut = q;
        if (cut) *cut = 0;
        if (!file_exists(dir)) cmd("cmd /c mkdir \"%s\"", dir);
    }
    bool ok = file_write_text(path, made);
    free(made);
    if (!ok) { say("! Could not write %s (%s).", o->file, o->name); return; }
    if (game_running()) {
        say("%s: ready, restart Blacklist to see it.", o->name);
        gp_send("NOTICE\t%s\t%s", o->on_title, o->on_line);   /* a red card in the game */
    } else say("%s is on: it counts from the next start of Blacklist.", o->name);
}

static void opts_sync(bool tell) { for (int i = 0; i < NOPTS; i++) opt_sync(&g_opts[i], tell); }

/* Remove SCBL Fix: our files go too */
static void opts_remove(void)
{
    for (int i = 0; i < NOPTS; i++) {
        char path[800];
        sys_path(path, sizeof path, g_opts[i].file);
        int n = 0;
        char *t = file_read_all(path, &n);
        if (g_opts[i].ours(t) && remove(path) == 0) say("%s: its file is removed.", g_opts[i].name);
        free(t);
    }
}

static bool ff_install(void)
{
    char dir[700], zip[800], un[800];
    sfmt(dir, sizeof dir, "%s\\fusionfix", exe_dir());
    sfmt(zip, sizeof zip, "%s\\" FF_ZIP, dir);
    sfmt(un, sizeof un, "%s\\unpacked", dir);
    char cur[800];
    sys_path(cur, sizeof cur, FF_LOADER);
    if (file_exists(cur) && !file_is(cur, FF_LOADER_SHA)) {
        say("! There is already a version.dll in the game folder (another mod?). Fusion Fix was left out so it isn't broken.");
        return false;
    }
    cmd("cmd /c mkdir \"%s\"", dir);
    if (!download_from(FF_BASE, "github.com/ThirteenAG/WidescreenFixesPack", FF_ZIP, FF_ZIP_SHA, zip)) return false;
    cmd("cmd /c rmdir /s /q \"%s\"", un);
    cmd("cmd /c mkdir \"%s\"", un);
    /* Windows' own tar (Windows 10 and later) unpacks zips */
    if (cmd("tar.exe -xf \"%s\" -C \"%s\"", zip, un) != 0) { say("! Could not unpack Fusion Fix."); return false; }
    char from[900], to[900];
    sfmt(from, sizeof from, "%s\\src\\SYSTEM\\" FF_ASI, un);
    if (!file_is(from, FF_ASI_SHA)) { say("! Fusion Fix's files aren't the ones SCBLLink checked. Not installed."); return false; }
    sfmt(from, sizeof from, "%s\\src\\SYSTEM\\" FF_LOADER, un);
    if (!file_is(from, FF_LOADER_SHA)) { say("! Fusion Fix's files aren't the ones SCBLLink checked. Not installed."); return false; }
    for (int i = 4; i >= 0; i--) { /* folders, top first */
        if (!FF_DIRS[i]) continue;
        sys_path(to, sizeof to, FF_DIRS[i]);
        cmd("cmd /c mkdir \"%s\"", to);
    }
    for (int i = 0; FF_FILES[i]; i++) {
        sfmt(from, sizeof from, "%s\\src\\SYSTEM\\%s", un, FF_FILES[i]);
        sys_path(to, sizeof to, FF_FILES[i]);
        if (!strcmp(FF_FILES[i], FF_INI) && file_exists(to)) continue; /* the player's settings stay */
        if (!file_copy(from, to)) { say("! Could not put %s in the game folder.", FF_FILES[i]); return false; }
    }
    cmd("cmd /c rmdir /s /q \"%s\"", un);
    if (!ff_edit(ff_edit_nothing, 0)) return false;
    ff_check();
    say("Fusion Fix installed (by ThirteenAG). Change its options with \"Game settings\".");
    return g_ff_ok;
}

static void ff_remove(void)
{
    char p[800];
    if (!ff_check()) {
        /* not ours (or not there): only the files we know, and only ours */
        sys_path(p, sizeof p, FF_LOADER);
        if (file_exists(p) && !file_is(p, FF_LOADER_SHA)) return;
    }
    for (int i = 0; FF_FILES[i]; i++) { sys_path(p, sizeof p, FF_FILES[i]); remove(p); }
    for (int i = 0; FF_DIRS[i]; i++) { sys_path(p, sizeof p, FF_DIRS[i]); cmd("cmd /c rmdir \"%s\"", p); } /* only when empty */
    ff_check();
    say("Fusion Fix removed.");
}

static void fix_install(void)
{
    if (!find_game()) { say("! Splinter Cell: Blacklist was not found."); return; }
    if (game_running()) { say("! Close Splinter Cell: Blacklist first."); return; }
    char dll[800];
    if (!our_dll(dll, sizeof dll)) { say("! " OUR_DLL " isn't next to SCBLLink.exe. Unpack the whole SCBLLink folder again."); return; }
    /* the game folder: keep Ubisoft's DLL as .orig, put ours in */
    char cur[800], orig[800];
    sys_path(cur, sizeof cur, "uplay_r1_loader.dll");
    sys_path(orig, sizeof orig, "uplay_r1_loader.orig.dll");
    if (!file_is(cur, g_our_sha)) {
        if (file_exists(cur) && looks_like_ubisofts(cur)) {
            /* the game's own (also after a Steam "verify", which puts it back) */
            if (!file_copy(cur, orig)) { say("! Could not back up the game's uplay_r1_loader.dll."); return; }
        } else if (!file_exists(orig)) {
            say("! The game's own uplay_r1_loader.dll isn't there to keep. Use Steam's \"Verify integrity of game files\", then try again.");
            return;
        }
        if (!file_copy(dll, cur)) { say("! Could not put the SCBL Fix in the game folder (is the game closed?)."); return; }
    }
    *fix_cache_at() = 0;
    if (g_fix_old) say("SCBL Fix updated: the game folder now has this SCBLLink's game DLL.");
    else say("SCBL Fix installed: SCBLLink's game DLL. The game's own DLL is kept as uplay_r1_loader.orig.dll.");
    say("(After a Steam \"Verify integrity\", click Install SCBL Fix again.)");
    if (!ff_check()) ff_install();
}

static void fix_remove(void)
{
    if (!find_game()) return;
    if (game_running()) { say("! Close Splinter Cell: Blacklist first."); return; }
    char cur[800], orig[800];
    sys_path(cur, sizeof cur, "uplay_r1_loader.dll");
    sys_path(orig, sizeof orig, "uplay_r1_loader.orig.dll");
    if (!file_exists(orig)) { say("Nothing to remove: the game's own DLL is in place."); return; }
    if (!looks_like_ubisofts(orig)) { say("! uplay_r1_loader.orig.dll isn't the game's own. Use Steam's \"Verify integrity of game files\" instead."); return; }
    if (!file_copy(orig, cur)) { say("! Could not put the game's own DLL back."); return; }
    remove(orig);
    *fix_cache_at() = 0;
    say("SCBL Fix removed: the game is back to its own uplay_r1_loader.dll.");
    opts_remove();
    ff_remove();
}

/* Older SCBLLink versions wrote uplay.toml and uplay.override.toml in the game folder (marked
   "Written by SCBLLink", the player's own kept as *.scbllink-backup). Nothing reads them now:
   ours go, the player's come back. */
static void old_files_tidy(void)
{
    static const char *const F[] = { "uplay.toml", "uplay.override.toml", 0 };
    if (!g_sys_dir[0]) return;
    for (int i = 0; F[i]; i++) {
        char path[800], bak[800];
        sys_path(path, sizeof path, F[i]);
        sfmt(bak, sizeof bak, "%s.scbllink-backup", path);
        int n = 0;
        char *t = file_read_all(path, &n);
        bool ours = t && strstr(t, "Written by SCBLLink");
        free(t);
        if (!ours) continue;
        remove(path);
        if (file_exists(bak) && rename(bak, path) != 0) note("! could not put %s back", F[i]);
        note("tidied the old %s", F[i]);
    }
}

/* ---------- is the host's server reachable? (own thread: it may block) ---------- */

enum { CHK_IDLE, CHK_RUNNING, CHK_DONE };
static volatile int g_chk_state;
static volatile int g_chk_http, g_chk_auth, g_chk_secure; /* 1 ok, 0 no answer */
static volatile bool g_chk_quiet;

static bool check_http(uint32_t host)
{
    sock_t s = tcp_connect(g_my_ip, 0, host, 80);
    if (s == BADSOCK) return false;
    static const char req[] = "GET /OnlineConfigService.svc/GetOnlineConfig?onlineConfigID=0 HTTP/1.1\r\n"
                              "Host: onlineconfigservice.ubi.com\r\nConnection: close\r\n\r\n";
    bool ok = false;
    u64 t0 = now_ms();
    int sent = 0;
    while (sent < (int)sizeof req - 1 && now_ms() - t0 < 3000) {
        int r = tcp_send(s, req + sent, (int)sizeof req - 1 - sent);
        if (r < 0) break;
        sent += r;
        if (!r) sleep_ms(10);
    }
    char buf[256];
    int got = 0;
    while (got < (int)sizeof buf - 1 && now_ms() - t0 < 4000) {
        int r = tcp_recv(s, buf + got, (int)sizeof buf - 1 - got);
        if (r < 0) break;
        if (!r) { sleep_ms(10); continue; }
        got += r;
        buf[got] = 0;
        if (strchr(buf, '\n')) break;
    }
    buf[got] = 0;
    ok = !strncmp(buf, "HTTP/1.", 7) && strstr(buf, " 200") != 0;
    sock_close(s);
    return ok;
}

static bool check_quazal(uint32_t host, uint16_t port)
{
    sock_t s = udp_open(g_my_ip, 0, 0);
    if (s == BADSOCK) return false;
    uint8_t syn[16];
    int n = quazal_syn(syn, 0);
    bool ok = false;
    for (int tries = 0; tries < 3 && !ok; tries++) {
        udp_sendto(s, syn, n, host, port);
        u64 t0 = now_ms();
        while (!ok && now_ms() - t0 < 1000) {
            uint8_t b[512];
            uint32_t ip;
            uint16_t pt;
            int r = udp_recvfrom(s, b, sizeof b, &ip, &pt);
            if (r > 0 && ip == host && pt == port && quazal_is_syn_ack(b, r)) ok = true;
            else if (r <= 0) sleep_ms(10);
        }
    }
    sock_close(s);
    return ok;
}

static void check_thread(void *arg)
{
    uint32_t host = vip(HOST_LAST);
    g_chk_http = check_http(host);
    g_chk_auth = check_quazal(host, 21126);
    g_chk_secure = check_quazal(host, 21127);
    g_chk_state = CHK_DONE;
}

static void check_start(bool quiet)
{
    if (g_chk_state == CHK_RUNNING || !g_net_up) return;
    g_chk_quiet = quiet;
    g_chk_state = CHK_RUNNING;
    if (!thread_start(check_thread, 0)) g_chk_state = CHK_IDLE;
}

static bool g_ready;          /* last check passed */
static u64 g_next_check;
static int g_chk_tries;       /* checks since Host/Join: the first ones are quick and quiet */

static void check_report(void)
{
    if (g_chk_state != CHK_DONE) return;
    g_chk_state = CHK_IDLE;
    bool ok = g_chk_http && g_chk_auth && g_chk_secure;
    /* the host's own account, made once its server answers */
    if (ok && g_mode == MODE_HOST && !g_have_acct) {
        if (account_ensure(g_me, g_name, &g_my_acct)) {
            g_have_acct = true;
            g_account_told = false;
            g_presence_dirty = true;
            say("Your name in the game: %s", g_my_acct.name);
        } else {
            note("host account not made yet");
            ok = false;
            g_chk_quiet = true;
        }
    }
    if (ok && g_mode == MODE_JOIN && !g_have_acct) ok = false;
    bool was = g_ready;
    g_ready = ok;
    if (ok && (!was || !g_chk_quiet)) {
        say("=== Ready: the host's server answers through SCBLLink. ===");
        say("Click \"Start Blacklist\" (or start it from Steam).");
        ui_alert("ready");
        return;
    }
    if (ok || g_chk_quiet) return;
    say("Server check: config %s, login %s, game server %s.",
        g_chk_http ? "OK" : "no answer", g_chk_auth ? "OK" : "no answer", g_chk_secure ? "OK" : "no answer");
    say("  Don't start the game yet: it only looks for the server once, at start.");
}

/* ---------- joiner ---------- */

static bool rejected(u64 lobby)
{
    for (int i = 0; i < g_nrejected; i++)
        if (g_rejected[i] == lobby) return true;
    return false;
}
static void reject(u64 lobby) { if (g_nrejected < 16) g_rejected[g_nrejected++] = lobby; }

static void lobby_gone(void)
{
    if (g_lobby) S.MM_LeaveLobby(MM, g_lobby);
    if (g_mode == MODE_JOIN) g_npeers = 0;
    g_lobby = g_host = 0;
    g_ready = false;
    g_have_acct = false;
    g_ff_host_rules = false;
    game_reset();
    S.Friends_ClearRichPresence(F);
}

static void request_join(u64 lobby)
{
    if (g_mode != MODE_JOIN || !lobby || lobby == g_lobby || lobby == g_joining || rejected(lobby)) return;
    if (g_lobby) { send_ctl1(g_host, 'B'); lobby_gone(); }
    g_joining = lobby;
    say("Joining lobby %s ...", codestr(lobby));
    S.MM_JoinLobby(MM, lobby);
}

static void send_hello(void)
{
    uint8_t m[3 + NAME_MAX_LEN + 16] = { 'H', PROTO };
    int nl = (int)strlen(g_name), vl = (int)strlen(VERSION);
    m[2] = (uint8_t)nl;
    memcpy(m + 3, g_name, (size_t)nl);
    memcpy(m + 3 + nl, VERSION, (size_t)vl);
    send_ctl(g_host, m, 3 + nl + vl);
    g_last_hello = now_ms();
}

static void on_lobby_entered(u64 lobby, uint32_t response)
{
    if (g_mode != MODE_JOIN || lobby == g_lobby) return;
    g_joining = 0;
    if (response != 1) {
        if (!rejected(lobby)) say("! Could not join that lobby (Steam code %u).", (unsigned)response);
        reject(lobby);
        return;
    }
    const char *tag = S.MM_GetLobbyData(MM, lobby, "scbllink");
    const char *proto = S.MM_GetLobbyData(MM, lobby, "scbllink_proto");
    if (!tag || strcmp(tag, "1") || !proto || proto[0] != PROTO || proto[1]) {
        const char *ver = S.MM_GetLobbyData(MM, lobby, "scbllink_ver");
        say("! That lobby is not an SCBLLink %s game (theirs: %s).", VERSION, ver && *ver ? clean(ver) : "?");
        S.MM_LeaveLobby(MM, lobby);
        reject(lobby);
        return;
    }
    g_lobby = lobby;
    g_host = S.MM_GetLobbyOwner(MM, lobby);
    g_host_last = now_ms();
    g_npeers = 0;
    add_peer(g_host, 0);
    say("Joined %s's lobby. Connecting through Steam...", who(g_host));
    S.Friends_SetRichPresence(F, "status", "Playing Splinter Cell: Blacklist co-op (SCBLLink)");
    send_hello();
}

typedef struct { u64 friend_, lobby, at, asked; } Answer;
#define NANS 256
static Answer g_ans[NANS];

static Answer *answer_for(u64 f, bool make)
{
    int free_ = -1, oldest = 0;
    for (int i = 0; i < NANS; i++) {
        if (g_ans[i].friend_ == f) return &g_ans[i];
        if (!g_ans[i].friend_ && free_ < 0) free_ = i;
        if (g_ans[i].asked < g_ans[oldest].asked) oldest = i;
    }
    if (!make) return 0;
    if (free_ < 0) free_ = oldest;
    memset(&g_ans[free_], 0, sizeof g_ans[free_]);
    g_ans[free_].friend_ = f;
    return &g_ans[free_];
}

/* Find hosting friends: rich presence while they are in SCBLLink, otherwise
   ask their SCBLLink directly ('Q'; a host answers 'L' + lobby id, only to its
   own friends). They are listed; the player picks one and clicks Join. */
static int g_last_found = -1;
static void scan_for_host(void)
{
    static UiPerson found[16];
    int n = 0, total = S.Friends_GetFriendCount(F, FRIEND_IMMEDIATE);
    u64 t = now_ms();
    for (int i = 0; i < total && n < 16; i++) {
        u64 id = S.Friends_GetFriendByIndex(F, i, FRIEND_IMMEDIATE);
        uint8_t info[32] = { 0 };
        bool playing = S.Friends_GetFriendGamePlayed(F, id, info);
        uint32_t app = playing ? (uint32_t)(rd64(info) & 0xFFFFFF) : 0;
        u64 lob = 0;
        if (app == SPACEWAR_APPID) {
            S.Friends_RequestFriendRichPresence(F, id);
            const char *rp = S.Friends_GetFriendRichPresence(F, id, "scbllink_lobby");
            if (rp && *rp) lob = str_to_u64(rp);
        }
        if (!lob) {
            Answer *a = answer_for(id, false);
            if (a && a->lobby && t - a->at < 15000) lob = a->lobby;
            else if (S.Friends_GetFriendPersonaState(F, id) != 0) {
                u64 every = (app == SPACEWAR_APPID || app == BL_APPID) ? 4000 : 30000;
                if (!a) a = answer_for(id, true);
                if (!a->asked || t - a->asked > every) { send_ctl1(id, 'Q'); a->asked = t; }
            }
        }
        if (lob && !rejected(lob)) {
            found[n].id = id;
            found[n].lobby = lob;
            sfmt(found[n].name, UI_NAME, "%s", who(id));
            n++;
        }
    }
    if (n != g_last_found && n > 0) say(n == 1 ? "Found %s hosting. Pick them and click Join." : "Found %s friends hosting.", n == 1 ? found[0].name : idstr((u64)n));
    g_last_found = n;
    ui_list(LIST_HOSTING, n, found);
}

/* ---------- host: lobby ---------- */

static void on_lobby_created(int32_t result, u64 lobby)
{
    if (g_mode != MODE_HOST || g_lobby) return;
    if (result != 1) { say("! Steam could not create a lobby (error %d).", result); return; }
    g_lobby = lobby;
    S.MM_SetLobbyData(MM, lobby, "scbllink", "1");
    S.MM_SetLobbyData(MM, lobby, "scbllink_ver", VERSION);
    { char pr[2] = { PROTO, 0 }; S.MM_SetLobbyData(MM, lobby, "scbllink_proto", pr); }
    S.MM_SetLobbyJoinable(MM, lobby, true);
    S.Friends_SetRichPresence(F, "status", "Hosting Splinter Cell: Blacklist co-op (SCBLLink)");
    S.Friends_SetRichPresence(F, "scbllink_lobby", idstr(lobby));
    clipboard_set(codestr(lobby));
    say("");
    say("=== Lobby is ready (code %s, copied to clipboard) ===", codestr(lobby));
    say("Your friend opens SCBLLink and clicks \"Join a friend\" (or pastes the code).");
    say("");
}

/* ---------- control messages ---------- */

static void version_notice(u64 id, const uint8_t *v, int n)
{
    char tv[16] = { 0 };
    if (n > 0) memcpy(tv, v, (size_t)(n < 15 ? n : 15));
    if (tv[0] && strcmp(tv, VERSION)) say("! %s has SCBLLink %s, you have " VERSION ".", who(id), clean(tv));
}

/* host -> friend: 'F', PROTO, has Fusion Fix, then its co-op settings (fusion.c) */
static void send_ff(u64 to)
{
    uint8_t m[512] = { 'F', PROTO, 0 }; /* (no big stack frames: this build has no stack probes) */
    int k = 3;
    if (ff_check()) {
        char path[800];
        sys_path(path, sizeof path, FF_INI);
        int len = 0;
        char *ini = file_read_all(path, &len);
        int pn = ini ? ff_pack_shared(ini, m + 3, (int)sizeof m - 3) : -1;
        free(ini);
        if (pn >= 0) { m[2] = 1; k += pn; }
    }
    send_ctl(to, m, k);
}

static void send_ff_all(void)
{
    for (int i = 0; i < g_npeers; i++)
        if (g_peers[i].welcomed) send_ff(g_peers[i].id);
}

typedef struct { const uint8_t *m; int n, which, applied; } FfApply;
static bool ff_apply_cb(char **ini, void *arg)
{
    FfApply *a = arg;
    a->applied = ff_apply(ini, a->m, a->n, a->which);
    return a->applied >= 0;
}

static void on_host_ff(const uint8_t *m, int n)
{
    if (n < 3 || m[1] != PROTO) return;
    static int told = -1; /* one warning per kind */
    bool theirs = m[2] == 1, mine = ff_check();
    if (theirs != mine) {
        if (told != (int)theirs)
            say(theirs ? "! %s plays with Fusion Fix and you don't: click Stop, then Install SCBL Fix, so your game matches theirs."
                       : "! %s plays without Fusion Fix and you have it: co-op missions may differ (enemies, unlocks).", who(g_host));
        told = (int)theirs;
        return;
    }
    if (!theirs) return;
    FfApply a = { m + 3, n - 3, FF_SHARED, 0 };
    if (!ff_edit(ff_apply_cb, &a)) { say("! %s's co-op settings could not be used (they sent bad ones?).", who(g_host)); return; }
    g_ff_host_rules = true;
    if (a.applied > 0) {
        say("Co-op settings from %s: %d changed (enemies, unlocks...). You can see them in \"Game settings\".", who(g_host), a.applied);
        if (game_running()) say("! They count from the next start of Blacklist: close it and click \"Start Blacklist\" again.");
    }
}

/* ---------- the game DLL (phases B/C): who's playing, game sessions, invitations ----------
   SCBLLink's own uplay_r1_loader.dll asks this PC's SCBLLink, through a pipe (gamepipe), for
   the game account and the friend list, and tells it the game's session and invitations.
   The host keeps the roster - everyone's game account, whether their game is running, the
   session it's in - and sends it to the friends ('R'); friends tell the host about their
   game ('G'); invitations go through the host ('I'). */

#define SESSION_DATA 496
typedef struct {
    u64 steam;
    char name[NAME_MAX_LEN + 1], ubi[65];
    bool online, inv_only, has_data;
    uint32_t session;
    uint8_t data[SESSION_DATA];
} Member;
static Member g_roster[MAX_PEERS + 1];
static int g_nroster;
static uint8_t g_roster_sent[MAX_CTL];
static int g_roster_sent_n = -1;

/* this PC's game */
static bool g_game_on, g_my_inv_only, g_my_has_data;
static uint32_t g_my_session;
static uint8_t g_my_data[SESSION_DATA];
static char g_invite_from[NAME_MAX_LEN + 1]; /* a pending invitation, "" = none */

static int put_str(uint8_t *m, int k, const char *v)
{
    int l = (int)strlen(v);
    m[k++] = (uint8_t)l;
    memcpy(m + k, v, (size_t)l);
    return k + l;
}

static bool get_str(const uint8_t *m, int n, int *k, char *out, int max)
{
    if (*k >= n) return false;
    int l = m[*k];
    if (l > max || *k + 1 + l > n) return false;
    memcpy(out, m + *k + 1, (size_t)l);
    out[l] = 0;
    *k += 1 + l;
    return true;
}

static void hex_of(const uint8_t *d, int n, char *out)
{
    static const char h[] = "0123456789abcdef";
    for (int i = 0; i < n; i++) { out[2 * i] = h[d[i] >> 4]; out[2 * i + 1] = h[d[i] & 15]; }
    out[2 * n] = 0;
}

#define FAKE_FRIEND_UBI "7e577e577e577e577e577e577e577e57"
static void guest_friends(void);
/* the game's friend list: everyone in the roster but this player */
static void game_friends(void)
{
    if (!gp_connected()) return;
    static char line[2 * SESSION_DATA + 200];
    gp_send("FRIENDS");
    for (int i = 0; i < g_nroster; i++) {
        Member *r = &g_roster[i];
        if (r->steam == g_me || !r->ubi[0]) continue;
        char hex[2 * SESSION_DATA + 1] = "-";
        if (r->has_data) hex_of(r->data, SESSION_DATA, hex);
        sfmt(line, sizeof line, "F\t%s\t%s\t%d\t%u\t%s", r->ubi, r->name, r->online ? 1 : 0, (unsigned)r->session, hex);
        gp_send("%s", line);
    }
#ifdef SCBL_DEV
    /* developer build: a pretend friend "test", so the invite list has someone to pick */
    gp_send("F\t%s\ttest\t1\t0\t-", FAKE_FRIEND_UBI);
#endif
    gp_send("END");
}

static void game_account(void)
{
    if (!gp_connected() || !g_have_acct || !g_my_ip) return;
    if (g_two_on) gp_send("BIND"); /* two games on this PC: each on its own address */
    gp_send("ACCOUNT\t%s\t%s\t%s\t%s\t%s", g_my_acct.name, g_my_acct.pass, g_my_acct.ubi, iptext(g_my_ip),
            iptext(vip(g_solo_on ? SOLO_SERVER_LAST : HOST_LAST)));
    if (!g_account_told) { note("game: account %s given to the game", g_my_acct.name); g_account_told = true; }
}

static int roster_encode(uint8_t *m)
{
    int k = 0;
    m[k++] = 'R';
    m[k++] = PROTO;
    m[k++] = (uint8_t)g_nroster;
    for (int i = 0; i < g_nroster; i++) {
        Member *r = &g_roster[i];
        memcpy(m + k, &r->steam, 8);
        k += 8;
        m[k++] = r->online;
        m[k++] = r->inv_only;
        memcpy(m + k, &r->session, 4);
        k += 4;
        m[k++] = r->has_data;
        k = put_str(m, k, r->name);
        k = put_str(m, k, r->ubi);
        if (r->has_data) { memcpy(m + k, r->data, SESSION_DATA); k += SESSION_DATA; }
    }
    return k;
}

/* host: the roster from what it knows; sent to the friends when it changed */
static void host_roster(void)
{
    if (g_mode != MODE_HOST) return;
    int n = 0;
    if (g_have_acct) {
        Member *r = &g_roster[n++];
        memset(r, 0, sizeof *r);
        r->steam = g_me;
        sfmt(r->name, sizeof r->name, "%s", g_my_acct.name);
        sfmt(r->ubi, sizeof r->ubi, "%s", g_my_acct.ubi);
        r->online = g_game_on;
        r->inv_only = g_my_inv_only;
        r->session = g_my_session;
        r->has_data = g_my_has_data;
        memcpy(r->data, g_my_data, SESSION_DATA);
    }
    for (int i = 0; i < g_npeers && n < MAX_PEERS + 1; i++) {
        Peer *p = &g_peers[i];
        if (!p->welcomed || !p->acct_ok) continue;
        Member *r = &g_roster[n++];
        memset(r, 0, sizeof *r);
        r->steam = p->id;
        sfmt(r->name, sizeof r->name, "%s", p->acct.name);
        sfmt(r->ubi, sizeof r->ubi, "%s", p->acct.ubi);
        r->online = p->in_game;
        r->inv_only = p->inv_only;
        r->session = p->session;
        r->has_data = p->has_data;
        memcpy(r->data, p->data, SESSION_DATA);
    }
#ifdef SCBL_DEV
    if (g_two_on && g_g_ok && n < MAX_PEERS + 1) {
        Member *r = &g_roster[n++];
        memset(r, 0, sizeof *r);
        r->steam = GUEST_STEAM;
        sfmt(r->name, sizeof r->name, "%s", g_g_acct.name);
        sfmt(r->ubi, sizeof r->ubi, "%s", g_g_acct.ubi);
        r->online = g_g_game_on;
        r->inv_only = g_g_inv_only;
        r->session = g_g_session;
        r->has_data = g_g_has_data;
        memcpy(r->data, g_g_data, SESSION_DATA);
    }
#endif
    g_nroster = n;
    static uint8_t m[MAX_CTL];
    int k = roster_encode(m);
    if (k == g_roster_sent_n && !memcmp(m, g_roster_sent, (size_t)k)) return;
    memcpy(g_roster_sent, m, (size_t)k);
    g_roster_sent_n = k;
    for (int i = 0; i < g_npeers; i++)
        if (g_peers[i].welcomed) send_ctl(g_peers[i].id, m, k);
    game_friends();
    guest_friends();
}

/* friend: the host's roster */
static void on_roster(const uint8_t *m, int n)
{
    if (n < 3 || m[1] != PROTO || m[2] > MAX_PEERS + 1) return;
    static Member r[MAX_PEERS + 1]; /* static: big, and this build has no stack probes */
    int cnt = m[2], k = 3;
    for (int i = 0; i < cnt; i++) {
        Member *x = &r[i];
        memset(x, 0, sizeof *x);
        if (k + 15 > n) return;
        memcpy(&x->steam, m + k, 8);
        k += 8;
        x->online = m[k++] == 1;
        x->inv_only = m[k++] == 1;
        memcpy(&x->session, m + k, 4);
        k += 4;
        x->has_data = m[k++] == 1;
        if (!get_str(m, n, &k, x->name, NAME_MAX_LEN) || !get_str(m, n, &k, x->ubi, 64)) return;
        if (!name_ok(x->name) || !ubi_ok(x->ubi)) return;
        if (x->has_data) {
            if (k + SESSION_DATA > n) return;
            memcpy(x->data, m + k, SESSION_DATA);
            k += SESSION_DATA;
        }
    }
    if (k != n) return;
    memcpy(g_roster, r, sizeof(Member) * (size_t)cnt);
    g_nroster = cnt;
    game_friends();
}

/* friend -> host: this PC's game */
static void send_presence(void)
{
    if (g_mode != MODE_JOIN || !g_host || !g_have_acct) return;
    uint8_t m[8 + SESSION_DATA] = { 'G', PROTO, g_game_on, g_my_inv_only };
    memcpy(m + 4, &g_my_session, 4);
    m[8 - 0] = 0;
    int k = 8;
    m[k++] = g_my_has_data;
    if (g_my_has_data) { memcpy(m + k, g_my_data, SESSION_DATA); k += SESSION_DATA; }
    send_ctl(g_host, m, k);
}

/* host: a friend's game */
static void on_presence(Peer *p, const uint8_t *m, int n)
{
    if (n < 9 || m[1] != PROTO) return;
    bool has = m[8] == 1;
    if (n != 9 + (has ? SESSION_DATA : 0)) return;
    p->in_game = m[2] == 1;
    p->inv_only = m[3] == 1;
    memcpy(&p->session, m + 4, 4);
    p->has_data = has;
    if (has) memcpy(p->data, m + 9, SESSION_DATA);
    host_roster();
}

static void invite_for_me(const char *from)
{
    sfmt(g_invite_from, sizeof g_invite_from, "%s", from);
    say("%s invited you to their game: click \"Join %s\".", from, from);
    ui_alert("invite");
    gp_send("INVITE\t%s", from);
}

/* host: an invitation to the player with this game account id, from `inviter` (account name) */
/* the host's server: `from` invited the player named `to_name` (its search then finds the room) */
static void invite_tell_server(const Account *from, const char *ubi, const char *to_name)
{
    (void)ubi;
    if (from) own_invite(from->name, to_name);
}

static void route_invite(const char *ubi, const char *inviter, const Account *from)
{
    if (g_have_acct && !strcmp(ubi, g_my_acct.ubi)) {
        invite_for_me(inviter);
        invite_tell_server(from, ubi, g_my_acct.name);
        return;
    }
#ifdef SCBL_DEV
    if (!strcmp(ubi, FAKE_FRIEND_UBI)) { note("DEV: invitation sent to the pretend friend \"test\" (nobody receives it)"); return; }
    if (g_two_on && g_g_ok && !strcmp(ubi, g_g_acct.ubi)) {
        gpx_send(1, "INVITE\t%s", inviter);
        say("DEV: %s invited the second game (%s): press F5 in it.", inviter, g_g_acct.name);
        invite_tell_server(from, ubi, g_g_acct.name);
        return;
    }
#endif
    for (int i = 0; i < g_npeers; i++) {
        Peer *p = &g_peers[i];
        if (p->welcomed && p->acct_ok && !strcmp(p->acct.ubi, ubi)) {
            uint8_t m[4 + NAME_MAX_LEN] = { 'I', PROTO };
            int k = put_str(m, 2, inviter);
            send_ctl(p->id, m, k);
            note("invitation from %s to %s", inviter, p->acct.name);
            invite_tell_server(from, ubi, p->acct.name);
            return;
        }
    }
    note("! invitation for an unknown player");
}

static void on_invite(Peer *p, const uint8_t *m, int n)
{
    if (n < 3 || m[1] != PROTO) return;
    int k = 2;
    if (g_mode == MODE_HOST) {
        char ubi[65];
        if (!p->acct_ok || !get_str(m, n, &k, ubi, 64) || k != n || !ubi_ok(ubi)) return;
        route_invite(ubi, p->acct.name, &p->acct);
    } else {
        char from[NAME_MAX_LEN + 1];
        if (!get_str(m, n, &k, from, NAME_MAX_LEN) || k != n || !name_ok(from)) return;
        invite_for_me(from);
    }
}

/* lines from the game DLL */
static void game_line(char *line)
{
    char *f[5] = { line, 0, 0, 0, 0 };
    int nf = 1;
    for (char *c = line; *c && nf < 5; c++)
        if (*c == '\t') { *c = 0; f[nf++] = c + 1; }
    if (!strcmp(f[0], "CONNECTED")) {
        note("game: the game DLL connected");
        g_account_told = false;
        game_account();
        game_friends();
    } else if (!strcmp(f[0], "HELLO")) {
        g_game_on = true;
        g_presence_dirty = true;
        say("The game is linked to SCBLLink.");
    } else if (!strcmp(f[0], "GONE") || !strcmp(f[0], "BYE")) {
        if (g_game_on) note("game: closed");
        g_game_on = false;
        g_my_session = 0;
        g_my_has_data = false;
        g_presence_dirty = true;
    } else if (!strcmp(f[0], "SESSION") && nf == 4) {
        uint32_t id = 0;
        for (const char *c = f[1]; *c; c++) {
            if (*c < '0' || *c > '9' || id > 400000000u) return;
            id = id * 10 + (uint32_t)(*c - '0');
        }
        g_my_session = id;
        g_my_inv_only = f[2][0] == '1';
        g_my_has_data = false;
        if (id && strlen(f[3]) == 2 * SESSION_DATA) {
            bool ok = true;
            for (int i = 0; i < SESSION_DATA && ok; i++) {
                int v = 0;
                for (int j = 0; j < 2; j++) {
                    char c = f[3][2 * i + j];
                    int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
                    if (d < 0) ok = false;
                    v = v * 16 + d;
                }
                g_my_data[i] = (uint8_t)v;
            }
            g_my_has_data = ok;
        }
        note("game: session %u", (unsigned)id);
        g_presence_dirty = true;
    } else if (!strcmp(f[0], "ACCEPTED") && nf == 2) {
        if (g_invite_from[0]) say("Joining %s's game (accepted in the game).", g_invite_from);
        g_invite_from[0] = 0;
    } else if (!strcmp(f[0], "INVITE") && nf == 2) {
        if (!ubi_ok(f[1])) return;
        if (g_mode == MODE_HOST) route_invite(f[1], g_my_acct.name, g_have_acct ? &g_my_acct : 0);
        else if (g_mode == MODE_JOIN && g_host) {
            uint8_t m[4 + 64] = { 'I', PROTO };
            int k = put_str(m, 2, f[1]);
            send_ctl(g_host, m, k);
        }
    }
}

static void game_tick(void)
{
    static char line[4200];
    while (gp_take(line, sizeof line)) game_line(line);
    if (!g_account_told) game_account();
    if (g_presence_dirty) {
        g_presence_dirty = false;
        if (g_mode == MODE_HOST) host_roster();
        else send_presence();
    }
}

#ifdef SCBL_DEV
/* ---------- the second game on this PC (developer test) ---------- */
static void guest_friends(void)
{
    if (!g_two_on || !gpx_connected(1)) return;
    static char line[2 * SESSION_DATA + 200];
    gpx_send(1, "FRIENDS");
    for (int i = 0; i < g_nroster; i++) {
        Member *r = &g_roster[i];
        if (r->steam == GUEST_STEAM || !r->ubi[0]) continue;
        char hex[2 * SESSION_DATA + 1] = "-";
        if (r->has_data) hex_of(r->data, SESSION_DATA, hex);
        sfmt(line, sizeof line, "F\t%s\t%s\t%d\t%u\t%s", r->ubi, r->name, r->online ? 1 : 0, (unsigned)r->session, hex);
        gpx_send(1, "%s", line);
    }
    gpx_send(1, "END");
}

static void guest_account(void)
{
    if (!g_two_on || !g_g_ok || !gpx_connected(1)) return;
    gpx_send(1, "BIND");
    gpx_send(1, "ACCOUNT\t%s\t%s\t%s\t%s\t%s", g_g_acct.name, g_g_acct.pass, g_g_acct.ubi, iptext(vip(GUEST_LAST)), iptext(vip(HOST_LAST)));
    if (!g_g_told) { note("DEV: account %s given to the second game", g_g_acct.name); g_g_told = true; }
}

static void guest_line(char *line)
{
    char *f[5] = { line, 0, 0, 0, 0 };
    int nf = 1;
    for (char *c = line; *c && nf < 5; c++)
        if (*c == '\t') { *c = 0; f[nf++] = c + 1; }
    if (!strcmp(f[0], "CONNECTED")) {
        note("DEV: the second game's DLL connected");
        g_g_told = false;
        guest_account();
        guest_friends();
    } else if (!strcmp(f[0], "HELLO")) {
        g_g_game_on = true;
        say("DEV: the second game is linked (as %s, %s).", g_g_acct.name, iptext(vip(GUEST_LAST)));
        host_roster();
    } else if (!strcmp(f[0], "GONE") || !strcmp(f[0], "BYE")) {
        if (g_g_game_on) say("DEV: the second game closed.");
        g_g_game_on = false;
        g_g_session = 0;
        g_g_has_data = false;
        host_roster();
    } else if (!strcmp(f[0], "SESSION") && nf == 4) {
        uint32_t id = 0;
        for (const char *c = f[1]; *c; c++) {
            if (*c < '0' || *c > '9' || id > 400000000u) return;
            id = id * 10 + (uint32_t)(*c - '0');
        }
        g_g_session = id;
        g_g_inv_only = f[2][0] == '1';
        g_g_has_data = false;
        if (id && strlen(f[3]) == 2 * SESSION_DATA) {
            bool ok = true;
            for (int i = 0; i < SESSION_DATA && ok; i++) {
                int v = 0;
                for (int j = 0; j < 2; j++) {
                    char c = f[3][2 * i + j];
                    int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
                    if (d < 0) ok = false;
                    v = v * 16 + d;
                }
                g_g_data[i] = (uint8_t)v;
            }
            g_g_has_data = ok;
        }
        note("DEV: second game: session %u", (unsigned)id);
        host_roster();
    } else if (!strcmp(f[0], "ACCEPTED") && nf == 2) {
        say("DEV: the second game accepted the invitation.");
    } else if (!strcmp(f[0], "INVITE") && nf == 2) {
        if (!ubi_ok(f[1])) return;
        route_invite(f[1], g_g_acct.name, &g_g_acct);
    }
}

/* the host's account is made: the second game's too */
static void guest_account_make(void)
{
    if (!g_two_on || g_g_ok) return;
    if (account_ensure(GUEST_STEAM, GUEST_NAME, &g_g_acct)) {
        g_g_ok = true;
        note("DEV: second player's account: %s", g_g_acct.name);
        host_roster();
    } else say("! DEV: the second player's account couldn't be made yet.");
}

static void guest_launch(void)
{
    char exe[800];
    sfmt(exe, sizeof exe, "%s\\Blacklist_DX11_game.exe", g_sys_dir);
    g_g_proc = proc_start(exe, "-scbllink2", g_sys_dir, false);
    if (g_g_proc) say("DEV: starting the second game (%s). Put both in windowed mode to see them side by side.", GUEST_NAME);
    else say("! DEV: couldn't start %s", exe);
}

static void guest_tick(u64 t)
{
    if (!g_two_on) return;
    static char line[4200];
    while (gpx_take(1, line, sizeof line)) guest_line(line);
    if (!g_g_ok && g_have_acct) guest_account_make();
    if (!g_g_told) guest_account();
    /* started a little after the first game linked, so the two don't fight over starting */
    if (!g_g_launched && g_game_on && g_g_ok) {
        if (!g_g_launch_at) g_g_launch_at = t + 15000;
        else if (t >= g_g_launch_at) { g_g_launched = true; guest_launch(); }
    }
}

static void guest_start(void)
{
    if (!g_two) return;
    g_two_on = true;
    g_g_ok = g_g_told = g_g_launched = g_g_game_on = g_g_has_data = false;
    g_g_session = 0;
    g_g_launch_at = 0;
    if (cmd("netsh interface ipv4 add address name=\"" ADAPTER "\" %s 255.255.255.0", iptext(vip(GUEST_LAST))) != 0)
        say("! DEV: couldn't give the adapter the second game's address %s.", iptext(vip(GUEST_LAST)));
    gpx_start(1);
    say("DEV: \"2nd game\" is on: a second copy of the game will start by itself, as %s on %s, after yours links.", GUEST_NAME, iptext(vip(GUEST_LAST)));
}

static void guest_stop(void)
{
    if (!g_two_on) return;
    if (g_g_proc && proc_alive(g_g_proc)) say("! DEV: the second game is still running: close it too.");
    g_g_proc = 0;
    g_two_on = false;
}
#else
static void guest_friends(void) {}
#define guest_tick(t) ((void)0)
#define guest_start() ((void)0)
#define guest_stop() ((void)0)
#endif

static void game_reset(void)
{
    g_nroster = 0;
    g_roster_sent_n = -1;
    g_invite_from[0] = 0;
    g_account_told = false;
    g_presence_dirty = true;
    game_friends();
}

static void on_ctl(u64 from, const uint8_t *m, int n)
{
    if (n < 1) return;
    char c = (char)m[0];
    if (c == 'Q') {
        if (g_mode == MODE_HOST && g_lobby && !kicked(from) && S.Friends_HasFriend(F, from, FRIEND_IMMEDIATE)) {
            uint8_t a[10] = { 'L', PROTO };
            memcpy(a + 2, &g_lobby, 8);
            send_ctl(from, a, 10);
        }
        return;
    }
    if (c == 'L') {
        if (g_mode == MODE_JOIN && !g_lobby && n >= 10 && S.Friends_HasFriend(F, from, FRIEND_IMMEDIATE)) {
            Answer *a = answer_for(from, true);
            a->lobby = rd64(m + 2);
            a->at = now_ms();
        }
        return;
    }
    Peer *p = find_peer(from);
    if (g_mode == MODE_HOST) {
        if (c == 'B') { if (p) drop_peer(from, "left"); return; }
        if (!p) {
            if (!allowed_peer(from)) { note_ignored(from); return; }
            int last = free_last();
            p = last ? add_peer(from, last) : 0;
            if (!p) { say("! %s tried to join, but the game is full.", who(from)); return; }
        }
        p->last_heard = now_ms();
        if (c == 'G' && p->welcomed) { on_presence(p, m, n); return; }
        if (c == 'I' && p->welcomed) { on_invite(p, m, n); return; }
        if (c == 'H') {
            if (n < 2 || m[1] != PROTO) {
                say("! %s runs a different SCBLLink version. Both need " VERSION ".", who(from));
                static const char x[] = "X" VERSION;
                send_ctl(from, x, (int)sizeof x - 1);
                return;
            }
            /* their name: checked like ours (it goes into their account) */
            int nl = n >= 3 ? m[2] : 0;
            char want[NAME_MAX_LEN + 1] = "";
            if (nl <= NAME_MAX_LEN && 3 + nl <= n) { memcpy(want, m + 3, (size_t)nl); want[nl] = 0; }
            if (!name_ok(want)) name_clean(who(from), want);
            if (!p->acct_ok) {
                /* their account on our server; until the server answers, their hello waits (they send it again) */
                if (!g_ready || !account_ensure(from, want, &p->acct)) return;
                p->acct_ok = true;
            }
            if (!p->welcomed) {
                version_notice(from, m + 3 + nl, n - 3 - nl);
                p->welcomed = true;
                say("+ %s connected through Steam as %s (in the game: %s).", who(from), iptext(p->ip), p->acct.name);
                ui_alert("linked");
            }
            uint8_t w[8 + NAME_MAX_LEN + 64 + 64 + 16] = { 'W', PROTO, (uint8_t)p->last, HOST_LAST };
            int k = 4;
            const char *parts[3] = { p->acct.name, p->acct.pass, p->acct.ubi };
            for (int i = 0; i < 3; i++) {
                int l = (int)strlen(parts[i]);
                w[k++] = (uint8_t)l;
                memcpy(w + k, parts[i], (size_t)l);
                k += l;
            }
            int vl = (int)strlen(VERSION);
            memcpy(w + k, VERSION, (size_t)vl);
            send_ctl(from, w, k + vl);
            if (!p->ff_sent) { send_ff(from); p->ff_sent = true; }
            g_roster_sent_n = -1; /* a newcomer gets the roster */
            g_presence_dirty = true;
        }
    } else if (g_mode == MODE_JOIN) {
        if (!p || from != g_host) return;
        g_host_last = now_ms();
        p->last_heard = now_ms();
        if (c == 'Z') {
            say("! %s removed you from their game (kicked).", who(g_host));
            reject(g_lobby);
            lobby_gone();
            return;
        }
        if (c == 'X') {
            say("! The host uses a different SCBLLink version. You both need the same one.");
            return;
        }
        if (c == 'B') { say("! %s closed SCBLLink.", who(g_host)); lobby_gone(); return; }
        if (c == 'F' && p->welcomed) { on_host_ff(m, n); return; }
        if (c == 'R' && p->welcomed) { on_roster(m, n); return; }
        if (c == 'I' && p->welcomed) { on_invite(p, m, n); return; }
        if (c == 'W' && n >= 4 && !p->welcomed) {
            int mine = m[2], host = m[3];
            if (m[1] != PROTO || mine < 2 || mine > 254 || host != HOST_LAST) { say("! The host sent a bad address. Ignoring."); return; }
            /* our game account on the host's server: name, password, id. They go
               to the game, so only the expected characters pass. */
            char f[3][65];
            int k = 4;
            for (int i = 0; i < 3; i++) {
                int l = k < n ? m[k] : -1;
                if (l < 0 || l > 64 || k + 1 + l > n) { say("! The host sent a bad account. Ignoring."); return; }
                memcpy(f[i], m + k + 1, (size_t)l);
                f[i][l] = 0;
                k += 1 + l;
            }
            if (!name_ok(f[0]) || !secret_ok(f[1], 16, 64) || !ubi_ok(f[2])) { say("! The host sent a bad account. Ignoring."); return; }
            memset(&g_my_acct, 0, sizeof g_my_acct);
            sfmt(g_my_acct.name, sizeof g_my_acct.name, "%s", f[0]);
            sfmt(g_my_acct.pass, sizeof g_my_acct.pass, "%s", f[1]);
            sfmt(g_my_acct.ubi, sizeof g_my_acct.ubi, "%s", f[2]);
            version_notice(from, m + k, n - k);
            p->welcomed = true;
            p->last = HOST_LAST;
            p->ip = vip(HOST_LAST);
            say("Connected to %s through Steam. You are %s.", who(g_host), iptext(vip(mine)));
            if (!net_up(mine)) return;
            g_have_acct = true;
            say("Your name in the game: %s", g_my_acct.name);
            g_account_told = false;
            g_presence_dirty = true;
            g_next_check = now_ms();
        }
    }
    if (!p) return;
    if (c == 'K' && n >= 9) { uint8_t r[9]; memcpy(r, m, 9); r[0] = 'k'; send_ctl(from, r, 9); }
    else if (c == 'k' && n >= 9) {
        u64 sent = rd64(m + 1), t = now_ms();
        if (t >= sent && t - sent < 60000) {
            double rtt = (double)(t - sent);
            p->rtt = p->have_rtt ? p->rtt * 0.8 + rtt * 0.2 : rtt;
            p->have_rtt = true;
        }
    }
}

/* ---------- recording the game <-> server traffic (developer build only) ----------
   For building SCBLLink's own server: what the game asks and what it gets. Only the
   server's ports to and from the host's address (login, menus, parties, sessions; not
   the match itself). A .pcap file (raw IPv4) per Host/Join, next to SCBLLink.exe;
   nothing leaves this PC. Players' builds have none of this. */

#ifdef SCBL_DEV
static FILE *g_rec;
static long g_rec_bytes, g_rec_packets;
static char g_rec_path[760];

static bool server_port(uint16_t p) { return p == 21126 || p == 21127 || p == 80 || p == 8000; }

static void rec_open(void)
{
    for (int i = 1; i < 1000; i++) {
        sfmt(g_rec_path, sizeof g_rec_path, "%s\\scbllink-capture-%03d.pcap", exe_dir(), i);
        if (!file_exists(g_rec_path)) break;
    }
    g_rec = fopen(g_rec_path, "wb");
    if (!g_rec) { say("! DEV: could not start recording (%s).", g_rec_path); return; }
    uint32_t h[6] = { 0xa1b2c3d4u, 0x00040002u /* 2.4 */, 0, 0, 65535, 228 /* raw IPv4 */ };
    fwrite(h, 4, 6, g_rec);
    g_rec_bytes = 24;
    g_rec_packets = 0;
    say("DEV: recording the game's talk with the server to %s", g_rec_path);
}

static void rec_close(void)
{
    if (!g_rec) return;
    fclose(g_rec);
    g_rec = 0;
    say("DEV: recording closed: %ld packets in %s", g_rec_packets, g_rec_path);
}

static void rec_packet(const Ip4 *ip, const uint8_t *d, int n)
{
    if (!g_rec) return;
    uint32_t host = vip(HOST_LAST);
    /* later pieces of a split packet have no ports: kept when they're the host's */
    bool srv = ip->has_ports ? (ip->dst == host && server_port(ip->dport)) || (ip->src == host && server_port(ip->sport))
                             : ip->fragment && (ip->dst == host || ip->src == host) && ip->proto == 17;
    if (!srv) return;
    if (g_rec_bytes > 400L * 1024 * 1024) return; /* enough */
    uint32_t t = (uint32_t)now_ms(); /* 32-bit: no 64-bit division helpers in this build */
    uint32_t r[4] = { t / 1000, (t % 1000) * 1000, (uint32_t)n, (uint32_t)n };
    fwrite(r, 4, 4, g_rec);
    fwrite(d, 1, (size_t)n, g_rec);
    g_rec_bytes += 16 + n;
    g_rec_packets++;
    fflush(g_rec);
}

/* Recording alone: this PC's game -> "server at .200", and the server's answers -> "game
   at .250", both come out of the adapter here. Turned around and put back in. */
static bool solo_turn(const Ip4 *ip, const uint8_t *d, int n)
{
    static uint8_t b[MAX_PKT];
    bool to_server = ip->dst == vip(SOLO_SERVER_LAST), to_game = ip->dst == vip(SOLO_GAME_LAST);
    if (!g_solo_on || (!to_server && !to_game)) return false;
    /* only the server's ports (or the rest of a split packet of theirs) */
    if (ip->has_ports ? !server_port(to_server ? ip->dport : ip->sport) : !ip->fragment) return true;
    memcpy(b, d, (size_t)n);
    if (to_game) rec_packet(ip, d, n); /* as the server sent it: .1 -> .250 */
    if (!pkt_readdress(b, n, vip(to_server ? SOLO_GAME_LAST : SOLO_SERVER_LAST), g_my_ip)) return true;
    if (to_server) {
        Ip4 seen;
        if (ip4_parse(b, n, &seen)) rec_packet(&seen, b, n); /* as the server gets it: .250 -> .1 */
    }
    tun_send(b, n);
    return true;
}
#else
#define rec_open() ((void)0)
#define rec_close() ((void)0)
#define rec_packet(ip, d, n) ((void)0)
#define solo_turn(ip, d, n) false
#endif

/* ---------- packets ---------- */

/* From the game on this PC, through the adapter, to the other PC. */
static void from_adapter(const uint8_t *d, int n)
{
    Ip4 ip;
    if (n > MAX_PKT || !ip4_parse(d, n, &ip)) return;
    if (ip.src != g_my_ip || !vip_in_net(ip.dst) || ip.dst == g_my_ip) return;
    if (solo_turn(&ip, d, n)) return; /* developer build, recording alone */
    int v = pkt_verdict(&ip, vip(HOST_LAST), now_ms());
    if (v == PKT_DROP) return;
    rec_packet(&ip, d, n);
    Peer *p = g_mode == MODE_HOST ? peer_of_ip(ip.dst) : (g_npeers && g_peers[0].welcomed ? &g_peers[0] : 0);
    if (!p) return;
    send_msg(p->id, d, n, v == PKT_RELIABLE, v == PKT_RELIABLE ? CH_RELIABLE : CH_FAST);
    p->up++;
}

/* From the other PC, through Steam: checked again before it reaches this PC. */
static void from_steam(Peer *p, const uint8_t *d, int n, int ch)
{
    Ip4 ip;
    if (n > MAX_PKT || !ip4_parse(d, n, &ip)) { p->dropped++; return; }
    /* a friend can only speak for its own address */
    if (ip.src != p->ip || !vip_in_net(ip.dst)) { p->dropped++; return; }
    int v = pkt_verdict(&ip, vip(HOST_LAST), now_ms());
    if (v == PKT_DROP) { p->dropped++; return; }
    p->down++;
    rec_packet(&ip, d, n);
    if (ip.dst == g_my_ip) { tun_send(d, n); return; }
    if (g_mode == MODE_HOST) { /* friend to friend: the host passes it on */
        Peer *to = peer_of_ip(ip.dst);
        if (to && to != p) { send_msg(to->id, d, n, ch == CH_RELIABLE, ch); to->up++; }
    }
}

static void drain_adapter(void)
{
    if (!g_net_up) return;
    for (int i = 0; i < 512; i++) {
        const uint8_t *d;
        int n = tun_recv(&d);
        if (n <= 0) break;
        from_adapter(d, n);
        tun_release(d);
    }
}

static void drain_steam(void)
{
    void *msgs[64];
    int n;
    while ((n = S.NM_ReceiveMessagesOnChannel(NM, CH_CTL, msgs, 64)) > 0) {
        for (int i = 0; i < n; i++) {
            int sz = msg_size(msgs[i]);
            if (sz > 0 && sz <= MAX_CTL) on_ctl(msg_sender(msgs[i]), msg_data(msgs[i]), sz);
            S.Msg_Release(msgs[i]);
        }
    }
    for (int ch = CH_FAST; ch <= CH_RELIABLE; ch += 2) {
        while ((n = S.NM_ReceiveMessagesOnChannel(NM, ch, msgs, 64)) > 0) {
            for (int i = 0; i < n; i++) {
                void *m = msgs[i];
                u64 from = msg_sender(m);
                int sz = msg_size(m);
                Peer *p = find_peer(from);
                if (!p || !p->welcomed) {
                    if (g_mode == MODE_HOST && !p && !allowed_peer(from)) note_ignored(from);
                } else if (g_mode == MODE_HOST && !allowed_peer(from)) {
                    drop_peer(from, "no longer in your lobby or friends list");
                } else if (sz > 0 && sz <= MAX_PKT && g_net_up && rate_ok(&p->rate, now_ms())) {
                    p->last_heard = now_ms();
                    if (g_mode == MODE_JOIN) g_host_last = now_ms();
                    from_steam(p, msg_data(m), sz, ch);
                }
                S.Msg_Release(m);
            }
        }
    }
}

/* ---------- callbacks ---------- */

static void start_join(u64 lobby);

static void handle_cb(int cb, uint8_t *p, int size)
{
    switch (cb) {
    case CB_LobbyCreated:
        if (size >= 16) on_lobby_created((int32_t)rd32(p), rd64(p + 8));
        break;
    case CB_LobbyEnter:
        if (size >= 20) on_lobby_entered(rd64(p), rd32(p + 16));
        break;
    case CB_LobbyChatUpdate: {
        if (size < 28 || rd64(p) != g_lobby) break;
        u64 changed = rd64(p + 8);
        uint32_t st = rd32(p + 24);
        if (st & 1) {
            if (g_mode == MODE_HOST && changed != g_me) say("%s joined your lobby.", who(changed));
        } else if (st & (CHAT_LEFT | CHAT_DISCONNECTED | CHAT_KICKED | CHAT_BANNED)) {
            if (g_mode == MODE_HOST) {
                if (find_peer(changed)) drop_peer(changed, "left the lobby");
            } else if (changed == g_host) {
                say("! %s stopped hosting.", who(g_host));
                lobby_gone();
            }
        }
        break;
    }
    case CB_GameLobbyJoinRequested:
        /* the player accepted a Steam invite: that is their yes, from the start screen too */
        if (size >= 16 && g_mode != MODE_HOST) { say("Accepted invite from %s.", who(rd64(p + 8))); start_join(rd64(p)); }
        else if (size >= 16) say("Got a Steam invite, but you are hosting. Click Stop first, then accept it again.");
        break;
    case CB_NetMsgSessionRequest: {
        u64 from = S.Ident_GetSteamID64(p);
        bool friend_ = S.Friends_HasFriend(F, from, FRIEND_IMMEDIATE);
        bool ok = g_mode == MODE_HOST ? friend_ && !kicked(from)
                : g_mode == MODE_JOIN ? (g_host && from == g_host) || (!g_lobby && friend_)
                : false;
        if (ok) S.NM_AcceptSessionWithUser(NM, p);
        else if (g_mode == MODE_HOST && kicked(from)) tell_kicked(from);
        break;
    }
    case CB_NetMsgSessionFailed: {
        u64 w = size >= 16 ? S.Ident_GetSteamID64(p) : 0;
        if (w && find_peer(w)) say("(Steam: the connection to %s dropped for a moment; it retries.)", who(w));
        break;
    }
    default:
        break;
    }
}

static void pump_callbacks(void)
{
    S.MD_RunFrame(g_pipe);
    CallbackMsg m;
    while (S.MD_GetNextCallback(g_pipe, &m)) {
        if (m.callback == CB_SteamAPICallCompleted && m.size >= 16) {
            u64 call = rd64(m.param);
            int cb = (int)rd32(m.param + 8);
            uint32_t sz = rd32(m.param + 12);
            uint8_t *buf = sz < 65536 ? calloc(1, sz + 16) : 0;
            bool failed = false;
            if (buf && S.MD_GetAPICallResult(g_pipe, call, buf, (int)sz, cb, &failed) && !failed) handle_cb(cb, buf, (int)sz);
            else if (cb == CB_LobbyEnter) g_joining = 0;
            free(buf);
        } else {
            handle_cb(m.callback, m.param, m.size);
        }
        S.MD_FreeLastCallback(g_pipe);
    }
}

/* ---------- modes ---------- */

static void stop_mode(void)
{
    g_step[0] = 0;
    g_chk_tries = 0;
    if (g_mode == MODE_IDLE) return;
    g_ff_host_rules = false;
    if (game_running()) say("! Splinter Cell: Blacklist is still running. Close it too: it can't reach the server any more.");
    for (int i = 0; i < g_npeers; i++) {
        Peer *p = &g_peers[i];
        if (p->welcomed) say("Link with %s: %s packets sent, %s received, %s blocked.", who(p->id), idstr(p->up), idstr(p->down), idstr(p->dropped));
        send_ctl1(p->id, 'B');
        close_session(p->id);
    }
    g_npeers = 0;
    if (g_lobby) S.MM_LeaveLobby(MM, g_lobby);
    g_lobby = g_host = g_joining = 0;
    S.Friends_ClearRichPresence(F);
    own_stop();
    guest_stop();
    net_down();
    g_nkicked = 0;
    g_nrejected = 0;
    g_ready = false;
    g_have_acct = false;
    game_reset();
    rec_close();
#ifdef SCBL_DEV
    g_solo_on = false;
#endif
    g_mode = MODE_IDLE;
    g_last_found = -1;
    ui_list(LIST_LINKED, 0, 0);
    ui_list(LIST_HOSTING, 0, 0);
    say("Stopped. The SCBLLink adapter and its firewall rules are removed.");
}

static bool preflight(void)
{
    if (!find_game()) { say("! Splinter Cell: Blacklist was not found. Is it installed through Steam?"); return false; }
    if (game_running()) { say("! Close Splinter Cell: Blacklist first: it has to start after SCBLLink is ready."); return false; }
    if (!fix_ready()) {
        say("! The SCBL Fix isn't installed in the game yet: click \"Install SCBL Fix\" first.");
        return false;
    }
    return true;
}

/* SCBLLink's own server: is this the game account of someone in the lobby? */
static bool own_login(const char *name, const char *pass)
{
    if (g_have_acct && !strcmp(name, g_my_acct.name) && !strcmp(pass, g_my_acct.pass)) return true;
    for (int i = 0; i < g_npeers; i++) {
        Peer *p = &g_peers[i];
        if (p->welcomed && p->acct_ok && !strcmp(name, p->acct.name) && !strcmp(pass, p->acct.pass)) return true;
    }
#ifdef SCBL_DEV
    if (g_two_on && g_g_ok && !strcmp(name, g_g_acct.name) && !strcmp(pass, g_g_acct.pass)) return true;
#endif
    return false;
}
static bool own_ubi_name(const char *ubi, char name[64])
{
    if (g_have_acct && !strcmp(ubi, g_my_acct.ubi)) { sfmt(name, 64, "%s", g_my_acct.name); return true; }
    for (int i = 0; i < g_npeers; i++) {
        Peer *p = &g_peers[i];
        if (p->welcomed && p->acct_ok && !strcmp(ubi, p->acct.ubi)) { sfmt(name, 64, "%s", p->acct.name); return true; }
    }
#ifdef SCBL_DEV
    if (!strcmp(ubi, FAKE_FRIEND_UBI)) { sfmt(name, 64, "test"); return true; }
    if (g_two_on && g_g_ok && !strcmp(ubi, g_g_acct.ubi)) { sfmt(name, 64, "%s", g_g_acct.name); return true; }
#endif
    return false;
}
static void own_log(const char *line) { if (line[0] == '!') say("%s", line); else note("%s", line); }

/* the game's server, on this PC's SCBLLink address only */
/* A new address isn't usable at once: Windows first checks that nobody else on the network
   has it (a second or two). Until then nothing can bind to it, so wait for it. */
static bool addr_wait(uint32_t ip, int max_ms)
{
    for (int waited = 0;; waited += 250) {
        sock_t s = udp_open(ip, 0, 0);
        if (s != BADSOCK) {
            sock_close(s);
            note("the address %s was usable after %d ms", iptext(ip), waited);
            return true;
        }
        if (waited >= max_ms) return false;
        sleep_ms(250);
    }
}

static bool server_start(void)
{
#ifdef SCBL_DEV
    bool verbose = true; /* every call in the log */
#else
    bool verbose = false;
#endif
    if (!own_start(vip(HOST_LAST), vip(g_solo_on ? SOLO_SERVER_LAST : HOST_LAST), own_login, own_ubi_name, own_log, verbose)) return false;
    say("Game server started (it only listens on %s).", iptext(vip(HOST_LAST)));
    return true;
}

static void start_host(void)
{
    if (g_mode == MODE_HOST) { say("You are already hosting (lobby code %s). Click Stop first to start over.", g_lobby ? codestr(g_lobby) : "not ready yet"); return; }
    if (g_mode != MODE_IDLE) stop_mode();
    if (!preflight()) return;
    g_mode = MODE_HOST;
#ifdef SCBL_DEV
    g_solo_on = g_solo;
#endif
    say("Hosting: setting up. This takes a few seconds; the steps show here and at the top.");
    if (!net_up(HOST_LAST)) { stop_mode(); return; }
    step("Hosting: waiting for Windows to finish the adapter...");
    if (!addr_wait(vip(HOST_LAST), 10000)) note("the address %s still isn't usable after 10 s", iptext(vip(HOST_LAST)));
    step("Hosting: starting the game's server...");
    if (!server_start()) { stop_mode(); return; }
    guest_start();
    rec_open();
    if (g_solo_on) say("! DEV: recording alone. Your own game is recorded; friends can't play in this mode (turn \"Record alone\" off to play together).");
    say("Creating a friends-only Steam lobby...");
    S.MM_CreateLobby(MM, LOBBY_FRIENDS_ONLY, MAX_PEERS + 1);
    g_step[0] = 0; /* from here the status follows the lobby and the server check */
    g_chk_tries = 0;
    g_next_check = now_ms() + 500;
}

static void start_join(u64 lobby)
{
    if (g_mode == MODE_HOST) stop_mode();
    if (g_mode == MODE_IDLE) {
        if (!preflight()) return;
        g_mode = MODE_JOIN;
        g_chk_tries = 0;
        rec_open();
        say("Looking for a friend who is hosting...");
    }
    if (lobby) request_join(lobby);
}

static void kick(u64 id)
{
    if (g_mode != MODE_HOST || !id) return;
    if (!kicked(id) && g_nkicked < 32) { g_kick_told[g_nkicked] = 0; g_kicked[g_nkicked++] = id; }
    tell_kicked(id);
    if (find_peer(id)) drop_peer(id, "kicked by you");
    say("%s is kicked until you stop hosting.", who(id));
}

static void status(void)
{
    say("Mode: %s. Adapter: %s. Game: %s. Server check: %s.",
        g_mode == MODE_HOST ? "hosting" : g_mode == MODE_JOIN ? "joining" : "stopped",
        g_net_up ? iptext(g_my_ip) : "off", game_running() ? "running" : "not running", g_ready ? "ready" : "not ready");
    if (g_mode == MODE_HOST) say("Server: %s.", own_running() ? "running" : "not running");
    for (int i = 0; i < g_npeers; i++) {
        Peer *p = &g_peers[i];
        say("  %s (%s): %s, ping %d ms, sent %s, received %s, blocked %s.", who(p->id),
            p->ip ? iptext(p->ip) : "-", p->welcomed ? "linked" : "connecting",
            p->have_rtt ? (int)(p->rtt + 0.5) : -1, idstr(p->up), idstr(p->down), idstr(p->dropped));
    }
}

static void start_game(void)
{
    if (g_mode == MODE_IDLE) { say("Click \"Host a game\" or \"Join a friend\" first."); return; }
    if (!g_ready) { say("! Not ready yet: the host's server doesn't answer. Click \"Test the link\" to check again."); return; }
    if (game_running()) { say("The game is already running."); return; }
    open_url("steam://rungameid/235600");
    say("Starting Splinter Cell: Blacklist through Steam...");
}

/* ---------- loop ---------- */

static void tick(u64 t)
{
    if (g_mode == MODE_JOIN && !g_lobby && !g_joining) {
        static u64 last_scan;
        if (t - last_scan > 2000) { last_scan = t; scan_for_host(); }
    }
    if (g_mode == MODE_JOIN && g_lobby && g_npeers && !g_peers[0].welcomed && t - g_last_hello > 2000) send_hello();
    if (g_mode == MODE_JOIN && g_lobby && t - g_host_last > PEER_TIMEOUT_MS) {
        say("! Nothing from %s for 30 s.", who(g_host));
        g_host_last = t;
    }
    for (int i = 0; i < g_npeers; i++) {
        Peer *p = &g_peers[i];
        if (g_mode == MODE_HOST && t - p->last_heard > PEER_TIMEOUT_MS) { drop_peer(p->id, "no answer for 30 s"); i--; continue; }
        if (p->welcomed && t - p->last_ka >= KEEPALIVE_MS) {
            uint8_t k[9] = { 'K' };
            memcpy(k + 1, &t, 8);
            send_ctl(p->id, k, 9);
            p->last_ka = t;
        }
    }
    /* keep checking until ready (every second at first, quietly, then every 5 s), then every
       15 s quietly. Not before IPv6 is off the adapter: the game must start after that. */
    /* Balaclava, Crosshair: write their files as soon as the game DLL has saved the game's own ones */
    static u64 last_pec;
    if (t - last_pec > 5000) { last_pec = t; opts_sync(false); }
    v6_report();
    if (g_net_up && g_mode != MODE_IDLE && t >= g_next_check && g_chk_state == CHK_IDLE && g_v6_state != 1 &&
        (g_mode == MODE_HOST || (g_npeers && g_peers[0].welcomed))) {
        bool early = g_chk_tries < 5;
        check_start(g_ready || early);
        g_chk_tries++;
        g_next_check = t + (g_ready ? 15000 : early ? 1000 : 5000);
    }
    check_report();
}

/* what the window shows */
static void publish(void)
{
    UiState st;
    memset(&st, 0, sizeof st);
    st.mode = g_mode;
    st.lobby = g_lobby;
    st.ready = g_ready;
    st.game_found = find_game();
    if (st.game_found && !g_ff_checked) ff_check();
    /* "Install SCBL Fix" stays until Fusion Fix is in too (unless another mod's version.dll is there) */
    st.dll_ok = st.game_found && fix_ready();
    st.fix_ok = st.dll_ok && (g_ff_ok || g_ff_blocked);
    st.fix_old = st.game_found && !st.dll_ok && g_fix_old;
    st.ff_ok = st.game_found && g_ff_ok;
    st.game_linked = gp_connected() && g_game_on;
    st.dev_solo = g_solo;
    st.dev_two = g_two;
    sfmt(st.invite_from, sizeof st.invite_from, "%s", g_invite_from);
    st.ff_host_rules = g_mode == MODE_JOIN && g_ff_host_rules;
    if (st.ff_ok) sys_path(st.ff_ini, sizeof st.ff_ini, FF_INI);
    {
        static char vpath[400];
        static u64 vat;
        if (!vat || now_ms() - vat > 5000) { vat = now_ms(); if (!video_ini_path(vpath, sizeof vpath)) vpath[0] = 0; }
        sfmt(st.video_ini, sizeof st.video_ini, "%s", vpath);
    }
    static u64 last_scan;
    static bool running;
    if (now_ms() - last_scan > 2000) { last_scan = now_ms(); running = game_running(); }
    st.game_running = running;
    sfmt(st.my_name, sizeof st.my_name, "%s", g_name);
    if (g_net_up) vip_text(g_my_ip, st.my_ip);
    int linked = 0;
    for (int i = 0; i < g_npeers; i++) linked += g_peers[i].welcomed;
    st.linked = linked > 0;
    if (!g_steam_ok) sfmt(st.status, sizeof st.status, "!Steam is not ready - see the log");
    else if (!st.game_found) sfmt(st.status, sizeof st.status, "!Splinter Cell: Blacklist was not found");
    else if (g_mode == MODE_IDLE && running) sfmt(st.status, sizeof st.status, "!Close Blacklist first: it has to start after SCBLLink");
    else if (g_mode == MODE_IDLE && st.fix_old) sfmt(st.status, sizeof st.status, "!Click \"Update SCBL Fix\" first");
    else if (g_mode == MODE_IDLE && !st.dll_ok) sfmt(st.status, sizeof st.status, "!Click \"Install SCBL Fix\" first");
    else if (g_mode == MODE_IDLE) sfmt(st.status, sizeof st.status, "Ready to host or join");
    else if (g_step[0]) sfmt(st.status, sizeof st.status, "%s", g_step);
    else if (g_mode == MODE_HOST) {
        if (!g_lobby) sfmt(st.status, sizeof st.status, "Hosting: opening your Steam lobby...");
        else if (!g_ready) sfmt(st.status, sizeof st.status, "Hosting: starting the game's server...");
        else if (!linked) sfmt(st.status, sizeof st.status, "Ready - waiting for a friend to join");
        else sfmt(st.status, sizeof st.status, "Ready - %d friend%s linked", linked, linked == 1 ? "" : "s");
    } else {
        if (!g_lobby) sfmt(st.status, sizeof st.status, g_joining ? "Joining the lobby..." : "Looking for a friend who hosts...");
        else if (!linked) sfmt(st.status, sizeof st.status, "Connecting to %s through Steam...", who(g_host));
        else if (!g_ready) sfmt(st.status, sizeof st.status, "Linked to %s - waiting for the host's server", who(g_host));
        else sfmt(st.status, sizeof st.status, "Ready - linked to %s", who(g_host));
    }
    ui_state(&st);
    if (g_mode == MODE_HOST) {
        UiPerson list[MAX_PEERS];
        int n = 0;
        for (int i = 0; i < g_npeers; i++) {
            if (!g_peers[i].welcomed) continue;
            list[n].id = g_peers[i].id;
            list[n].lobby = 0;
            sfmt(list[n].name, UI_NAME, "%s", who(g_peers[i].id));
            n++;
        }
        ui_list(LIST_LINKED, n, list);
    }
}

static bool handle(const Cmd *c)
{
    switch (c->kind) {
    case CMD_HOST: if (g_steam_ok) start_host(); break;
    case CMD_JOIN:
        if (!g_steam_ok) break;
        if (g_mode == MODE_JOIN) { g_last_found = -1; break; }
        start_join(0);
        break;
    case CMD_JOIN_LOBBY: if (g_steam_ok) start_join(c->arg); break;
    case CMD_LAUNCH: start_game(); break;
    case CMD_TEST:
        if (!g_net_up) { say("Not linked yet."); break; }
        if (g_chk_state == CHK_IDLE) { say("Checking the host's server..."); g_ready = false; check_start(false); }
        break;
    case CMD_DETAILS: status(); break;
    case CMD_KICK: kick(c->arg); break;
    case CMD_SET_NAME: {
        char n[NAME_MAX_LEN + 1];
        name_load(g_steam_ok ? S.Friends_GetPersonaName(F) : "Player", n);
        if (strcmp(n, g_name) != 0) {
            sfmt(g_name, sizeof g_name, "%s", n);
            say(g_mode == MODE_IDLE ? "Your name in the game is now %s." : "Your name in the game is now %s (from the next game: click Stop, then Host or Join again).", g_name);
        }
        break;
    }
    case CMD_ACCEPT:
        if (g_invite_from[0]) {
            gp_send("ACCEPT\t%s", g_invite_from);
            say(gp_connected() ? "Joining %s's game..." : "! The game isn't linked to SCBLLink: start it with \"Start Blacklist\" first (%s).", g_invite_from);
            if (gp_connected()) g_invite_from[0] = 0;
        }
        break;
    case CMD_DEV_SOLO:
#ifdef SCBL_DEV
        if (g_mode != MODE_IDLE) { say("! DEV: click Stop first, then change \"Record alone\"."); break; }
        g_solo = !g_solo;
        if (g_solo && g_two) { g_two = false; say("DEV: \"2nd game\" is off: it can't go with recording alone."); }
        say(g_solo ? "DEV: Record alone is ON: Host, then Start Blacklist. Your own game's talk with the server is recorded."
                   : "DEV: Record alone is off: hosting works as usual (a friend's game is recorded).");
#endif
        break;
    case CMD_DEV_TWO:
#ifdef SCBL_DEV
        if (g_mode != MODE_IDLE) { say("! DEV: click Stop first, then change this."); break; }
        g_two = !g_two;
        if (g_two && g_solo) { g_solo = false; say("DEV: \"Record alone\" is off: it can't go with a second game."); }
        say(g_two ? "DEV: the next Host starts a second copy of the game on this PC, as a second player (%s). Your game must be started first."
                  : "DEV: no second game.", GUEST_NAME);
#endif
        break;
    case CMD_REFRESH:
        gp_send("REFRESH");
        say("Asked the game to read the friend list again.");
        break;
    case CMD_FF_SAVE: {
        static uint8_t buf[2048];
        int n = ui_take_ff(buf, sizeof buf);
        if (n <= 0 || !ff_check()) break;
        /* joined: the host's co-op settings stay; only this player's own change */
        FfApply a = { buf, n, g_ff_host_rules ? FF_PERSONAL : FF_ALL, 0 };
        if (!ff_edit(ff_apply_cb, &a)) break;
        int vc = video_apply(buf, n);
        if (vc < 0) say("! Could not save the game's videoSettings.ini (motion blur, occlusion): is it read-only?");
        else a.applied += vc;
        if (vc > 0 && game_running())
            say("! Blacklist is running: it may write its videoSettings.ini again when it closes. If motion blur comes back, save these settings again with the game closed.");
        say("Game settings saved (%d changed). They count from the next start of Blacklist.", a.applied);
        opts_sync(true);
        if (g_mode == MODE_HOST) send_ff_all();
        break;
    }
    case CMD_FIX_INSTALL:
        if (g_mode != MODE_IDLE) { say("Click Stop first."); break; }
        fix_install();
        break;
    case CMD_FIX_REMOVE:
        if (g_mode != MODE_IDLE) { say("Click Stop first."); break; }
        fix_remove();
        break;
    case CMD_STOP: stop_mode(); break;
    case CMD_QUIT: return false;
    default: break;
    }
    return true;
}

static bool steam_start(void)
{
    file_write_text("steam_appid.txt", "480");
    const char *e = steam_load();
    if (e) { say("! %s", e); return false; }
    if (!S.IsSteamRunning()) { say("! Steam is not running. Start Steam, log in, then try again."); return false; }
    char err[1024] = { 0 };
    int r = S.InitFlat(err);
    if (r != 0) { say("! Steam would not start the connection (code %d): %s", r, err); return false; }
    S.MD_Init();
    g_pipe = S.GetHSteamPipe();
    U = S.SteamUser();
    F = S.SteamFriends();
    MM = S.SteamMatchmaking();
    NM = S.SteamNetMsgs();
    NU = S.SteamNetUtils();
    AP = S.SteamApps ? S.SteamApps() : 0;
    if (!U || !F || !MM || !NM || !NU) { say("! Steam interfaces unavailable."); return false; }
    if (!S.User_BLoggedOn(U)) { say("! Steam is in offline mode. Go online and try again."); S.Shutdown(); return false; }
    g_me = S.User_GetSteamID(U);
    S.NU_InitRelayNetworkAccess(NU);
    /* always through Valve's relay: never share either player's IP address */
    if (!S.NU_SetGlobalConfigInt32 || !S.NU_SetGlobalConfigInt32(NU, 104 /* P2P_Transport_ICE_Enable */, 0))
        say("! Steam would not turn off direct connections; your IP may be shared with the other player.");
    say("Signed in to Steam as %s.", who(g_me));
    return true;
}

void engine_run(void)
{
    set_title("SCBLLink " VERSION);
    say("SCBLLink " VERSION " - Splinter Cell: Blacklist co-op with Steam friends. By Mhdshehri.");
    say("Test build: click Install SCBL Fix once.");
#ifdef SCBL_DEV
    say("! DEVELOPER BUILD: it records the game's talk with the server into scbllink-capture-NNN.pcap. Don't give this one to players.");
#endif
    g_steam_ok = steam_start();
    gp_start(); /* SCBLLink's own game DLL talks to us through a pipe */
    if (g_steam_ok) {
        firewall_remove(); /* leftover rules after a crash */
        name_load(S.Friends_GetPersonaName(F), g_name);
        say("Your name in the game: %s (change it at the top of the window).", g_name);
        if (find_game()) {
            say("Game folder: %s", g_game_dir);
            if (!game_running()) old_files_tidy();
        }
        else say("! Splinter Cell: Blacklist was not found through Steam.");
    }
    bool run = true;
    while (run) {
        Cmd c;
        while (ui_poll(&c)) if (!handle(&c)) run = false;
        if (!run) break;
        if (g_steam_ok) {
            pump_callbacks();
            drain_steam();
            drain_adapter();
            own_poll();
            guest_tick(now_ms());
            tick(now_ms());
            game_tick();
        }
        static u64 last_pub;
        if (now_ms() - last_pub > 200) { last_pub = now_ms(); publish(); }
        if (g_net_up) tun_wait(1);
        else sleep_ms(5);
    }
    stop_mode();
    if (g_steam_ok) S.Shutdown();
}
