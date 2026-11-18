/* uplay.c - SCBLLink's uplay_r1_loader.dll: the 57 functions Splinter Cell:
 * Blacklist calls, answered without Ubisoft Connect. The account, the friends
 * and the invitations come from SCBLLink on this PC (link.c); saves are plain
 * files. The code is SCBLLink's own. All cdecl. By Mhdshehri. */
#include "link.h"

#define EXPORT /* listed in uplay.def */
typedef uintptr_t usize;

void hooks_install(bool may_load);
void overlay_start(void);
void sl_init(void);

/* ---------- shapes the game uses ---------- */

typedef struct { uint32_t unk, completed; usize result; } Overlapped;
typedef struct { usize count; void **items; usize kind; } List;
typedef struct { usize type; usize payload; } Event;

static void ov_done(Overlapped *o, usize result)
{
    if (!o) return;
    o->completed = 1;
    o->result = result;
}

/* Lists this DLL made (handed back through UPLAY_Release). The friend list is the
   game's own struct: never freed here. */
enum { LIST_KEYS = 1, LIST_SAVES = 2 };
static List *g_owned[64];

static List *new_list(usize kind, usize count)
{
    List *l = calloc(1, sizeof *l);
    if (!l) return 0;
    l->kind = kind;
    l->count = count;
    l->items = count ? calloc(count, sizeof(void *)) : 0;
    for (int i = 0; i < 64; i++)
        if (!g_owned[i]) { g_owned[i] = l; break; }
    return l;
}

typedef struct { char *key; } KeyItem;
typedef struct { usize slot; char *name; usize size; } SaveItem;

static void free_list(List *l)
{
    for (usize i = 0; i < l->count; i++) {
        if (!l->items[i]) continue;
        if (l->kind == LIST_KEYS) free(((KeyItem *)l->items[i])->key);
        if (l->kind == LIST_SAVES) free(((SaveItem *)l->items[i])->name);
        free(l->items[i]);
    }
    free(l->items);
    free(l);
}

static char *dup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

/* ---------- core ---------- */

EXPORT usize UPLAY_Startup(usize uplay_id, usize game_version, const char *language)
{
    sl_log("UPLAY_Startup(%u, %u, %s)", (unsigned)uplay_id, (unsigned)game_version, language ? language : "-");
    hooks_install(true); /* what couldn't be hooked while loading */
    overlay_start(); /* before the link: an invitation may come in right away */
    sl_start();
    return 0; /* all good: no Ubisoft Connect */
}

EXPORT bool UPLAY_Update(void) { return true; }

EXPORT bool UPLAY_Quit(void)
{
    sl_log("UPLAY_Quit");
    sl_stop();
    return false;
}

EXPORT bool UPLAY_GetLastError(const char **out) { return false; }

EXPORT bool UPLAY_Release(List *l)
{
    if (!l) return true;
    for (int i = 0; i < 64; i++)
        if (g_owned[i] == l) {
            g_owned[i] = 0;
            free_list(l);
            return true;
        }
    return true; /* not ours (the friend list): left alone */
}

EXPORT bool UPLAY_HasOverlappedOperationCompleted(Overlapped *o) { return o && o->completed; }

EXPORT bool UPLAY_GetOverlappedOperationResult(Overlapped *o, usize *result)
{
    if (!o || !o->completed || !result) return false;
    *result = o->result;
    return true;
}

/* An accepted invitation, as the game reads it: event -> {Foo*} -> {.., Bar*, 496} -> {1, 0, name[128]}. */
typedef struct { usize one, zero; char name[128]; } InviteBar;
typedef struct { usize unknown[2]; InviteBar *bar; usize size496; } InviteFoo;
typedef struct { InviteFoo *foo; } InviteAccepted;
static InviteBar g_bar = { 1, 0, "" };
static InviteFoo g_foo = { { 0, 0 }, &g_bar, 496 };
static InviteAccepted g_accepted = { &g_foo };

