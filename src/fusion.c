/* fusion.c - Fusion Fix's settings and its .ini, see fusion.h. By Mhdshehri. */
#include "fusion.h"

/* The keys of the Fusion Fix release SCBLLink installs (its SplinterCellBlacklist.FusionFix.ini).
   shared: they change what a co-op mission is (enemies, maps, failing, unlocks), so the host's
   values go to the friends; the rest is each player's own (screen, mouse, intro). */
const FfSetting FF_SETTINGS[] = {
    /* each player's own (the order is the link protocol's: add at the end only) */
    { "MAIN", "SkipIntro", "Skip the intro videos", FF_BOOL, 0, 1, 0, false, false, false, "1",
      "The company logo videos when the game starts." },
    { "MAIN", "SkipPressAnyKeyScreen", "Skip the \"Press any key\" screen", FF_BOOL, 0, 1, 0, false, false, false, "1",
      "Goes straight to the menu. Also skips the \"continue from the checkpoint?\" question." },
    { "MAIN", "DisableNegativeMouseAcceleration", "Smooth mouse (no slow-down)", FF_BOOL, 0, 1, 0, false, false, false, "1",
      "The game slows the camera down when you move the mouse fast. This turns that off." },
    { "MAIN", "UltraWideSupport", "Wide screens (21:9, 32:9)", FF_BOOL, 0, 1, 0, false, false, false, "1",
      "On a screen wider than 16:9 the game fills it; the HUD and menus stay 16:9. Changes nothing on a normal 16:9 screen." },
    { "MAIN", "DisableNightVisionFlash", "No white flash for night vision", FF_BOOL, 0, 1, 0, false, false, false, "1",
      "The bright flash every time night vision turns on." },
    { "CAMPAIGN", "EnableRunDuringForcedWalk", "Run in the walk-only parts", FF_BOOL, 0, 1, 0, false, false, false, "1",
      "Story parts where the game makes Sam walk slowly: you can run there." },
    { "CAMPAIGN", "DisableMissionFailOnDetection", "Story: no game over when seen", FF_BOOL, 0, 1, 0, false, false, false, "0",
      "Story missions (alone) that end when an enemy sees you go on instead." },
    { "LOGITECH", "LightSyncRGB", "Logitech RGB lighting", FF_BOOL, 0, 1, 0, false, false, false, "1",
      "Your Logitech G keyboard and mouse light up in your suit's colour. Needs Logitech G HUB; does nothing otherwise." },
    { "MAIN", "FOVFactor", "Field of view", FF_FLOAT, 0.5, 2.5, 0, false, false, false, "1.0",
      "How wide you see: 1.00 is the game's own, 1.20 is 20% wider, 0.80 narrower. Most players like 1.10 to 1.30." },
    { "MAIN", "ScreenCullBias", "Hide far objects", FF_FLOAT, 0, 1, 0, false, false, false, "0.0",
      "0.00 keeps far objects on screen (best). 1.00 is the game's own: far objects disappear." },
    { "MAIN", "TurnSpeed", "Turn speed", FF_FLOAT, 0.5, 10, 0, false, false, false, "1.0",
      "How fast Sam turns toward where you run. 1.00 is the game's own, 2.00 twice as fast." },
    { "MAIN", "FPSLimit", "Frame rate limit", FF_INT, 30, 1000, 0, false, false, true, "0",
      "The most frames per second the game draws. 0 = no limit. Example: 144 for a 144 Hz screen." },
    { "MAIN", "DefaultMissionFilter", "Mission list shows", FF_CHOICE, 0, 3, "All|Active|Campaign|4th Echelon", true, false, false, "1",
      "Which missions the world map (the SMI) shows first when you open it." },
    /* the host decides */
    { "UNLOCKS", "UnlockDLC", "Unlock the DLC (gear, weapons)", FF_BOOL, 0, 1, 0, false, true, false, "1",
      "The DLC that can't be bought since Ubisoft closed the servers." },
    { "UNLOCKS", "UnlockAllNonCampaignMissions", "Unlock all side missions", FF_BOOL, 0, 1, 0, false, true, false, "1",
      "All the missions of Grim, Kobin, Charlie and Briggs (4th Echelon), from the start." },
    { "UNLOCKS", "UnlockAllCampaignMissions", "Unlock all story missions", FF_BOOL, 0, 1, 0, false, true, false, "0",
      "Every story mission can be picked from the start." },
    { "MAIN", "DisablePerfectionistChecks", "Perfectionist: keep Mark and Execute", FF_BOOL, 0, 1, 0, false, true, false, "1",
      "On Perfectionist (the hardest difficulty) the game takes away Mark and Execute and the radar. This keeps them." },
    { "COOP", "DisableMissionFailOnDetection", "Briggs (co-op): no game over when seen", FF_BOOL, 0, 1, 0, false, true, false, "0",
      "Briggs' co-op missions that end when an enemy sees you go on instead." },
    { "COOP", "ReinforcementsNumber", "Briggs: extra enemies", FF_CHOICE, 0, 1, "Default|Random", false, true, false, "Default",
      "The enemies that come when the alarm goes off. Default: the game's number, times the number below. Random: a random total between the two numbers below." },
    { "COOP", "ReinforcementsEnemyMultiplier", "Briggs: extra enemies x", FF_INT, 1, 20, 0, false, true, false, "1",
      "With Default: the game's extra enemies times this. 1 = the game's own, 2 = twice as many." },
    { "COOP", "ReinforcementsEnemyRandomRangeMin", "Briggs: Random, from", FF_INT, 1, 200, 0, false, true, false, "10",
      "With Random: the fewest extra enemies. Too many can make the last mission impossible." },
    { "COOP", "ReinforcementsEnemyRandomRangeMax", "Briggs: Random, up to", FF_INT, 1, 200, 0, false, true, false, "20",
      "With Random: the most extra enemies." },
    { "GHOST", "DisableMissionFailOnDetection", "Grim (stealth): no game over when seen", FF_BOOL, 0, 1, 0, false, true, false, "0",
      "Grim's stealth missions that end when an enemy sees you go on instead." },
    { "HUNTER", "ReinforcementsNumber", "Kobin (Hunter): extra enemies", FF_CHOICE, 0, 1, "Default|Random", false, true, false, "Default",
      "Kobin's missions: the enemies that come when you're found. Default: the game's number, times the number below. Random: a random total between the two numbers below." },
    { "HUNTER", "ReinforcementsEnemyMultiplier", "Kobin: extra enemies x", FF_INT, 1, 20, 0, false, true, false, "1",
      "With Default: the game's extra enemies times this. 1 = the game's own." },
    { "HUNTER", "ReinforcementsEnemyRandomRangeMin", "Kobin: Random, from", FF_INT, 1, 200, 0, false, true, false, "1",
      "With Random: the fewest extra enemies." },
    { "HUNTER", "ReinforcementsEnemyRandomRangeMax", "Kobin: Random, up to", FF_INT, 1, 200, 0, false, true, false, "100",
      "With Random: the most extra enemies." },
    { "EXTRACTION", "ExtractionWaveConfigs", "Charlie (waves): enemies", FF_CHOICE, 0, 1, "Default|Random", false, true, false, "Default",
      "Charlie's missions, where enemies come in waves. Default: each wave times the number below. Random: each enemy times a random number between the two below." },
    { "EXTRACTION", "ExtractionWaveEnemyMultiplier", "Charlie: enemies per wave x", FF_INT, 1, 20, 0, false, true, false, "1",
      "With Default: each wave's enemies times this. 1 = the game's own." },
    { "EXTRACTION", "ExtractionWaveEnemyRandomRangeMin", "Charlie: Random x, from", FF_INT, 0, 20, 0, false, true, false, "0",
      "With Random: each enemy of a wave comes this many times at least (0 = may not come)." },
    { "EXTRACTION", "ExtractionWaveEnemyRandomRangeMax", "Charlie: Random x, up to", FF_INT, 1, 20, 0, false, true, false, "4",
      "With Random: each enemy of a wave comes this many times at most." },
    { "EXTRACTION", "EnableKobinAndGrimMaps", "Charlie: add Kobin's and Grim's maps", FF_BOOL, 0, 1, 0, false, true, false, "0",
      "Wave versions of Kobin's and Grim's maps on the world map. They don't count for progress." },
    { "EXTRACTION", "EnableCoopCampaignMaps", "Charlie: add Briggs' maps", FF_BOOL, 0, 1, 0, false, true, false, "0",
      "Wave versions of the 4 Briggs co-op maps. They don't count for progress." },
    { "KESTREL", "EnableKestrelDLC", "Add the cut Kestrel missions", FF_BOOL, 0, 1, 0, false, true, false, "0",
      "4 missions Ubisoft made but never released, on the world map. They don't count for progress." },
};
const int FF_NSETTINGS = (int)(sizeof FF_SETTINGS / sizeof FF_SETTINGS[0]);

