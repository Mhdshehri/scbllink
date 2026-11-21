/* camera.c - the Crosshair choice (see camera.h). By Mhdshehri. */
#include "camera.h"

#define CAM_FIRST "// This file uses 8 as Tab stop value."
#define CAM_SECTIONS 730

static const uint8_t *find_bytes(const uint8_t *h, size_t n, const char *w)
{
    size_t l = strlen(w);
    for (size_t i = 0; i + l <= n; i++)
        if (h[i] == (uint8_t)w[0] && !memcmp(h + i, w, l)) return h + i;
    return 0;
}

static bool texty(uint8_t b) { return b == 0 || b == 9 || b == 10 || b == 13 || b >= 32; }

char *cfg_unsplit(const uint8_t *buf, size_t n, const char *marker)
{
    const uint8_t *m = find_bytes(buf, n, marker);
    if (!m) return 0;
    size_t a = (size_t)(m - buf), b = a;
    while (a > 0 && texty(buf[a - 1]) && buf[a - 1] != 127) a--;
    while (a < n && !buf[a]) a++;
    while (b < n && texty(buf[b]) && buf[b] != 127) b++;
    while (b > a && !buf[b - 1]) b--;
    char *out = malloc(b - a + 1);
    if (!out) return 0;
    size_t o = 0;
    for (size_t p = a; p < b;) {
        size_t e = p;
        while (e < b && buf[e] != '\n') e++;
        size_t end = e;
        bool cr = end > p && !buf[end - 1];
        if (cr) end--;
        size_t s = p;
        while (s < end && (buf[s] == ' ' || buf[s] == '\t')) s++;
        bool head = s < end && buf[s] == '[';
        bool comment = s + 1 < end && buf[s] == '/' && buf[s + 1] == '/';
        bool first = !comment;
        for (size_t i = p; i < end; i++) {
            char c = (char)buf[i];
            if (!c) {
                c = first ? (head ? ']' : '=') : ' ';
                first = false;
            }
            out[o++] = c;
        }
        if (cr) out[o++] = '\r';
        if (e < b) out[o++] = '\n';
        p = e + 1;
    }
    out[o] = 0;
    return out;
}

bool cam_ours(const char *text) { return text && strstr(text, CAM_MARK) != 0; }

/* a line starting at p: its end (at '\n' or the end) */
static const char *line_end(const char *p) { const char *e = strchr(p, '\n'); return e ? e : p + strlen(p); }

/* where each change goes */
typedef struct { const char *section; int ons; bool add; } Change;
static const Change CHANGES[] = {
    { "[Default]",            1, false },   /* on foot, and what other modes take from it */
    { "[_Zoom]",              0, true  },   /* aiming */
    { "[Back2Wall]",          0, true  },   /* in cover */
    { "[DownedShooting]",     1, false },
    { "[COOP_LDE_Initiator]", 1, false },   /* two reticle lines in it: the one that shows it */
    { "[COOP_LDE_Confirmer]", 1, false },
    { "[Hub]",                1, false },
    { "[HubClose]",           1, false },
};
#define NCHANGES ((int)(sizeof CHANGES / sizeof CHANGES[0]))
#define ADD_LINE "reticle=\tt==\tv=0\t\t\t// display reticle? (SCBLLink: crosshair off)\r\n"

static bool is_header(const char *line, const char *name)
{
    size_t l = strlen(name);
    return !strncmp(line, name, l) && (line[l] == '\r' || line[l] == '\n' || !line[l]);
}

/* "reticle=" with a value whose "v=1" turns to "v=0": the address of that '1', or 0 */
static char *reticle_on(char *line)
{
    if (strncmp(line, "reticle=", 8)) return 0;
    const char *e = line_end(line);
    for (char *p = line + 8; p + 2 < e; p++)
        if (p[0] == 'v' && p[1] == '=' && (p[2] == '1' || p[2] == '0')) return p[2] == '1' ? p + 2 : 0;
    return 0;
}

char *cam_crosshair_off(const char *orig)
{
    if (!orig) return 0;
    if (!strncmp(orig, "\xEF\xBB\xBF", 3)) orig += 3;
    if (strncmp(orig, CAM_FIRST, strlen(CAM_FIRST)) || cam_ours(orig)) return 0;
    int sections = 0;
    for (const char *p = orig; *p; p = *line_end(p) ? line_end(p) + 1 : line_end(p))
        if (*p == '[') sections++;
    if (sections != CAM_SECTIONS) return 0;
    size_t n = strlen(orig);
    char *out = malloc(strlen(CAM_MARK) + 2 + n + NCHANGES * sizeof ADD_LINE + 1);
    if (!out) return 0;
    char *o = out;
    memcpy(o, CAM_MARK "\r\n", strlen(CAM_MARK) + 2);
    o += strlen(CAM_MARK) + 2;
    int seen[NCHANGES] = { 0 }, turned[NCHANGES] = { 0 }, cur = -1;
    for (const char *p = orig; *p;) {
        const char *e = line_end(p);
        size_t l = (size_t)(e - p) + (*e ? 1 : 0);
        char *line = o;
        memcpy(o, p, l);
        o += l;
        if (*p == '[') {
            cur = -1;
            for (int c = 0; c < NCHANGES; c++)
                if (is_header(p, CHANGES[c].section)) { cur = c; seen[c]++; }
            if (cur >= 0 && CHANGES[cur].add) {               /* right under the section's name */
                if (!*e) { *o++ = '\r'; *o++ = '\n'; }
                memcpy(o, ADD_LINE, strlen(ADD_LINE));
                o += strlen(ADD_LINE);
            }
        } else if (cur >= 0) {
            *o = 0;
            char *one = reticle_on(line);
            if (one) {
                if (CHANGES[cur].add) { free(out); return 0; } /* it had its own after all: not the file we know */
                *one = '0';
                turned[cur]++;
            }
        }
        p = e + (*e ? 1 : 0);
    }
    *o = 0;
    for (int c = 0; c < NCHANGES; c++)
        if (seen[c] != 1 || turned[c] != CHANGES[c].ons) { free(out); return 0; }
    return out;
}
