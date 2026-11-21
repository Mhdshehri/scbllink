/* pec.c - the Balaclava PEC.ini (see pec.h). By Mhdshehri. */
#include "pec.h"

static const char *const SECTIONS[] = {
    "[Engine.PECInventoryManager]", "[Echelon.EPECInventoryManager]", "[Echelon.EPECInventory]", 0 };

/* the heads, and how many lines the game has for each: Sam in each suit, and Briggs (the
   co-op partner; his cutscene heads stay) */
static const struct { const char *from, *to; int count; bool briggs; } SWAP[] = {
    { "m_MeshName=\"CHARM-CCS-Sam.Sam_head\"",               "m_MeshName=\"CHARM-CCS-Balaclava.Balaclava_head\"",               12, false },
    { "m_MeshName=\"CHARM-CCS-Sam.Sam_head_dirt\"",          "m_MeshName=\"CHARM-CCS-Balaclava.Balaclava_head_dirt\"",          1,  false },
    { "m_MeshName=\"CHARM-CCS-Sam.Sam_head_dirt_nohair\"",   "m_MeshName=\"CHARM-CCS-Balaclava.Balaclava_head_dirt_nohair\"",   1,  false },
    { "ePresetType_COOPBriggs,m_MeshName=\"CHARM-CCS-Briggs.Briggs_head\"",
      "ePresetType_COOPBriggs,m_MeshName=\"CHARM-CCS-Balaclava.Balaclava_head\"",                                                    1,  true  },
};
#define NSWAP ((int)(sizeof SWAP / sizeof SWAP[0]))

static int count(const char *s, const char *w)
{
    int n = 0;
    size_t l = strlen(w);
    for (const char *p = strstr(s, w); p; p = strstr(p + l, w)) n++;
    return n;
}

/* the start of the line holding w, or NULL; w must start a line */
static const char *line_at(const char *s, const char *w)
{
    size_t l = strlen(w);
    for (const char *p = strstr(s, w); p; p = strstr(p + l, w))
        if (p == s || p[-1] == '\n') return p;
    return 0;
}

bool pec_ours(const char *text) { return text && strstr(text, PEC_MARK) != 0; }

char *pec_balaclava(const char *orig, bool sam, bool briggs)
{
    if (!orig || (!sam && !briggs)) return 0;
    if (!strncmp(orig, "\xEF\xBB\xBF", 3)) orig += 3;
    if (pec_ours(orig) || strncmp(orig, SECTIONS[0], strlen(SECTIONS[0]))) return 0;
    /* the sections: each once, in order, each with lines; every line "key=value" with a named key */
    const char *at[3];
    for (int i = 0; i < 3; i++) {
        at[i] = line_at(orig, SECTIONS[i]);
        if (!at[i] || count(orig, SECTIONS[i]) != 1 || (i && at[i] <= at[i - 1])) return 0;
    }
    if (count(orig, "\n[") != 2) return 0;
    int lines[3] = { 0 };
    for (const char *p = orig; *p;) {
        const char *e = strchr(p, '\n');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        if (l && p[l - 1] == '\r') l--;
        if (l && p[0] != '[') {
            const char *eq = 0;
            for (size_t k = 0; k < l && !eq; k++) if (p[k] == '=') eq = p + k;
            if (!eq || eq == p || !strncmp(p, "KEY_", 4) || p[0] == '?') return 0;
            for (const char *k = p; k < eq; k++)
                if (!((*k >= 'a' && *k <= 'z') || (*k >= 'A' && *k <= 'Z') || (*k >= '0' && *k <= '9') || *k == '_' || *k == '[' || *k == ']')) return 0;
            int s = p > at[2] ? 2 : p > at[1] ? 1 : 0;
            lines[s]++;
        }
        if (!e) break;
        p = e + 1;
    }
    /* the game's config has exactly these many lines (its last version): any other count means
       it was cut short, or entries from elsewhere came along (seen: 3 suit lines after the gadgets) */
    if (lines[0] != 9 || lines[1] != 1436 || lines[2] != 28) return 0;
    /* the head lines as the game has them, and its own one balaclava line (ePresetType_Balaclava) */
    for (int i = 0; i < NSWAP; i++)
        if (count(orig, SWAP[i].from) != SWAP[i].count) return 0;
    if (count(orig, "CHARM-CCS-Balaclava.Balaclava_head\"") != 1) return 0;

    size_t n = strlen(orig);
    char *out = malloc(3 + n + strlen(PEC_MARK) + 2 + 64 * 14 + 1);
    if (!out) return 0;
    memcpy(out, "\xEF\xBB\xBF", 3);
    char *o = out + 3;
    const char *mark_after = at[1] + strlen(SECTIONS[1]);
    for (const char *p = orig; *p;) {
        int hit = -1;
        for (int i = 0; i < NSWAP && hit < 0; i++)
            if ((SWAP[i].briggs ? briggs : sam) && !strncmp(p, SWAP[i].from, strlen(SWAP[i].from))) hit = i;
        if (hit >= 0) {
            size_t tl = strlen(SWAP[hit].to);
            memcpy(o, SWAP[hit].to, tl);
            o += tl;
            p += strlen(SWAP[hit].from);
            continue;
        }
        *o++ = *p++;
        if (p == mark_after) {                 /* our mark goes under the suit section's name */
            if (*p == '\r') *o++ = *p++;
            if (*p == '\n') *o++ = *p++;
            size_t ml = strlen(PEC_MARK);
            memcpy(o, PEC_MARK, ml);
            o += ml;
            *o++ = '\r';
            *o++ = '\n';
        }
    }
    *o = 0;
    for (int i = 0; i < NSWAP; i++)
        if (count(out, SWAP[i].from) != ((SWAP[i].briggs ? briggs : sam) ? 0 : SWAP[i].count)) { free(out); return 0; }
    return out;
}