EXPORT bool UPLAY_GetNextEvent(Event *e)
{
    SlEvent ev;
    if (!e || !sl_next_event(&ev)) return false;
    if (ev.type == SL_EV_INVITE_ACCEPTED) {
        memset(g_bar.name, 0, sizeof g_bar.name);
        size_t n = strlen(ev.name);
        if (n > sizeof g_bar.name - 1) n = sizeof g_bar.name - 1;
        memcpy(g_bar.name, ev.name, n);
        e->type = SL_EV_INVITE_ACCEPTED;
        e->payload = (usize)&g_accepted;
        sl_log("event: invitation of %s accepted", g_bar.name);
    } else {
        /* the game answers 10000 by reading the friend list again; no payload */
        e->type = SL_EV_FRIENDS_CHANGED;
        e->payload = 0;
        sl_log("event: read the friend list again");
    }
    return true;
}

/* ---------- user ---------- */

/* the game's buffers: 256 (name), 64 (password), 64 (account id) */
static bool give(char *buf, size_t cap, const char *v)
{
    size_t n = strlen(v);
    if (!buf || n >= cap) return false;
    memcpy(buf, v, n + 1);
    return true;
}

EXPORT bool UPLAY_USER_GetUsername(char *buf)
{
    char n[SL_NAME];
    return sl_account(n, 0, 0) ? give(buf, 256, n) : give(buf, 256, "Player");
}

EXPORT bool UPLAY_USER_GetPassword(char *buf)
{
    char p[SL_PASS];
    return sl_account(0, p, 0) ? give(buf, 64, p) : give(buf, 64, "offline");
}

EXPORT bool UPLAY_USER_GetAccountId(char *buf)
{
    char id[SL_ID];
    return sl_account(0, 0, id) ? give(buf, 64, id) : give(buf, 64, "00000000-0000-0000-0000-000000000000");
}

EXPORT bool UPLAY_USER_GetEmail(char *buf) { return false; }
EXPORT bool UPLAY_USER_IsConnected(void) { return true; }

EXPORT bool UPLAY_USER_GetCdKeys(List **out, Overlapped *o)
{
    /* Ubisoft's servers are gone: nothing checks this key; the game only needs one */
    List *l = out ? new_list(LIST_KEYS, 1) : 0;
    if (!l) { ov_done(o, 1); return false; }
    KeyItem *k = calloc(1, sizeof *k);
    if (k) k->key = dup("SCBL-LINK-0000-0000");
    l->items[0] = k;
    *out = l;
    ov_done(o, 0);
    return true;
}

typedef struct { const uint8_t *data; uint32_t size; } SessionWrap;

EXPORT bool UPLAY_USER_SetGameSession(void *session, usize flags, const SessionWrap *w, bool invite_only)
{
    unsigned id = (unsigned)(usize)session; /* a small number, not a pointer: the server's session id */
    static char hex[SL_SESSION_DATA * 2 + 1];
    if (w && w->data && w->size == SL_SESSION_DATA) {
        for (int i = 0; i < SL_SESSION_DATA; i++) sfmt(hex + 2 * i, 3, "%02x", w->data[i]);
    } else {
        hex[0] = '-';
        hex[1] = 0;
        if (w && w->size != SL_SESSION_DATA) sl_log("! session data of %u bytes, not passed on", w ? (unsigned)w->size : 0);
    }
    sl_log("session %u (invite only: %d)", id, invite_only);
    sl_send("SESSION\t%u\t%d\t%s", id, invite_only ? 1 : 0, hex);
    return true;
}

EXPORT bool UPLAY_USER_ClearGameSession(void)
{
    sl_log("session cleared");
    sl_send("SESSION\t0\t0\t-");
    return true;
}

/* ---------- friends ---------- */

typedef struct { usize state; char *u2; usize u3; void *data; } FriendDetails; /* state: 0 online, 2 offline */
typedef struct { char *id; char *name; usize u1, u2; FriendDetails *details; usize u3; } Friend;

EXPORT bool UPLAY_FRIENDS_Init(usize flags) { return true; }

