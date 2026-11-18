/* account.c - players' names and their game accounts (kept by the host).
 *
 * Each player picks a name once in SCBLLink (saved next to it). The host's
 * SCBLLink makes an account for itself and for every friend who links, for
 * its own server. Friends get their account's details from the host over
 * Steam; nothing is typed in. The host keeps the accounts it made
 * (scbllink_accounts.txt), one per Steam account, so a friend keeps their
 * name and progress from one evening to the next. By Mhdshehri. */
#include "account.h"
#include "plat.h"
#include "sys.h"

/* ---------- names ---------- */

/* What the game and the server take without trouble: letters, digits, _ - .
   3 to 16 characters. Anything else is dropped (spaces become _). */
void name_clean(const char *in, char out[NAME_MAX_LEN + 1])
{
    int k = 0;
    for (int i = 0; in && in[i] && k < NAME_MAX_LEN; i++) {
        char c = in[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.') out[k++] = c;
        else if (c == ' ' && k && out[k - 1] != '_') out[k++] = '_';
    }
    while (k && out[k - 1] == '_') k--;
    out[k] = 0;
}

/* a game account id: random hex, or the account name (older accounts) */
bool ubi_ok(const char *s)
{
    int l = 0;
    for (; s && s[l]; l++) {
        char c = s[l];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
        if (l >= 64) return false;
    }
    return l >= 1;
}

bool name_ok(const char *n)
{
    char c[NAME_MAX_LEN + 1];
    name_clean(n, c);
    int l = (int)strlen(n);
    return l >= NAME_MIN_LEN && l <= NAME_MAX_LEN && strcmp(c, n) == 0;
}

#define NAME_FILE "scbllink_name.txt"

void name_load(const char *steam_name, char out[NAME_MAX_LEN + 1])
{
    out[0] = 0;
    int len = 0;
    char *t = file_read_all(NAME_FILE, &len);
    if (t) {
        for (int i = 0; t[i]; i++) if (t[i] == '\r' || t[i] == '\n') { t[i] = 0; break; }
        if (name_ok(t)) sfmt(out, NAME_MAX_LEN + 1, "%s", t);
        free(t);
    }
    if (out[0]) return;
    name_clean(steam_name, out);
    if ((int)strlen(out) < NAME_MIN_LEN) sfmt(out, NAME_MAX_LEN + 1, "Player");
}

bool name_save(const char *n)
{
    if (!name_ok(n)) return false;
    char t[NAME_MAX_LEN + 4];
    sfmt(t, sizeof t, "%s\r\n", n);
    return file_write_text(NAME_FILE, t);
}

static void random_hex(char *out, int bytes)
{
    uint8_t b[32];
    if (bytes > 32) bytes = 32;
    if (!random_bytes(b, bytes)) { /* never expected; still unique enough for a private server */
        u64 t = now_ms();
        for (int i = 0; i < bytes; i++) b[i] = (uint8_t)(t >> (i % 8 * 8)) ^ (uint8_t)(i * 37);
    }
    static const char H[] = "0123456789abcdef";
    for (int i = 0; i < bytes; i++) { out[2 * i] = H[b[i] >> 4]; out[2 * i + 1] = H[b[i] & 15]; }
    out[2 * bytes] = 0;
}

bool secret_ok(const char *s, int min, int max)
{
    int l = (int)strlen(s);
    if (l < min || l > max) return false;
    for (int i = 0; i < l; i++) if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
    return true;
}

/* ---------- the accounts ---------- */

#define ACCT_FILE "scbllink_accounts.txt"
#define MAX_ACCTS 64
static Account g_acct[MAX_ACCTS];
static int g_nacct = -1;

static void accts_load(void)
{
    if (g_nacct >= 0) return;
    g_nacct = 0;
    int len = 0;
    char *t = file_read_all(ACCT_FILE, &len);
    if (!t) return;
    for (char *l = t; *l && g_nacct < MAX_ACCTS;) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        Account a;
        memset(&a, 0, sizeof a);
        char *f[4] = { 0 };
        int nf = 0;
        for (char *p = l; nf < 4;) {
            f[nf++] = p;
            char *sp = strchr(p, ' ');
            if (!sp) break;
            *sp = 0;
            p = sp + 1;
        }
        if (nf == 4) {
            for (int i = 0; i < 4; i++) { char *r = strchr(f[i], '\r'); if (r) *r = 0; }
            a.steam = str_to_u64(f[0]);
            sfmt(a.name, sizeof a.name, "%s", f[1]);
            sfmt(a.pass, sizeof a.pass, "%s", f[2]);
            sfmt(a.ubi, sizeof a.ubi, "%s", f[3]);
            if (a.steam && name_ok(a.name) && secret_ok(a.pass, 16, 64) && ubi_ok(a.ubi)) g_acct[g_nacct++] = a;
        }
        if (!e) break;
        l = e + 1;
    }
    free(t);
}

static void accts_save(void)
{
    static char t[MAX_ACCTS * 200];
    int k = 0;
    t[0] = 0;
    for (int i = 0; i < g_nacct; i++) {
        char id[24];
        u64_to_str(g_acct[i].steam, id);
        sfmt(t + k, sizeof t - (size_t)k, "%s %s %s %s\r\n", id, g_acct[i].name, g_acct[i].pass, g_acct[i].ubi);
        k += (int)strlen(t + k);
    }
    file_write_text(ACCT_FILE, t);
}

static bool name_taken(const char *n, u64 by)
{
    for (int i = 0; i < g_nacct; i++)
        if (g_acct[i].steam != by && strcmp(g_acct[i].name, n) == 0) return true;
    return false;
}

bool account_ensure(u64 steam, const char *wanted, Account *out)
{
    accts_load();
    char base[NAME_MAX_LEN + 1];
    sfmt(base, sizeof base, "%s", name_ok(wanted) ? wanted : "Player");
    Account *mine = 0;
    for (int i = 0; i < g_nacct; i++) if (g_acct[i].steam == steam) mine = &g_acct[i];
    Account a;
    memset(&a, 0, sizeof a);
    a.steam = steam;
    if (mine) { /* same password and id as before, so the player's progress carries on */
        sfmt(a.pass, sizeof a.pass, "%s", mine->pass);
        sfmt(a.ubi, sizeof a.ubi, "%s", mine->ubi);
    } else {
        random_hex(a.pass, 16);
        random_hex(a.ubi, 16);
    }
    for (int tryn = 0; tryn < 10; tryn++) {
        if (tryn == 0) sfmt(a.name, sizeof a.name, "%s", base);
        else {
            char sfx[8];
            sfmt(sfx, sizeof sfx, "%d", tryn + 1);
            int room = NAME_MAX_LEN - (int)strlen(sfx);
            sfmt(a.name, sizeof a.name, "%.*s%s", room, base, sfx);
        }
        if (name_taken(a.name, steam)) continue;
        if (mine) *mine = a;
        else if (g_nacct < MAX_ACCTS) g_acct[g_nacct++] = a;
        accts_save();
        *out = a;
        return true;
    }
    return false;
}
