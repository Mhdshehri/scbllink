/* overlay.c - SCBLLink's overlay inside the game (DirectX 11, the Steam
 * version's Blacklist_DX11_game.exe). By Mhdshehri.
 *
 * What it shows (drawn for 1080p, scaled to the screen):
 *   - a notice when someone invites you: "KaZ invited you - F5 to join";
 *   - F5 (no invitation waiting): a panel with the players and their state,
 *     F6 there has the game read the friend list again (Refresh);
 *   - a short notice when the game starts: "F5 opens SCBLLink".
 *
 * How: the picture is drawn with GDI into memory and written straight into the
 * frame the game is about to show (UpdateSubresource on the back buffer, from
 * IDXGISwapChain::Present). No shaders, no change to the game's own drawing
 * state. Present is reached through the swap chain's function table, which
 * every swap chain made by this DXGI shares. Keys: read with GetAsyncKeyState
 * while the game's window is in front - the game's own input is left alone. */
#include "link.h"

#define IMPU __declspec(dllimport)
typedef void *HWND;
typedef void *HDC;
typedef void *HGDI;
typedef struct { long l, t, r, b; } RECT_;

IMPU HWND WINAPI CreateWindowExA(DWORD ex, const char *cls, const char *name, DWORD style, int x, int y, int w, int h, HWND parent, void *menu, HMODULE inst, void *p);
IMPU BOOL WINAPI DestroyWindow(HWND w);
IMPU short WINAPI GetAsyncKeyState(int vk);
IMPU HWND WINAPI GetForegroundWindow(void);
IMPU DWORD WINAPI GetWindowThreadProcessId(HWND w, DWORD *pid);
IMPU int WINAPI FillRect(HDC dc, const RECT_ *r, HGDI brush);
IMPU HDC WINAPI CreateCompatibleDC(HDC dc);
IMPU HGDI WINAPI CreateDIBSection(HDC dc, const void *bmi, unsigned usage, void **bits, HANDLE section, DWORD offset);
IMPU HGDI WINAPI SelectObject(HDC dc, HGDI o);
IMPU BOOL WINAPI DeleteObject(HGDI o);
IMPU BOOL WINAPI DeleteDC(HDC dc);
IMPU HGDI WINAPI CreateSolidBrush(DWORD color);
IMPU HGDI WINAPI CreateFontA(int h, int w, int esc, int orient, int weight, DWORD italic, DWORD under, DWORD strike, DWORD charset, DWORD out, DWORD clip, DWORD quality, DWORD pitch, const char *face);
IMPU DWORD WINAPI SetTextColor(HDC dc, DWORD c);
IMPU int WINAPI SetBkMode(HDC dc, int mode);
IMPU BOOL WINAPI TextOutA(HDC dc, int x, int y, const char *s, int n);
IMPU BOOL WINAPI GdiFlush(void);

#define RGBC(r, g, b) ((DWORD)((r) | ((g) << 8) | ((b) << 16)))
#define VK_F5 0x74
#define VK_F6 0x75

typedef long HRESULT;
typedef struct { uint32_t a; uint16_t b, c; uint8_t d[8]; } GUID_;
static const GUID_ IID_Texture2D = { 0x6f15aaf2, 0xd208, 0x4e89, { 0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c } };
static const GUID_ IID_Device11 = { 0xdb6f6ddb, 0xac77, 0x4e88, { 0x82, 0x53, 0x81, 0x9d, 0xf9, 0xbb, 0xf1, 0x40 } };