EXPORT bool UPLAY_FRIENDS_GetFriendList(void *filter, List *out)
{
    if (!out) return false;
    /* The game keeps the pointers, so a list is made once per version of it and kept. */
    static uint32_t made_version = 0xFFFFFFFF;
    static void **items;
    static usize count;
    static SlFriend f[SL_MAX_FRIENDS]; /* static: 10 KB, and the game calls this from one thread */
    uint32_t version;
    int n = sl_friends(f, SL_MAX_FRIENDS, &version);
    if (version != made_version) {
        void **it = n ? calloc((size_t)n, sizeof(void *)) : 0;
        int k = 0;
        for (int i = 0; i < n && it; i++) {
            Friend *x = calloc(1, sizeof *x);
            FriendDetails *d = calloc(1, sizeof *d);
            if (!x || !d) break;
            x->id = dup(f[i].id);
            x->name = dup(f[i].name);
            d->state = f[i].online ? 0 : 2;
            if (f[i].has_data) {
                d->data = malloc(SL_SESSION_DATA);
                if (d->data) memcpy(d->data, f[i].data, SL_SESSION_DATA);
            }
            x->details = d;
            it[k++] = x;
        }
        items = it;
        count = (usize)k;
        made_version = version;
    }
    out->count = count;
    out->items = items;
    sl_log("friend list: %u", (unsigned)count);
    return true;
}

EXPORT bool UPLAY_FRIENDS_InviteToGame(const char *account_id, Overlapped *o)
{
    char id[SL_ID];
    size_t n = account_id ? strlen(account_id) : 0;
    if (!n || n >= sizeof id) return false;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)account_id[i] < 0x20) return false;
    memcpy(id, account_id, n + 1);
    sl_log("invite %s", id);
    sl_send("INVITE\t%s", id);
    ov_done(o, 0);
    return true; /* false would make the game say the invitation failed */
}

EXPORT bool UPLAY_FRIENDS_IsFriend(const char *account_id)
{
    if (!account_id) return false;
    SlFriend *f = malloc(sizeof(SlFriend) * SL_MAX_FRIENDS); /* 10 KB: not on the stack */
    if (!f) return false;
    int n = sl_friends(f, SL_MAX_FRIENDS, 0);
    bool yes = false;
    for (int i = 0; i < n && !yes; i++) yes = !strcmp(f[i].id, account_id);
    free(f);
    return yes;
}

EXPORT bool UPLAY_FRIENDS_IsBlackListed(const char *account_id) { return false; }
EXPORT usize UPLAY_FRIENDS_AddToBlackList(void) { return 0; }
EXPORT usize UPLAY_FRIENDS_DisableFriendMenuItem(void) { return 0; }
EXPORT usize UPLAY_FRIENDS_EnableFriendMenuItem(void) { return 0; }
EXPORT usize UPLAY_FRIENDS_RequestFriendship(void) { return 0; }
EXPORT usize UPLAY_FRIENDS_ShowFriendSelectionUI(void) { return 0; }

/* ---------- party (Blacklist only calls Init and InvitePartyToGame) ---------- */

EXPORT bool UPLAY_PARTY_Init(usize flags) { return true; }

EXPORT bool UPLAY_PARTY_InvitePartyToGame(Overlapped *o)
{
    sl_log("party pulled into the game");
    ov_done(o, 0);
    return true;
}

EXPORT usize UPLAY_PARTY_DisablePartyMemberMenuItem(void) { return 0; }
EXPORT usize UPLAY_PARTY_EnablePartyMemberMenuItem(void) { return 0; }
EXPORT usize UPLAY_PARTY_GetFullMemberList(void) { return 0; }
EXPORT usize UPLAY_PARTY_GetInGameMemberList(void) { return 0; }
EXPORT bool UPLAY_PARTY_InviteToParty(const char *id, Overlapped *o) { return false; }
EXPORT bool UPLAY_PARTY_IsInParty(const char *id) { return false; }
EXPORT bool UPLAY_PARTY_IsPartyLeader(const char *id) { return false; }
EXPORT bool UPLAY_PARTY_PromoteToLeader(const char *id, Overlapped *o) { return false; }
EXPORT bool UPLAY_PARTY_RespondToGameInvite(const void *inv, bool accept) { return false; }
EXPORT usize UPLAY_PARTY_SetGuest(void) { return 0; }
EXPORT usize UPLAY_PARTY_SetUserData(void) { return 0; }
EXPORT usize UPLAY_PARTY_ShowGameInviteOverlayUI(void) { return 0; }

/* ---------- saves: <slot as 8 digits>.sav (+ .meta, its name) ---------- */

static char g_saves[400];

