/* cfgcap.c - saves the game's own camera config (BlacklistCamera.ini) on this PC, once, for
 * SCBLLink's Crosshair choice (src/camera.c makes the file from it). Nothing is sent anywhere.
 *
 * The game unpacks that file from a package we can't read, splits the text in place while it
 * reads it (0 bytes for '=', ']', ...), then frees the memory. So the game's own HeapFree and
 * VirtualFree imports come through here until the text is caught: a freed block holding it is
 * put back together (cfg_unsplit) and, when it is the whole file the game has, saved.
 * Only when it isn't saved yet and the game has no update\blacklistcamera.ini of its own (then
 * what it reads is its own), not in the developer's second game. By Mhdshehri. */
#include "w32.h"
#include "link.h"
#include "../src/camera.h"

typedef struct { void *base, *alloc_base; DWORD alloc_prot; size_t size; DWORD state, prot, type; } CapMem;
IMP size_t WINAPI VirtualQuery(const void *p, CapMem *mi, size_t n);
IMP size_t WINAPI HeapSize(HANDLE heap, DWORD flags, const void *p);
IMP long WINAPI InterlockedCompareExchange(volatile long *dst, long with, long cmp);
bool hook_import(const char *dll, const char *name, void **real, void *mine);

typedef BOOL (WINAPI *HeapFree_fn)(HANDLE, DWORD, void *);
typedef BOOL (WINAPI *VirtualFree_fn)(void *, size_t, DWORD);
static HeapFree_fn real_HeapFree;
static VirtualFree_fn real_VirtualFree;
static volatile long g_state;      /* 0 looking, 1 a block is being looked at, 2 done */
static char g_path[600];

static bool has(const uint8_t *p, size_t n, const char *w)
{
    size_t l = strlen(w);
    for (size_t i = 0; i + l <= n; i++)
        if (p[i] == (uint8_t)w[0] && !memcmp(p + i, w, l)) return true;
    return false;
}

static void look(const void *mem, size_t n)
{
    if (n < 65536 || n > (16u << 20)) return;
    if (InterlockedCompareExchange(&g_state, 1, 0) != 0) return;   /* one at a time, and not after */
    long next = 0;
    if (has(mem, n, "[Echelon.ECamera")) {
        char *text = cfg_unsplit(mem, n, "[Echelon.ECamera");
        char *check = text ? cam_crosshair_off(text) : 0;    /* the whole file the game has? */
        if (check) {
            char tmp[620];
            sfmt(tmp, sizeof tmp, "%s.tmp", g_path);
            HANDLE f = CreateFileA(tmp, 0x40000000, 0, 0, 2, 0x80, 0);
            DWORD w = 0;
            size_t l = strlen(text);
            bool ok = f != (HANDLE)-1 && WriteFile(f, text, (DWORD)l, &w, 0) && w == l;
            if (f != (HANDLE)-1) CloseHandle(f);
            if (ok && rename(tmp, g_path) == 0) {
                next = 2;
                sl_log("crosshair: saved the game's own camera config for SCBLLink (%u bytes)", (unsigned)l);
            } else remove(tmp);
        } else if (text) sl_log("crosshair: a camera config went by that isn't the whole one; still looking");
        free(check);
        free(text);
    }
    g_state = next;
}

static BOOL WINAPI cap_HeapFree(HANDLE heap, DWORD flags, void *p)
{
    if (p && g_state == 0) {
        size_t n = HeapSize(heap, 0, p);
        if (n != (size_t)-1) look(p, n);
    }
    return real_HeapFree(heap, flags, p);
}

static BOOL WINAPI cap_VirtualFree(void *p, size_t size, DWORD type)
{
    if (p && g_state == 0 && (type & 0x8000)) {   /* MEM_RELEASE: the whole region */
        CapMem m;
        if (VirtualQuery(p, &m, sizeof m) == sizeof m && m.state == 0x1000 && (m.prot & 0xEE) && !(m.prot & 0x101))
            look(m.base, m.size);
    }
    return real_VirtualFree(p, size, type);
}

void cfgcap_start(void)
{
    if (sl_second()) return;
    const char *app = getenv("APPDATA");
    if (!app) return;
    char dir[560];
    sfmt(dir, sizeof dir, "%s\\SCBLLink", app);
    sfmt(g_path, sizeof g_path, "%s\\" CAM_ORIG_FILE, dir);
    if (GetFileAttributesA(g_path) != 0xFFFFFFFFUL) return;           /* already saved */
    char exe[520], loose[600];
    DWORD n = GetModuleFileNameA(0, exe, sizeof exe);
    if (!n || n >= sizeof exe) return;
    char *s = 0;
    for (char *q = exe; *q; q++) if (*q == '\\') s = q;
    if (!s) return;
    *s = 0;
    sfmt(loose, sizeof loose, "%s\\" CAM_FILE, exe);
    if (GetFileAttributesA(loose) != 0xFFFFFFFFUL) return;            /* the game reads that one, not its own */
    CreateDirectoryA(dir, 0);
    bool a = hook_import("kernel32.dll", "HeapFree", (void **)&real_HeapFree, (void *)cap_HeapFree);
    bool b = hook_import("kernel32.dll", "VirtualFree", (void **)&real_VirtualFree, (void *)cap_VirtualFree);
    sl_log("crosshair: watching for the game's camera config (%s%s)", a ? "HeapFree" : "", b ? " VirtualFree" : "");
}