/* COM by hand: obj -> table of functions; slot numbers from the DirectX headers */
#define VT(o) (*(void ***)(o))
typedef unsigned long(WINAPI *Release_fn)(void *);
typedef HRESULT(WINAPI *Present_fn)(void *, unsigned, unsigned);
typedef HRESULT(WINAPI *GetBuffer_fn)(void *, unsigned, const GUID_ *, void **);
typedef HRESULT(WINAPI *GetDevice_fn)(void *, const GUID_ *, void **);
typedef void(WINAPI *GetImmediateContext_fn)(void *, void **);
typedef void(WINAPI *GetDesc2D_fn)(void *, uint32_t *);
typedef struct { uint32_t left, top, front, right, bottom, back; } Box;
typedef void(WINAPI *UpdateSubresource_fn)(void *, void *, unsigned, const Box *, const void *, unsigned, unsigned);
enum { SC_GETDEVICE = 7, SC_PRESENT = 8, SC_GETBUFFER = 9, DEV_GETCONTEXT = 40, TEX_GETDESC = 10, CTX_UPDATESUBRESOURCE = 48 };

static Present_fn real_present;

static void release(void *o)
{
    if (o) ((Release_fn)VT(o)[2])(o);
}

static int g_steps = 2; /* the first frames: each step to the log, to see where it stops */
#define STEP(...) do { if (g_steps > 0) sl_log(__VA_ARGS__); } while (0)

/* ---------- what to show ---------- */

static char g_invite[SL_NAME];     /* waiting invitation ("" none) */
static uint32_t g_invite_at;
static bool g_panel;               /* F5 panel open */
static uint32_t g_hello_until;     /* the "F5 opens SCBLLink" notice */
static uint32_t g_note_until;
static char g_note[96];            /* short-lived line ("Refreshing...", "Joining KaZ...") */
static CRITICAL_SECTION g_cs;

void overlay_invite(const char *from)
{
    EnterCriticalSection(&g_cs);
    sfmt(g_invite, sizeof g_invite, "%s", from);
    g_invite_at = GetTickCount();
    LeaveCriticalSection(&g_cs);
}

/* a card from SCBLLink, like the invitation's: title, and what to do ("Restart Blacklist to see it") */
static char g_ntitle[64], g_nline[80];
static uint32_t g_notice_until;

void overlay_notice(const char *title, const char *line)
{
    EnterCriticalSection(&g_cs);
    sfmt(g_ntitle, sizeof g_ntitle, "%s", title);
    sfmt(g_nline, sizeof g_nline, "%s", line);
    g_notice_until = GetTickCount() + 30000;
    LeaveCriticalSection(&g_cs);
}

void overlay_invite_clear(void)
{
    EnterCriticalSection(&g_cs);
    g_invite[0] = 0;
    LeaveCriticalSection(&g_cs);
}

static void note(const char *s)
{
    sfmt(g_note, sizeof g_note, "%s", s);
    g_note_until = GetTickCount() + 4000;
}

