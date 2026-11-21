/* pec.c - saves the game's own suit config (PEC) on this PC, once, for SCBLLink's Balaclava
 * choice (src/pec.c makes the PEC.ini from it). Nothing is sent anywhere.
 *
 * The game keeps that config only in its packed files, and the text only for a moment while it
 * starts. What stays in memory is the parsed table, and that is what is read here:
 *  - a section is an array of 20-byte entries, in the file's order:
 *        { uint32 key (name number), char *value, int32 len (with the 0), int32 cap, uint32 next }
 *  - key names: a table of pointers to name entries (a 12-byte header, then the text); [0] is "None".
 * Each section is found from a value only it has; the names from the name "m_arSuitMaterial".
 * All reads go through ReadProcessMemory, so memory freed meanwhile can't crash the game.
 * It runs only when the file isn't saved yet and the game has no PEC.ini of its own (then the
 * config in memory is the game's own), not in the developer's second game, and gives up after 5 min.
 * By Mhdshehri. */
#include "w32.h"
#include "link.h"

typedef struct { void *base, *alloc_base; DWORD alloc_prot; size_t size; DWORD state, prot, type; } MEMINFO;
IMP size_t WINAPI VirtualQuery(const void *p, MEMINFO *mi, size_t n);
IMP BOOL WINAPI ReadProcessMemory(HANDLE proc, const void *at, void *buf, size_t n, size_t *got);

#define CHUNK (256 * 1024)
#define MAX_PAIRS 20000
#define MAX_KEYS 64
#define OUT_CAP (2 * 1024 * 1024)

typedef struct { uint32_t key, ptr; int32_t num, max; uint32_t next; } Pair;
typedef struct { const char *name, *anchor; uint32_t at; int count; } Sec;

/* each section, and a value only it has */
static Sec SEC[3] = {
    { "[Engine.PECInventoryManager]", "(m_iHeadID=255,m_iTorsoID=255,m_iLegsID=255", 0, 0 },
    { "[Echelon.EPECInventoryManager]", "(m_PresetType=ePresetType_SPSuitA, m_eCamoColor=eCamoColor_Black, m_Material=", 0, 0 },
    { "[Echelon.EPECInventory]",      "(m_eName=GUT_Upgrade1,m_eLinkedGadget=GDT_FragGrenade", 0, 0 },
};

static uint8_t *g_buf;     /* CHUNK: a copy of memory being searched */
static char *g_val;        /* 64 KB: one value */
static char *g_out;        /* the file being made */
static const uint8_t *g_mem_lo, *g_mem_hi, *g_img_lo, *g_img_hi;
static char g_path[600];

static bool readable(const MEMINFO *m)
{
    if (m->state != 0x1000 || (m->prot & 0x101)) return false;   /* committed, not NOACCESS/GUARD */
    return (m->prot & 0xEE) != 0;
}

static bool copy(const void *at, void *out, size_t n)
{
    size_t got = 0;
    return ReadProcessMemory(GetCurrentProcess(), at, out, n, &got) && got == n;
}

/* our own buffers and our own strings don't count */
static bool ours(const uint8_t *p) { return (p >= g_mem_lo && p < g_mem_hi) || (p >= g_img_lo && p < g_img_hi); }

static bool rd(uint32_t at, void *out, size_t n) { return at >= 0x10000 && copy((const void *)(uintptr_t)at, out, n); }

static bool rdstr(uint32_t at, char *out, size_t cap, size_t *len)
{
    size_t n = 0;
    while (n + 1 < cap) {
        size_t piece = 4096 - ((at + n) & 4095);
        if (piece > cap - 1 - n) piece = cap - 1 - n;
        if (!rd(at + (uint32_t)n, out + n, piece)) return false;
        for (size_t i = 0; i < piece; i++)
            if (!out[n + i]) { if (len) *len = n + i; return true; }
        n += piece;
    }
    return false;
}

static bool pair_ok(uint32_t at, Pair *p)
{
    size_t len;
    if (!rd(at, p, sizeof *p) || !p->key || p->key >= 0x200000) return false;  /* name 0 is "None" */
    if (!p->ptr && !p->num) { g_val[0] = 0; return true; }                     /* an empty value */
    if (p->num <= 0 || p->num > p->max || p->max > 0x10000) return false;
    return rdstr(p->ptr, g_val, 65536, &len) && (int32_t)len == p->num - 1;
}

static int walk_fwd(uint32_t at)
{
    Pair p;
    int n = 0;
    while (n < MAX_PAIRS && pair_ok(at + 20u * (uint32_t)n, &p)) n++;
    return n;
}