static void saves_dir(void)
{
    if (g_saves[0]) return;
    const char *app = getenv("APPDATA");
    if (!app) app = ".";
    char base[400];
    sfmt(base, sizeof base, "%s\\SCBLLink", app);
    sfmt(g_saves, sizeof g_saves, sl_second() ? "%s\\SCBLLink\\Saves2" : "%s\\SCBLLink\\Saves", app);
    if (GetFileAttributesA(g_saves) != 0xFFFFFFFFUL) return;
    if (sl_second()) { CreateDirectoryA(base, 0); CreateDirectoryA(g_saves, 0); return; } /* its own, from nothing */
    CreateDirectoryA(base, 0);
    CreateDirectoryA(g_saves, 0);
}

static void save_path(usize slot, const char *ext, char *out, size_t n)
{
    saves_dir();
    sfmt(out, n, "%s\\%08u.%s", g_saves, (unsigned)slot, ext);
}

typedef struct { usize slot; FILE *f; } SaveHandle;

EXPORT bool UPLAY_SAVE_GetSavegames(List **out, Overlapped *o)
{
    if (!out) { ov_done(o, 1); return false; }
    saves_dir();
    char pat[420];
    sfmt(pat, sizeof pat, "%s\\*.sav", g_saves);
    SaveItem *found[64];
    int n = 0;
    FIND_DATAA d;
    HANDLE h = FindFirstFileA(pat, &d);
    if (h != INVALID_HANDLE) {
        do {
            unsigned slot = 0;
            int k = 0;
            while (d.name[k] >= '0' && d.name[k] <= '9' && k < 9) slot = slot * 10 + (unsigned)(d.name[k++] - '0');
            if (k == 0 || strcmp(d.name + k, ".sav") || n >= 64) continue;
            SaveItem *s = calloc(1, sizeof *s);
            if (!s) break;
            s->slot = slot;
            s->size = d.size_lo;
            char meta[600], name[256] = "";
            save_path(slot, "meta", meta, sizeof meta);
            FILE *m = fopen(meta, "rb");
            if (m) {
                size_t r = fread(name, 1, sizeof name - 1, m);
                name[r] = 0;
                fclose(m);
            }
            s->name = dup(name);
            found[n++] = s;
        } while (FindNextFileA(h, &d));
        FindClose(h);
    }
    List *l = new_list(LIST_SAVES, (usize)n);
    if (!l) { ov_done(o, 6); return false; }
    for (int i = 0; i < n; i++) l->items[i] = found[i];
    *out = l;
    sl_log("saves: %d", n);
    ov_done(o, 0);
    return true;
}

EXPORT bool UPLAY_SAVE_Open(usize slot, usize mode, SaveHandle **out, Overlapped *o)
{
    if (!out || slot > 99999999u) { ov_done(o, 1); return false; }
    char p[600];
    save_path(slot, "sav", p, sizeof p);
    FILE *f = fopen(p, mode == 1 ? "wb" : "rb");
    if (!f) { sl_log("! save %u: can't open (%s)", (unsigned)slot, mode == 1 ? "write" : "read"); ov_done(o, 6); return false; }
    SaveHandle *h = calloc(1, sizeof *h);
    if (!h) { fclose(f); ov_done(o, 6); return false; }
    h->slot = slot;
    h->f = f;
    *out = h;
    ov_done(o, 0);
    return true;
}

EXPORT bool UPLAY_SAVE_Read(SaveHandle *h, usize n, usize offset, uint8_t **buf, usize *got, Overlapped *o)
{
    if (!h || !h->f || !buf || !*buf || !got) { ov_done(o, 1); return false; }
    if (fseek(h->f, (long)offset, 0) != 0) { ov_done(o, 6); return false; }
    *got = fread(*buf, 1, n, h->f);
    ov_done(o, 0);
    return true;
}

EXPORT bool UPLAY_SAVE_Write(SaveHandle *h, usize n, const uint8_t **buf, Overlapped *o)
{
    if (!h || !h->f || !buf || (!*buf && n)) { ov_done(o, 1); return false; }
    if (fwrite(*buf, 1, n, h->f) != n) { sl_log("! save %u: write failed", (unsigned)h->slot); ov_done(o, 6); return false; }
    fflush(h->f);
    ov_done(o, 0);
    return true;
}