/* keys: only while the game is in front, and once per press */
static bool our_window(void)
{
    DWORD pid = 0;
    HWND w = GetForegroundWindow();
    if (!w) return false;
    GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

static bool pressed(int vk, bool *was)
{
    bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool edge = down && !*was;
    *was = down;
    return edge;
}

void sl_accept(const char *name); /* link.c: queue the "invitation accepted" event, tell SCBLLink */
void sl_refresh(void);            /* link.c: queue "read the friend list again" */

static void keys(void)
{
    static bool f5, f6;
    bool front = our_window();
    bool k5 = pressed(VK_F5, &f5), k6 = pressed(VK_F6, &f6);
    if (!front) return;
    EnterCriticalSection(&g_cs);
    char inv[SL_NAME];
    sfmt(inv, sizeof inv, "%s", g_invite);
    if (inv[0] && GetTickCount() - g_invite_at > 120000) g_invite[0] = inv[0] = 0; /* 2 minutes: gone */
    if (k5 && inv[0]) g_invite[0] = 0;
    LeaveCriticalSection(&g_cs);
    if (k5 && inv[0]) {
        sl_accept(inv);
        char t[96];
        sfmt(t, sizeof t, "Joining %s...", inv);
        note(t);
        g_panel = false;
    } else if (k5) {
        g_panel = !g_panel;
    }
    if (k6 && g_panel) {
        sl_refresh();
        note("Friend list read again");
    }
}

/* ---------- drawing (GDI, into memory) ---------- */

typedef struct {
    uint32_t size; int32_t w, h;
    uint16_t planes, bits;
    uint32_t compression, image, xppm, yppm, used, important;
} BmiHeader;

static HDC g_dc;
static HGDI g_bmp, g_font, g_bold, g_small;
static uint32_t *g_px;
static int g_w, g_h, g_font_scale = -1;

static bool canvas(int w, int h, int scale_pct)
{
    if (g_dc && g_w == w && g_h == h && g_font_scale == scale_pct) return true;
    if (g_dc) {
        DeleteObject(g_bmp);
        DeleteDC(g_dc);
        g_dc = 0;
    }
    if (g_font) { DeleteObject(g_font); DeleteObject(g_bold); DeleteObject(g_small); }
    BmiHeader b = { sizeof b, w, -h, 1, 32, 0, 0, 0, 0, 0, 0 }; /* top-down */
    STEP("canvas %dx%d", w, h);
    g_dc = CreateCompatibleDC(0);
    if (!g_dc) return false;
    void *bits = 0;
    g_bmp = CreateDIBSection(g_dc, &b, 0, &bits, 0, 0);
    if (!g_bmp) { DeleteDC(g_dc); g_dc = 0; return false; }
    SelectObject(g_dc, g_bmp);
    g_px = bits;
    g_w = w;
    g_h = h;
    g_font_scale = scale_pct;
    int s = scale_pct;
    g_font = CreateFontA(-(20 * s / 100), 0, 0, 0, 400, 0, 0, 0, 1, 0, 0, 5 /* ClearType */, 0, "Segoe UI");
    g_bold = CreateFontA(-(22 * s / 100), 0, 0, 0, 700, 0, 0, 0, 1, 0, 0, 5, 0, "Segoe UI");
    g_small = CreateFontA(-(17 * s / 100), 0, 0, 0, 400, 0, 0, 0, 1, 0, 0, 5, 0, "Segoe UI");
    SetBkMode(g_dc, 1 /* transparent */);
    return true;
}

static void fill(int x, int y, int w, int h, DWORD c)
{
    RECT_ r = { x, y, x + w, y + h };
    HGDI b = CreateSolidBrush(c);
    FillRect(g_dc, &r, b);
    DeleteObject(b);
}

static void text(int x, int y, HGDI font, DWORD color, const char *s)
{
    SelectObject(g_dc, font);
    SetTextColor(g_dc, color);
    TextOutA(g_dc, x, y, s, (int)strlen(s));
}

/* the colours of SCBLLink's window (dark) */
#define C_BG RGBC(24, 26, 30)
#define C_EDGE RGBC(60, 64, 72)
#define C_ACCENT RGBC(34, 160, 84)
#define C_TEXT RGBC(232, 234, 238)
#define C_DIM RGBC(150, 155, 165)
#define C_WARN RGBC(214, 92, 92)

/* Draws what's to be shown; returns its size (0 = nothing to show). */
static bool compose(int scale, int *out_w, int *out_h)
{
    uint32_t now = GetTickCount();
    char inv[SL_NAME], nt[64], nl[80];
    EnterCriticalSection(&g_cs);
    sfmt(inv, sizeof inv, "%s", g_invite);
    bool notice = (int32_t)(g_notice_until - now) > 0;
    sfmt(nt, sizeof nt, "%s", g_ntitle);
    sfmt(nl, sizeof nl, "%s", g_nline);
    LeaveCriticalSection(&g_cs);
    bool hello = (int32_t)(g_hello_until - now) > 0;
    bool noted = (int32_t)(g_note_until - now) > 0;
    int S = scale; /* percent */
#define P(v) ((v) * S / 100)
    if (g_panel) {
        static SlFriend f[SL_MAX_FRIENDS];
        int n = sl_friends(f, SL_MAX_FRIENDS, 0);
        int rows = n ? n : 1;
        int w = P(420), h = P(96 + rows * 30 + 60);
        if (!canvas(w, h, S)) return false;
        fill(0, 0, w, h, C_EDGE);
        fill(1, 1, w - 2, h - 2, C_BG);
        fill(1, 1, w - 2, P(4), C_ACCENT);
        text(P(18), P(16), g_bold, C_TEXT, "SCBLLink");
        text(P(18), P(46), g_small, C_DIM, "Players linked with you");
        int y = P(76);
        if (!n) { text(P(18), y, g_font, C_DIM, "Nobody yet"); y += P(30); }
        for (int i = 0; i < n; i++) {
            fill(P(18), y + P(8), P(10), P(10), f[i].online ? C_ACCENT : C_EDGE);
            text(P(38), y, g_font, C_TEXT, f[i].name);
            const char *st = !f[i].online ? "not in the game" : f[i].session ? "in a game" : "in the game";
            text(P(250), y + P(2), g_small, C_DIM, st);
            y += P(30);
        }
        y += P(12);
        fill(P(18), y, w - P(36), 1, C_EDGE);
        y += P(12);
        text(P(18), y, g_small, noted ? C_ACCENT : C_DIM, noted ? g_note : "F6  Refresh friends        F5  Close");
        *out_w = w;
        *out_h = h;
        return true;
    }
    if (inv[0]) {
        int w = P(420), h = P(84);
        if (!canvas(w, h, S)) return false;
        fill(0, 0, w, h, C_ACCENT);
        fill(P(4), 1, w - P(4) - 1, h - 2, C_BG);
        char t[96];
        sfmt(t, sizeof t, "%s invited you", inv);
        text(P(22), P(14), g_bold, C_TEXT, t);
        text(P(22), P(46), g_font, C_ACCENT, "Press F5 to join");
        *out_w = w;
        *out_h = h;
        return true;
    }
    if (notice) {   /* the invitation's card, in red: something to do (an invitation comes first) */
        int w = P(420), h = P(84);
        if (!canvas(w, h, S)) return false;
        fill(0, 0, w, h, C_WARN);
        fill(P(4), 1, w - P(4) - 1, h - 2, C_BG);
        text(P(22), P(14), g_bold, C_TEXT, nt);
        text(P(22), P(46), g_font, C_WARN, nl);
        *out_w = w;
        *out_h = h;
        return true;
    }
    if (noted || hello) {
        int w = P(420), h = P(52);
        if (!canvas(w, h, S)) return false;
        fill(0, 0, w, h, C_EDGE);
        fill(1, 1, w - 2, h - 2, C_BG);
        text(P(18), P(13), g_font, noted ? C_ACCENT : C_TEXT, noted ? g_note : (sl_linked() ? "SCBLLink: F5 opens the panel" : "SCBLLink isn't running: online play is off"));
        *out_w = w;
        *out_h = h;
        return true;
    }
#undef P
    return false;
}

/* GDI writes B,G,R,0. The frame wants RGBA or BGRA: convert into a row buffer. */
static void upload(void *ctx, void *bb, uint32_t format, int x, int y, int w, int h)
{
    static uint32_t *rows;
    static int cap;
    if (cap < w * h) {
        free(rows);
        rows = malloc((size_t)w * (size_t)h * 4);
        cap = rows ? w * h : 0;
        if (!rows) return;
    }
    GdiFlush();
    bool rgba = format == 28 || format == 29; /* R8G8B8A8 (_SRGB) */
    for (int i = 0; i < w * h; i++) {
        uint32_t p = g_px[i]; /* 0x00RRGGBB in memory as B,G,R,0 */
        rows[i] = rgba ? (0xFF000000u | (p & 0xFF) << 16 | (p & 0xFF00) | (p >> 16 & 0xFF)) : (0xFF000000u | p);
    }
    Box box = { (uint32_t)x, (uint32_t)y, 0, (uint32_t)(x + w), (uint32_t)(y + h), 1 };
    ((UpdateSubresource_fn)VT(ctx)[CTX_UPDATESUBRESOURCE])(ctx, bb, 0, &box, rows, (unsigned)w * 4, 0);
}

static HRESULT WINAPI my_present_outer(void *sc, unsigned sync, unsigned flags);

/* DXGI's own Present calls Present again, through the same function table, on the
   object inside it: that inner call must go straight on, or it comes back here for
   ever (the stack runs out and the game crashes). One thread draws at a time. */
static volatile DWORD g_in_present; /* thread id inside, 0 = none */

static HRESULT WINAPI my_present(void *sc, unsigned sync, unsigned flags)
{
    DWORD me = GetCurrentThreadId();
    if (g_in_present == me) return real_present(sc, sync, flags);
    if (g_in_present) return real_present(sc, sync, flags); /* another thread is drawing: don't wait */
    g_in_present = me;
    HRESULT r = my_present_outer(sc, sync, flags);
    g_in_present = 0;
    return r;
}

static HRESULT WINAPI my_present_outer(void *sc, unsigned sync, unsigned flags)
{
    static bool told, told_format;
    if (!told) { sl_log("overlay: the game's frames go through SCBLLink's overlay"); told = true; }
    STEP("frame: keys");
    keys();
    int w = 0, h = 0;
    void *bb = 0, *dev = 0, *ctx = 0;
    HRESULT gb = flags & 1 /* DXGI_PRESENT_TEST */ ? 1 : ((GetBuffer_fn)VT(sc)[SC_GETBUFFER])(sc, 0, &IID_Texture2D, &bb);
    STEP("frame: flags %u, back buffer %ld %p", flags, gb, bb);
    if (gb >= 0 && bb) {
        uint32_t d[11]; /* D3D11_TEXTURE2D_DESC */
        STEP("frame: desc");
        ((GetDesc2D_fn)VT(bb)[TEX_GETDESC])(bb, d);
        uint32_t W = d[0], H = d[1], format = d[4], samples = d[5];
        STEP("frame: %ux%u format %u samples %u usage %u bind %x misc %x", W, H, format, samples, d[7], d[8], d[10]);
        bool ok_format = format == 28 || format == 29 || format == 87 || format == 88 || format == 91;
        if (!ok_format || samples != 1) {
            if (!told_format) { sl_log("! overlay: frame format %u (samples %u) not handled", format, samples); told_format = true; }
        } else {
            int scale = (int)(H * 100 / 1080);
            if (scale < 60) scale = 60;
            STEP("frame: compose");
            bool any = compose(scale, &w, &h);
            STEP("frame: composed %d (%dx%d)", any, w, h);
            if (any && w <= (int)W && h <= (int)H &&
                ((GetDevice_fn)VT(sc)[SC_GETDEVICE])(sc, &IID_Device11, &dev) >= 0 && dev) {
                STEP("frame: context");
                ((GetImmediateContext_fn)VT(dev)[DEV_GETCONTEXT])(dev, &ctx);
                if (ctx) {
                    int x = ((int)W - w) / 2, y = (int)H * 6 / 100; /* top middle */
                    STEP("frame: upload at %d,%d", x, y);
                    upload(ctx, bb, format, x, y, w, h);
                    STEP("frame: uploaded");
                }
            }
        }
    }
    release(ctx);
    release(dev);
    release(bb);
    STEP("frame: game's Present");
    HRESULT r = real_present(sc, sync, flags);
    STEP("frame: done (%ld)", r);
    if (g_steps > 0) g_steps--;
    return r;
}

/* ---------- getting to Present ---------- */

typedef struct {
    uint32_t w, h, rate_num, rate_den, format, scanline, scaling;
    uint32_t samples, quality;
    uint32_t usage, count;
    HWND window;
    BOOL windowed;
    uint32_t effect, flags;
} SwapDesc;
typedef HRESULT(WINAPI *CreateDevSwap_fn)(void *, int, HMODULE, unsigned, const void *, unsigned, unsigned, const SwapDesc *, void **, void **, void *, void **);


void *hotpatch(void *fn, void *mine);   /* hook.c */
void *chain_jump(void *fn, void *mine);

static bool in_module(void *p, const char *name)
{
    uint8_t *b = GetModuleHandleA(name);
    if (!b) return false;
    uint32_t pe = *(uint32_t *)(b + 0x3c), size = *(uint32_t *)(b + pe + 24 + 56);
    return (uint8_t *)p >= b && (uint8_t *)p < b + size;
}

/* The swap chain's Present, hooked at the function itself (like hudhook), not in its
   function table: Steam's overlay keeps putting its own hook in that table, and two
   hooks there end up calling each other for ever. */
static bool hook_dx11(void)
{
    HMODULE m = GetModuleHandleA("d3d11.dll");
    if (!m) m = LoadLibraryA("d3d11.dll");
    CreateDevSwap_fn create = m ? (CreateDevSwap_fn)GetProcAddress(m, "D3D11CreateDeviceAndSwapChain") : 0;
    if (!create) { sl_log("! overlay: no D3D11"); return false; }
    /* a throw-away window, device and swap chain, only to find Present */
    HWND w = CreateWindowExA(0, "STATIC", "scbllink-overlay", 0, 0, 0, 64, 64, 0, 0, 0, 0);
    if (!w) { sl_log("! overlay: no window"); return false; }
    SwapDesc d;
    memset(&d, 0, sizeof d);
    d.w = d.h = 64;
    d.rate_den = 1;
    d.format = 28;
    d.samples = 1;
    d.usage = 0x20; /* render target output */
    d.count = 1;
    d.window = w;
    d.windowed = 1;
    void *sc = 0, *dev = 0, *ctx = 0;
    unsigned level = 0;
    HRESULT r = create(0, 1 /* hardware */, 0, 0, 0, 0, 7 /* SDK */, &d, &sc, &dev, &level, &ctx);
    if (r < 0 || !sc) r = create(0, 5 /* WARP */, 0, 0, 0, 0, 7, &d, &sc, &dev, &level, &ctx);
    void *fn = r >= 0 && sc ? VT(sc)[SC_PRESENT] : 0;
    release(ctx);
    release(dev);
    release(sc);
    DestroyWindow(w);
    if (!fn) { sl_log("! overlay: no swap chain (%ld)", r); return false; }
    if (!in_module(fn, "dxgi.dll")) {
        sl_log("! overlay: Present isn't DXGI's own here (another overlay?): no SCBLLink overlay, use SCBLLink's window");
        return false;
    }
    uint8_t *f = fn;
    sl_log("overlay: Present starts %02x %02x %02x %02x %02x", f[0], f[1], f[2], f[3], f[4]);
    void *on = hotpatch(fn, (void *)my_present);
    const char *how = "hot-patch";
    if (!on) { on = chain_jump(fn, (void *)my_present); how = "in front of another hook"; }
    if (!on) { sl_log("! overlay: can't hook Present safely here: no SCBLLink overlay, use SCBLLink's window"); return false; }
    real_present = (Present_fn)on;
    sl_log("overlay: ready (DirectX 11, %s), F5", how);
    return true;
}

static DWORD WINAPI overlay_thread(void *arg)
{
    char off[600];
    sfmt(off, sizeof off, "%sscbllink_no_overlay.txt", sl_dir());
    if (GetFileAttributesA(off) != 0xFFFFFFFFUL) { sl_log("overlay: off (scbllink_no_overlay.txt is in the game folder)"); return 0; }
    /* the game loads its renderer a little after starting */
    char exe[300];
    GetModuleFileNameA(0, exe, sizeof exe);
    bool dx11 = strstr(exe, "DX11") || strstr(exe, "dx11");
    if (!dx11) { sl_log("overlay: this is the DirectX 9 game (%s): no overlay there yet; use SCBLLink's window", exe); return 0; }
    for (int i = 0; i < 600 && !GetModuleHandleA("d3d11.dll"); i++) Sleep(100);
    hook_dx11();
    return 0;
}

void overlay_start(void)
{
    static bool started;
    if (started) return;
    started = true;
    InitializeCriticalSection(&g_cs);
    g_hello_until = GetTickCount() + 15000;
    CreateThread(0, 0, overlay_thread, 0, 0, 0);
}