/* ---------- small text helpers ---------- */

static bool is_sp(char c) { return c == ' ' || c == '\t'; }

static bool eq_nocase(const char *a, int an, const char *b)
{
    int bn = (int)strlen(b);
    if (an != bn) return false;
    for (int i = 0; i < an; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

/* A line [s, e): is it "[name]" (ignoring what follows)? Sets the name. */
static bool section_of(const char *s, const char *e, const char **ns, int *nn)
{
    while (s < e && is_sp(*s)) s++;
    if (s >= e || *s != '[') return false;
    const char *c = s + 1, *q = c;
    while (q < e && *q != ']') q++;
    if (q >= e) return false;
    *ns = c;
    *nn = (int)(q - c);
    return true;
}

/* A line [s, e): "key = value // comment". Finds the key and the value's span
   (value: up to the comment, trailing spaces cut). */
static bool key_line(const char *s, const char *e, const char *key, const char **vs, const char **ve)
{
    while (s < e && is_sp(*s)) s++;
    const char *k = s;
    while (s < e && !is_sp(*s) && *s != '=') s++;
    if (!eq_nocase(k, (int)(s - k), key)) return false;
    while (s < e && is_sp(*s)) s++;
    if (s >= e || *s != '=') return false;
    s++;
    while (s < e && is_sp(*s)) s++;
    const char *v = s;
    while (s < e && !(s[0] == '/' && s + 1 < e && s[1] == '/') && *s != ';' && *s != '\r' && *s != '\n') s++;
    const char *x = s;
    while (x > v && is_sp(x[-1])) x--;
    *vs = v;
    *ve = x;
    return true;
}

/* Finds [sec] key's value; also where the section ends (to add a missing key). */
static bool find_key(const char *ini, const char *sec, const char *key, const char **vs, const char **ve, const char **sec_end)
{
    bool in = false;
    *sec_end = 0;
    const char *s = ini;
    while (*s) {
        const char *e = s;
        while (*e && *e != '\n') e++;
        const char *ns;
        int nn;
        if (section_of(s, e, &ns, &nn)) {
            if (in) return false; /* left our section */
            in = eq_nocase(ns, nn, sec);
            if (in) *sec_end = *e ? e + 1 : e;
        } else if (in) {
            if (key_line(s, e, key, vs, ve)) return true;
            if (e > s && !(e - s == 1 && *s == '\r')) *sec_end = *e ? e + 1 : e; /* after its last non-empty line */
        }
        s = *e ? e + 1 : e;
    }
    return false;
}

bool ff_ini_get(const char *ini, const char *sec, const char *key, char *out, int n)
{
    const char *vs, *ve, *se;
    if (n <= 0) return false;
    out[0] = 0;
    if (!ini || !find_key(ini, sec, key, &vs, &ve, &se)) return false;
    int l = (int)(ve - vs);
    if (l > n - 1) l = n - 1;
    memcpy(out, vs, (size_t)l);
    out[l] = 0;
    return true;
}

static bool splice(char **ini, size_t at, size_t cut, const char *put)
{
    size_t len = strlen(*ini), pl = strlen(put);
    if (at > len || cut > len - at) return false;
    if (pl > cut) { /* grows: room first */
        char *r = realloc(*ini, len - cut + pl + 1);
        if (!r) return false;
        *ini = r;
    }
    char *r = *ini;
    memmove(r + at + pl, r + at + cut, len - at - cut + 1);
    memcpy(r + at, put, pl);
    return true;
}

bool ff_ini_set(char **ini, const char *sec, const char *key, const char *val)
{
    if (!*ini) { *ini = calloc(1, 1); if (!*ini) return false; }
    const char *vs, *ve, *se;
    if (find_key(*ini, sec, key, &vs, &ve, &se)) {
        size_t at = (size_t)(vs - *ini), old = (size_t)(ve - vs), vl = strlen(val);
        /* keep the comments lined up: pad to the old value's width when there is one after it */
        char put[96];
        bool comment = *ve && *ve != '\r' && *ve != '\n';
        size_t width = vl;
        if (comment) {
            const char *c = ve;
            while (is_sp(*c)) c++;
            width = (size_t)(c - vs);
            if (width < vl + 1) width = vl + 1;
            old = (size_t)(c - vs);
        }
        if (width > sizeof put - 1) width = sizeof put - 1;
        sfmt(put, sizeof put, "%s", val);
        for (size_t i = strlen(put); i < width; i++) put[i] = ' ';
        put[width] = 0;
        return splice(ini, at, old, put);
    }
    char line[160];
    if (se) {
        /* the section is there: add the key after its last line */
        size_t at = (size_t)(se - *ini);
        bool nl_before = at > 0 && (*ini)[at - 1] != '\n';
        sfmt(line, sizeof line, "%s%s = %s\r\n", nl_before ? "\r\n" : "", key, val);
        return splice(ini, at, 0, line);
    }
    size_t len = strlen(*ini);
    bool nl_before = len > 0 && (*ini)[len - 1] != '\n';
    sfmt(line, sizeof line, "%s\r\n[%s]\r\n%s = %s\r\n", nl_before ? "\r\n" : "", sec, key, val);
    return splice(ini, len, 0, line);
}

/* ---------- values ---------- */

/* a plain decimal number: [-]digits[.digits], at most 12 characters */
static bool parse_num(const char *s, double *v, bool *frac)
{
    while (is_sp(*s)) s++;
    int len = (int)strlen(s);
    while (len > 0 && is_sp(s[len - 1])) len--;
    if (len == 0 || len > 12) return false;
    double r = 0, scale = 1;
    bool neg = false, dot = false, digit = false;
    for (int i = 0; i < len; i++) {
        char c = s[i];
        if (c == '-' && i == 0) neg = true;
        else if (c == '.' && !dot) dot = true;
        else if (c >= '0' && c <= '9') {
            digit = true;
            if (dot) { scale /= 10; r += (c - '0') * scale; }
            else r = r * 10 + (c - '0');
        } else return false;
    }
    if (!digit) return false;
    *v = neg ? -r : r;
    *frac = dot;
    return true;
}

bool ff_normalize(int idx, const char *in, char *out, int n)
{
    if (idx < 0 || idx >= FF_NSETTINGS || !in || n < 8) return false;
    const FfSetting *s = &FF_SETTINGS[idx];
    double v;
    bool frac;
    switch (s->kind) {
    case FF_BOOL:
        if (!parse_num(in, &v, &frac) || frac || (v != 0 && v != 1)) return false;
        sfmt(out, (size_t)n, "%d", (int)v);
        return true;
    case FF_INT:
        if (!parse_num(in, &v, &frac) || frac) return false;
        if (!(s->zero_ok && v == 0) && (v < s->min || v > s->max)) return false;
        sfmt(out, (size_t)n, "%d", (int)v);
        return true;
    case FF_FLOAT: {
        if (!parse_num(in, &v, &frac) || v < s->min || v > s->max) return false;
        int c = (int)(v * 100 + 0.5); /* two decimals, written by hand (no %f needed) */
        sfmt(out, (size_t)n, "%d.%02d", c / 100, c % 100);
        return true;
    }
    case FF_CHOICE: {
        int i = 0;
        const char *c = s->choices;
        while (*c) {
            const char *e = strchr(c, '|');
            int l = e ? (int)(e - c) : (int)strlen(c);
            char num[8];
            sfmt(num, sizeof num, "%d", i);
            if (s->numbered ? strcmp(in, num) == 0 : eq_nocase(c, l, in)) {
                if (s->numbered) sfmt(out, (size_t)n, "%d", i);
                else { if (l > n - 1) return false; memcpy(out, c, (size_t)l); out[l] = 0; }
                return true;
            }
            if (!e) break;
            c = e + 1;
            i++;
        }
        return false;
    }
    default: return false;
    }
}

bool ff_harden(char **ini)
{
    /* [STARTUP] DedicatedServerExePath: Fusion Fix would start that program with the game.
       SCBLLink runs the server itself, on the SCBLLink adapter only. */
    char v[8];
    if (ff_ini_get(*ini, "STARTUP", "DedicatedServerExePath", v, sizeof v) && v[0] == 0) return true;
    return ff_ini_set(ini, "STARTUP", "DedicatedServerExePath", "");
}

int ff_pack_shared(const char *ini, uint8_t *out, int cap)
{
    int k = 0;
    for (int i = 0; i < FF_NSETTINGS; i++) {
        if (!FF_SETTINGS[i].shared) continue;
        char raw[64], v[FF_VAL];
        if (!ff_ini_get(ini, FF_SETTINGS[i].sec, FF_SETTINGS[i].key, raw, sizeof raw) || !ff_normalize(i, raw, v, sizeof v))
            sfmt(v, sizeof v, "%s", FF_SETTINGS[i].def);
        int l = (int)strlen(v);
        if (k + 2 + l > cap) return -1;
        out[k++] = (uint8_t)i;
        out[k++] = (uint8_t)l;
        memcpy(out + k, v, (size_t)l);
        k += l;
    }
    return k;
}

int ff_apply(char **ini, const uint8_t *m, int n, int which)
{
    int k = 0, applied = 0;
    while (k < n) {
        if (k + 2 > n) return -1;
        int idx = m[k], l = m[k + 1];
        k += 2;
        if (l >= FF_VAL || k + l > n) return -1;
        char in[FF_VAL], v[FF_VAL];
        memcpy(in, m + k, (size_t)l);
        in[l] = 0;
        k += l;
        /* only the keys asked for, only valid values: from a friend, the rest of the .ini is this player's */
        if (idx >= FF_NSETTINGS || !ff_normalize(idx, in, v, sizeof v)) continue;
        if ((which == FF_SHARED && !FF_SETTINGS[idx].shared) || (which == FF_PERSONAL && FF_SETTINGS[idx].shared)) continue;
        char cur[64];
        if (ff_ini_get(*ini, FF_SETTINGS[idx].sec, FF_SETTINGS[idx].key, cur, sizeof cur) && strcmp(cur, v) == 0) continue;
        if (!ff_ini_set(ini, FF_SETTINGS[idx].sec, FF_SETTINGS[idx].key, v)) return -1;
        applied++;
    }
    return applied;
}

int ff_apply_shared(char **ini, const uint8_t *m, int n) { return ff_apply(ini, m, n, FF_SHARED); }