/* where text starts (prefix: no ending 0 needed; whole: the ending 0 too) */
static int find_text(const char *s, bool whole, uint32_t *out, int max)
{
    size_t ml = strlen(s) + (whole ? 1 : 0);
    int found = 0;
    MEMINFO m;
    for (const uint8_t *p = (const uint8_t *)0x10000; found < max && VirtualQuery(p, &m, sizeof m) == sizeof m; p = (const uint8_t *)m.base + m.size) {
        if ((const uint8_t *)m.base + m.size <= p) break;
        if (!readable(&m) || m.size > 512u * 1024 * 1024) continue;
        const uint8_t *lo = m.base, *hi = lo + m.size;
        for (const uint8_t *c = lo; c < hi && found < max; c += CHUNK - 128) {
            size_t n = (size_t)(hi - c) < CHUNK ? (size_t)(hi - c) : CHUNK;
            if (!copy(c, g_buf, n)) continue;
            for (size_t i = 0; i + ml <= n && found < max; i++) {
                if (g_buf[i] != (uint8_t)s[0] || memcmp(g_buf + i, s, ml) || ours(c + i)) continue;
                uint32_t a = (uint32_t)(uintptr_t)(c + i);
                bool dup = false;
                for (int k = 0; k < found; k++) if (out[k] == a) dup = true;
                if (!dup) out[found++] = a;
            }
            if (n < CHUNK) break;
        }
    }
    return found;
}

/* aligned dwords equal to one of tg[]; not in our buffers, image or this thread's stack */
static int find_refs(const uint32_t *tg, int nt, uint32_t *ref_at, int *ref_t, int max)
{
    MEMINFO own = { 0 }, m;
    int here = 0, found = 0;
    VirtualQuery(&here, &own, sizeof own);
    for (const uint8_t *p = (const uint8_t *)0x10000; found < max && VirtualQuery(p, &m, sizeof m) == sizeof m; p = (const uint8_t *)m.base + m.size) {
        if ((const uint8_t *)m.base + m.size <= p) break;
        if (!readable(&m) || m.size > 512u * 1024 * 1024 || m.alloc_base == own.alloc_base) continue;
        const uint8_t *lo = m.base, *hi = lo + m.size;
        for (const uint8_t *c = lo; c < hi && found < max; c += CHUNK) {
            size_t n = (size_t)(hi - c) < CHUNK ? (size_t)(hi - c) : CHUNK;
            if (!copy(c, g_buf, n)) continue;
            const uint32_t *d = (const uint32_t *)g_buf;
            for (size_t i = 0; i < n / 4 && found < max; i++)
                for (int t = 0; t < nt; t++)
                    if (d[i] == tg[t] && !ours(c + 4 * i)) {
                        ref_at[found] = (uint32_t)(uintptr_t)(c + 4 * i);
                        ref_t[found] = t;
                        found++;
                        break;
                    }
            if (n < CHUNK) break;
        }
    }
    return found;
}

static size_t g_olen;
static bool out_put(const char *s, size_t n)
{
    if (g_olen + n + 1 > OUT_CAP) return false;
    memcpy(g_out + g_olen, s, n);
    g_olen += n;
    g_out[g_olen] = 0;
    return true;
}