EXPORT bool UPLAY_SAVE_SetName(SaveHandle *h, const char *name)
{
    if (!h || !name) return false;
    char p[600];
    save_path(h->slot, "meta", p, sizeof p);
    FILE *f = fopen(p, "wb");
    if (!f) return false;
    size_t n = strlen(name);
    if (n > 255) n = 255;
    fwrite(name, 1, n, f);
    fclose(f);
    return true;
}

EXPORT bool UPLAY_SAVE_Close(SaveHandle *h)
{
    if (!h) return false;
    if (h->f) fclose(h->f);
    free(h);
    return true;
}

EXPORT bool UPLAY_SAVE_Remove(usize slot, Overlapped *o)
{
    char p[600];
    save_path(slot, "sav", p, sizeof p);
    remove(p);
    save_path(slot, "meta", p, sizeof p);
    remove(p);
    ov_done(o, 0);
    return true;
}

/* ---------- the rest: nothing to do ---------- */

EXPORT bool UPLAY_ACH_EarnAchievement(const void *id, Overlapped *o) { return false; }
EXPORT bool UPLAY_ACH_GetAchievementImage(const void *id, void **img, Overlapped *o) { return false; }
EXPORT bool UPLAY_ACH_GetAchievements(const void *filter, const char *acct, void **out, Overlapped *o) { return false; }
EXPORT bool UPLAY_ACH_Write(const void *id) { return false; }
EXPORT bool UPLAY_AVATAR_Get(const char *acct, uint32_t *size, void **rgba, Overlapped *o) { return false; }
EXPORT usize UPLAY_OVERLAY_Show(void) { return 0; }
EXPORT usize UPLAY_PRESENCE_SetRichPresenceLine(void) { return 0; }
EXPORT usize UPLAY_PRESENCE_SetRichPresencePropertyInt32(void) { return 0; }
EXPORT usize UPLAY_PRESENCE_SetRichPresencePropertyString(void) { return 0; }
EXPORT bool UPLAY_WIN_GetRewards(const void **out, Overlapped *o) { return false; }

/* ---------- loading ---------- */

/* A crash (access violation and the like) goes to the log with where it happened, so a
   report can say which code it was. Only noted; the game's own handling goes on. */
typedef struct { DWORD code, flags; void *rec; void *addr; DWORD n; uintptr_t info[15]; } ExcRecord;
typedef struct { ExcRecord *rec; void *ctx; } ExcPointers;
__declspec(dllimport) void *WINAPI AddVectoredExceptionHandler(DWORD first, long(WINAPI *fn)(ExcPointers *));
static HMODULE g_self;

static void where(void *addr, char *out, size_t n)
{
    HMODULE mods[3] = { g_self, GetModuleHandleA(0), 0 };
    const char *names[3] = { "SCBLLink DLL", "game", 0 };
    for (int i = 0; mods[i]; i++) {
        uint8_t *b = mods[i];
        uint32_t pe = *(uint32_t *)(b + 0x3c), size = *(uint32_t *)(b + pe + 24 + 56); /* SizeOfImage */
        if ((uint8_t *)addr >= b && (uint8_t *)addr < b + size) { sfmt(out, n, "%s+0x%x", names[i], (unsigned)((uint8_t *)addr - b)); return; }
    }
    sfmt(out, n, "%p", addr);
}

static long WINAPI on_crash(ExcPointers *e)
{
    static int count;
    DWORD c = e->rec->code;
    if ((c == 0xC0000005 || c == 0xC000001D || c == 0xC0000096 || c == 0xC00000FD) && count < 5) {
        count++;
        char at[64];
        where(e->rec->addr, at, sizeof at);
        sl_log("!! exception 0x%08lx at %s (data %p)", c, at, e->rec->n >= 2 ? (void *)e->rec->info[1] : 0);
    }
    return 0; /* keep looking */
}

BOOL WINAPI DllMain(HMODULE dll, DWORD reason, void *reserved)
{
    if (reason == 1) { /* attach: before the game runs any of its own code */
        DisableThreadLibraryCalls(dll);
        g_self = dll;
        sl_init();
        AddVectoredExceptionHandler(1, on_crash);
        hooks_install(false);
    }
    return 1;
}