/* one try: true when the whole config was read and written */
static bool try_once(void)
{
    uint32_t tg[32], ref_at[64];
    int ref_t[64];
    /* 1: each section, walked out from its anchor value */
    for (int s = 0; s < 3; s++) {
        SEC[s].at = 0;
        SEC[s].count = 0;
        int nt = find_text(SEC[s].anchor, false, tg, 8);
        int nr = nt ? find_refs(tg, nt, ref_at, ref_t, 16) : 0;
        for (int r = 0; r < nr && !SEC[s].at; r++) {
            Pair p;
            uint32_t e = ref_at[r] - 4;
            if (!pair_ok(e, &p)) continue;
            int back = 0;
            while (back < MAX_PAIRS && pair_ok(e - 20u * (uint32_t)(back + 1), &p)) back++;
            SEC[s].at = e - 20u * (uint32_t)back;
            SEC[s].count = walk_fwd(SEC[s].at);
        }
        if (!SEC[s].at || SEC[s].count < 1) { sl_log("balaclava: the game's suit config isn't read yet (%s)", SEC[s].name); return false; }
        /* How many entries it really has: the array's own { data, num, max } says. Old entries can
           sit after the last one and look valid (seen: 3 suit lines after the gadget section). */
        uint32_t start = SEC[s].at, hdr[64];
        int walked = SEC[s].count, nh = find_refs(&start, 1, hdr, ref_t, 16), num = 0;
        for (int h = 0; h < nh && !num; h++) {
            int32_t nm[2];
            if (rd(hdr[h] + 4, nm, sizeof nm) && nm[0] >= 1 && nm[0] <= walked && nm[1] >= nm[0] && nm[1] <= walked + 4096) num = nm[0];
        }
        if (num) SEC[s].count = num;
        else sl_log("balaclava: the size of %s wasn't found; SCBLLink checks the line count", SEC[s].name);
    }
    /* 2: the names of the keys */
    uint32_t keys[MAX_KEYS];
    int nk = 0;
    for (int s = 0; s < 3; s++)
        for (int i = 0; i < SEC[s].count; i++) {
            Pair p;
            if (!rd(SEC[s].at + 20u * (uint32_t)i, &p, sizeof p)) return false;
            bool have = false;
            for (int k = 0; k < nk; k++) if (keys[k] == p.key) have = true;
            if (have) continue;
            if (nk == MAX_KEYS) return false;
            keys[nk++] = p.key;
        }
    uint32_t xs[8], table = 0, off = 0;
    int nx = find_text("m_arSuitMaterial", true, xs, 8), nt = 0;
    for (int i = 0; i < nx; i++)
        for (uint32_t k = 0; k <= 12; k += 4) tg[nt++] = xs[i] - k;
    int nr = nt ? find_refs(tg, nt, ref_at, ref_t, 64) : 0;
    for (int r = 0; r < nr && !table; r++) {
        uint32_t k_off = (uint32_t)(ref_t[r] % 4) * 4;      /* how far before the text the pointer points */
        for (int k = 0; k < nk && !table; k++) {
            uint32_t base = ref_at[r] - 4u * keys[k], e0;
            char none[8];
            size_t len;
            if (rd(base, &e0, 4) && rdstr(e0 + k_off, none, sizeof none, &len) && !strcmp(none, "None")) { table = base; off = k_off; }
        }
    }
    if (!table) { sl_log("balaclava: the game's names weren't found"); return false; }
    static char names[MAX_KEYS][64];
    for (int k = 0; k < nk; k++) {
        uint32_t e;
        size_t len;
        if (!rd(table + 4u * keys[k], &e, 4) || !rdstr(e + off, names[k], sizeof names[k], &len) || !len) {
            sl_log("balaclava: a key without its name");
            return false;
        }
    }
    /* 3: the file, as the game's own text would be */
    g_olen = 0;
    g_out[0] = 0;
    for (int s = 0; s < 3; s++) {
        char head[100];
        sfmt(head, sizeof head, "%s%s\r\n", s ? "\r\n" : "", SEC[s].name);   /* the names have their brackets */
        if (!out_put(head, strlen(head))) return false;
        for (int i = 0; i < SEC[s].count; i++) {
            Pair p;
            if (!pair_ok(SEC[s].at + 20u * (uint32_t)i, &p)) return false;   /* changed meanwhile: try later */
            const char *nm = 0;
            for (int k = 0; k < nk; k++) if (keys[k] == p.key) nm = names[k];
            if (!nm || !out_put(nm, strlen(nm)) || !out_put("=", 1) || !out_put(g_val, strlen(g_val)) || !out_put("\r\n", 2)) return false;
        }
    }
    /* written whole, then put in place: a cut-short file is never there */
    char tmp[620];
    sfmt(tmp, sizeof tmp, "%s.tmp", g_path);
    HANDLE f = CreateFileA(tmp, 0x40000000, 0, 0, 2, 0x80, 0);
    if (f == (HANDLE)-1) return false;
    DWORD w = 0;
    bool ok = WriteFile(f, "\xEF\xBB\xBF", 3, &w, 0) && w == 3 && WriteFile(f, g_out, (DWORD)g_olen, &w, 0) && w == g_olen;
    CloseHandle(f);
    if (!ok || rename(tmp, g_path) != 0) { remove(tmp); return false; }
    sl_log("balaclava: saved the game's own suit config (%d + %d + %d lines) for SCBLLink", SEC[0].count, SEC[1].count, SEC[2].count);
    return true;
}

static DWORD WINAPI pec_thread(void *arg)
{
    DWORD t0 = GetTickCount();
    Sleep(20000);
    while (GetTickCount() - t0 < 300000) {
        if (try_once()) return 0;
        Sleep(15000);
    }
    sl_log("balaclava: gave up for now (the game's suit config wasn't found); the next start tries again");
    return 0;
}

void pec_start(void)
{
    if (sl_second()) return;
    const char *app = getenv("APPDATA");
    if (!app) return;
    sfmt(g_path, sizeof g_path, "%s\\SCBLLink\\PEC-original.ini", app);
    if (GetFileAttributesA(g_path) != 0xFFFFFFFFUL) return;            /* already saved */
    char exe[520], loose[560];
    DWORD n = GetModuleFileNameA(0, exe, sizeof exe);
    if (!n || n >= sizeof exe) return;
    char *s = 0;
    for (char *q = exe; *q; q++) if (*q == '\\') s = q;
    if (!s) return;
    *s = 0;
    sfmt(loose, sizeof loose, "%s\\PEC.ini", exe);
    if (GetFileAttributesA(loose) != 0xFFFFFFFFUL) return;             /* not the game's own config in memory */
    size_t total = CHUNK + 65536 + OUT_CAP;
    uint8_t *mem = malloc(total);
    if (!mem) return;
    g_buf = mem;
    g_val = (char *)mem + CHUNK;
    g_out = (char *)mem + CHUNK + 65536;
    g_mem_lo = mem;
    g_mem_hi = mem + total;
    HMODULE self = GetModuleHandleA("uplay_r1_loader.dll");
    if (self) {
        uint8_t *b = (uint8_t *)self;
        uint32_t pe = *(uint32_t *)(b + 0x3c), size = *(uint32_t *)(b + pe + 24 + 56); /* SizeOfImage */
        g_img_lo = b;
        g_img_hi = b + size;
    }
    CreateThread(0, 0, pec_thread, 0, 0, 0);
}
