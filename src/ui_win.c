/* ui_win.c - the SCBLLink window (Windows): a sidebar (the logo, Play, Settings,
 * Log; How to and the theme at the bottom) and the page picked in it.
 *  - Play: the SCBL Fix card, Host | Join and the button that starts it, the
 *    players card (the list, the lobby code, your name), the status and
 *    "Start Blacklist".
 *  - Settings: the game settings (Fusion Fix's and SCBLLink's own), "Your game"
 *    and "Co-op" in cards.
 *  - Log and How to: a title and one card.
 * The colors come from SCBLLink's logo and the game's cover: smoky grey, a
 * charcoal sidebar, night-vision green. The engine (scbllink.c) runs on a second
 * thread; this file only shows its state and sends it commands.
 * Built from SCCTLink's window. Win32 is declared by hand (no Windows SDK).
 * By Mhdshehri. */
#include "ui.h"
#include "plat.h"
#include "lobbycode.h"
#include "account.h"
#include "fusion.h"
#include "sys.h"

#define WINAPI __attribute__((stdcall))
#define IMP __declspec(dllimport)
typedef void *HANDLE, *HWND, *HINSTANCE, *HICON, *HCURSOR, *HBRUSH, *HFONT, *HDC, *HMENU;
typedef int BOOL;
typedef unsigned int UINT;
typedef unsigned long DWORD;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM, LRESULT;
typedef uint16_t WCHAR;
typedef LRESULT(WINAPI *WNDPROC)(HWND, UINT, WPARAM, LPARAM);

typedef struct {
    UINT cbSize, style;
    WNDPROC proc;
    int clsExtra, wndExtra;
    HINSTANCE inst;
    HICON icon;
    HCURSOR cursor;
    HBRUSH background;
    const WCHAR *menu, *className;
    HICON iconSm;
} WNDCLASSEXW;
typedef struct { long left, top, right, bottom; } RECT;
typedef struct { HWND hwnd; UINT msg; WPARAM wp; LPARAM lp; DWORD time; long x, y; DWORD priv; } MSG;
typedef struct { void *a, *b; long c, d; void *e, *f; } CRITICAL_SECTION; /* 24 bytes on x86 */

IMP uint16_t WINAPI RegisterClassExW(const WNDCLASSEXW *c);
IMP HWND WINAPI CreateWindowExW(DWORD ex, const WCHAR *cls, const WCHAR *title, DWORD style, int x, int y,
                                int w, int h, HWND parent, HMENU id, HINSTANCE inst, void *param);
IMP LRESULT WINAPI DefWindowProcW(HWND, UINT, WPARAM, LPARAM);
IMP BOOL WINAPI GetMessageW(MSG *m, HWND w, UINT a, UINT b);
IMP BOOL WINAPI TranslateMessage(const MSG *m);
IMP LRESULT WINAPI DispatchMessageW(const MSG *m);
IMP BOOL WINAPI IsDialogMessageW(HWND w, MSG *m);
IMP void WINAPI PostQuitMessage(int code);
IMP LRESULT WINAPI SendMessageW(HWND w, UINT m, WPARAM wp, LPARAM lp);
IMP BOOL WINAPI SetWindowTextW(HWND w, const WCHAR *t);
IMP int WINAPI GetWindowTextW(HWND w, WCHAR *buf, int n);
IMP uintptr_t WINAPI SetTimer(HWND w, uintptr_t id, UINT ms, void *fn);
IMP int WINAPI MessageBoxW(HWND w, const WCHAR *text, const WCHAR *cap, UINT type);
IMP HCURSOR WINAPI LoadCursorW(HINSTANCE i, const WCHAR *name);
IMP HICON WINAPI LoadIconW(HINSTANCE i, const WCHAR *name);
IMP BOOL WINAPI ShowWindow(HWND w, int cmd);
IMP BOOL WINAPI IsWindowVisible(HWND w);
IMP BOOL WINAPI UpdateWindow(HWND w);
IMP BOOL WINAPI EnableWindow(HWND w, BOOL on);
IMP BOOL WINAPI DestroyWindow(HWND w);
IMP HDC WINAPI GetDC(HWND w);
IMP int WINAPI ReleaseDC(HWND w, HDC dc);
IMP BOOL WINAPI AdjustWindowRectEx(RECT *r, DWORD style, BOOL menu, DWORD ex);
IMP int WINAPI GetSystemMetrics(int i);
IMP HFONT WINAPI CreateFontW(int h, int w, int esc, int orient, int weight, DWORD italic, DWORD underline,
                            DWORD strike, DWORD charset, DWORD outprec, DWORD clipprec, DWORD quality,
                            DWORD pitch, const WCHAR *face);
IMP int WINAPI GetDeviceCaps(HDC dc, int i);
IMP void WINAPI InitCommonControls(void);
IMP HANDLE WINAPI CreateThread(void *sec, size_t stack, DWORD(WINAPI *fn)(void *), void *arg, DWORD flags, DWORD *id);
IMP DWORD WINAPI WaitForSingleObject(HANDLE h, DWORD ms);
IMP void WINAPI InitializeCriticalSection(CRITICAL_SECTION *cs);
IMP void WINAPI EnterCriticalSection(CRITICAL_SECTION *cs);
IMP void WINAPI LeaveCriticalSection(CRITICAL_SECTION *cs);
IMP int WINAPI MultiByteToWideChar(UINT cp, DWORD flags, const char *s, int n, WCHAR *w, int wn);
IMP int WINAPI WideCharToMultiByte(UINT cp, DWORD flags, const WCHAR *w, int wn, char *s, int n, const char *d, BOOL *u);
IMP HINSTANCE WINAPI GetModuleHandleW(const WCHAR *name);
IMP HBRUSH WINAPI CreateSolidBrush(DWORD color);
IMP HANDLE WINAPI CreatePen(int style, int width, DWORD color);
IMP HANDLE WINAPI SelectObject(HDC dc, HANDLE obj);
IMP BOOL WINAPI DeleteObject(HANDLE obj);
IMP HANDLE WINAPI GetStockObject(int i);
IMP DWORD WINAPI SetTextColor(HDC dc, DWORD c);
IMP DWORD WINAPI SetBkColor(HDC dc, DWORD c);
IMP int WINAPI SetBkMode(HDC dc, int mode);
IMP BOOL WINAPI RoundRect(HDC dc, int l, int t, int r, int b, int w, int h);
IMP int WINAPI FillRect(HDC dc, const RECT *r, HBRUSH b);
IMP int WINAPI DrawTextW(HDC dc, const WCHAR *t, int n, RECT *r, UINT fmt);
IMP BOOL WINAPI GetClientRect(HWND w, RECT *r);
IMP BOOL WINAPI RedrawWindow(HWND w, const RECT *r, HANDLE rgn, UINT flags);
IMP BOOL WINAPI SetWindowPos(HWND w, HWND after, int x, int y, int cx, int cy, UINT flags);
IMP HWND WINAPI GetDlgItem(HWND w, int id);
IMP HWND WINAPI SetFocus(HWND w);
IMP BOOL WINAPI GetWindowRect(HWND w, RECT *r);
IMP BOOL WINAPI SetLayeredWindowAttributes(HWND w, DWORD key, unsigned char alpha, DWORD flags);
typedef struct { UINT size; HWND hwnd; DWORD flags; UINT count; DWORD timeout; } FLASHWINFO;
IMP BOOL WINAPI FlashWindowEx(const FLASHWINFO *f);
IMP HWND WINAPI GetForegroundWindow(void);
IMP BOOL WINAPI MessageBeep(UINT type);
IMP void *WINAPI LoadLibraryExA(const char *path, void *file, DWORD flags);
IMP void *WINAPI GetProcAddress(void *mod, const char *name);

#define WM_DESTROY 0x2
#define WM_CLOSE 0x10
#define WM_SETFONT 0x30
#define WM_GETTEXTLENGTH 0xE
#define WM_COMMAND 0x111
#define WM_TIMER 0x113
#define EM_SETSEL 0xB1
#define EM_SCROLLCARET 0xB7
#define EM_REPLACESEL 0xC2
#define EM_SETLIMITTEXT 0xC5
#define EM_SETREADONLY 0xCF
#define LB_ADDSTRING 0x180
#define LB_RESETCONTENT 0x184
#define LB_SETCURSEL 0x186
#define LB_GETCURSEL 0x188
#define LB_GETTEXT 0x189
#define LBN_DBLCLK 2
#define WM_ERASEBKGND 0x14
#define WM_DRAWITEM 0x2B
#define WM_MEASUREITEM 0x2C
#define WM_CTLCOLOREDIT 0x133
#define WM_CTLCOLORLISTBOX 0x134
#define WM_CTLCOLORSTATIC 0x138
#define BS_OWNERDRAW 0xBL
#define LBS_OWNERDRAWFIXED 0x10L
#define LBS_HASSTRINGS 0x40L
#define ODT_LISTBOX 2
#define ODS_SELECTED 0x1
#define ODS_DISABLED 0x4
#define ODS_FOCUS 0x10
#define DT_CENTER 0x1
#define DT_VCENTER 0x4
#define DT_SINGLELINE 0x20
#define DT_END_ELLIPSIS 0x8000
#define TRANSPARENT_BK 1
#define NULL_BRUSH 5
#define RGBC(r, g, b) ((DWORD)((r) | ((g) << 8) | ((b) << 16)))
#define WS_CHILD 0x40000000L
#define WS_VISIBLE 0x10000000L
#define WS_TABSTOP 0x10000L
#define WS_VSCROLL 0x200000L
#define WS_CAPTION 0xC00000L
#define WS_SYSMENU 0x80000L
#define WS_MINIMIZEBOX 0x20000L
#define ES_MULTILINE 0x4L
#define ES_AUTOVSCROLL 0x40L
#define ES_AUTOHSCROLL 0x80L
#define ES_READONLY 0x800L
#define LBS_NOTIFY 0x1L
#define LBS_NOINTEGRALHEIGHT 0x100L
#define CP_UTF8 65001
#define MB_ICONWARNING 0x30
#define MB_YESNO 0x4
#define MB_ICONQUESTION 0x20
#define IDYES 6
#define LOGPIXELSY 90
#define FW_NORMAL 400
#define FW_BOLD 700
#define CLEARTYPE_QUALITY 5

#define WTEXT(s) L##s /* only for ASCII literals: wchar_t is 16-bit on this target */
typedef struct { long x, y; } POINT;
IMP BOOL WINAPI GetCursorPos(POINT *p);
IMP BOOL WINAPI ScreenToClient(HWND w, POINT *p);
IMP HANDLE WINAPI LoadImageW(HINSTANCE i, const WCHAR *name, UINT type, int cx, int cy, UINT flags);
IMP BOOL WINAPI DrawIconEx(HDC dc, int x, int y, HICON icon, int cx, int cy, UINT step, HBRUSH flicker, UINT flags);
IMP long WINAPI GetWindowLongW(HWND w, int i);
IMP HDC WINAPI CreateCompatibleDC(HDC dc);
IMP HANDLE WINAPI CreateCompatibleBitmap(HDC dc, int w, int h);
IMP BOOL WINAPI DeleteDC(HDC dc);
IMP int WINAPI SetStretchBltMode(HDC dc, int mode);
IMP BOOL WINAPI SetBrushOrgEx(HDC dc, int x, int y, void *old);
IMP BOOL WINAPI StretchBlt(HDC d, int x, int y, int w, int h, HDC s, int sx, int sy, int sw, int sh, DWORD rop);

#define DT_CALCRECT 0x400
#define DT_RIGHT 0x2

enum {
    ID_HOST = 101,  /* Host | Join: pick one (while stopped) */
    ID_JOIN,        /* the button that starts it: "Host a game" / "Join a friend", "Stop" while running */
    ID_STATUS, ID_LOBBY, ID_COPYID, ID_JOINID, ID_LISTLABEL, ID_LIST, ID_ACTION, ID_REFRESH, ID_LAUNCH,
    ID_TEST, ID_DETAILS, ID_LOG, ID_HOWTO, ID_THEME, ID_TAB_HOWTO, ID_TAB_LOG, ID_COPYLOG, ID_LOBBYLABEL,
    ID_FIX, ID_NAMELABEL, ID_NAME, ID_SAVENAME, ID_DEVSOLO, ID_DEVTWO,
    ID_NAV_PLAY, ID_TAB_SET,      /* the sidebar's pages (with ID_TAB_LOG, ID_TAB_HOWTO) */
    ID_FIXTITLE, ID_FIXSUB,       /* Play: the SCBL Fix card's two lines */
    ID_SETTAB,                    /* Settings: Your game | Co-op */
    ID_FF_SWITCH, ID_FF_NOTE, ID_FF_INFO, ID_FF_DEFAULTS, ID_FF_SAVE, ID_FF_FOOT, ID_FF_LOCKED, ID_FF_HINT,
    ID_LAST /* keep last */
};
/* the game settings' controls, by setting: the control, its name, its limits (numbers) */
enum { ID_FF_CTL = 2000, ID_FF_LABEL = 2100, ID_FF_RANGE = 2200 };

typedef struct { UINT type, id, item, action, state; HWND hwnd; HDC dc; RECT rc; uintptr_t data; } DrawItem;
typedef struct { UINT type, id, item, width, height; uintptr_t data; } MeasureItem;

/* ---------- shared with the engine thread ---------- */

static CRITICAL_SECTION g_cs;
static char g_pend[65536];
static int g_npend;
static UiState g_state, g_shown;
static bool g_state_dirty;
static UiPerson g_lists[2][16];
static int g_nlist[2];
static bool g_list_dirty[2];
static bool g_alert, g_warned;
static Cmd g_cmds[32];
static int g_cmd_head, g_cmd_tail;

void ui_log(const char *line)
{
    EnterCriticalSection(&g_cs);
    int n = (int)strlen(line);
    if (line[0] == '!') g_warned = true;
    if (g_npend + n + 2 < (int)sizeof g_pend) {
        memcpy(g_pend + g_npend, line, (size_t)n);
        g_npend += n;
        g_pend[g_npend++] = '\r';
        g_pend[g_npend++] = '\n';
    }
    LeaveCriticalSection(&g_cs);
}

void ui_alert(const char *what)
{
    (void)what;
    EnterCriticalSection(&g_cs);
    g_alert = true;
    LeaveCriticalSection(&g_cs);
}

void ui_state(const UiState *s)
{
    EnterCriticalSection(&g_cs);
    if (memcmp(&g_state, s, sizeof *s) != 0) { g_state = *s; g_state_dirty = true; }
    LeaveCriticalSection(&g_cs);
}

void ui_list(int which, int n, const UiPerson *p)
{
    if (which < 0 || which > 1) return;
    EnterCriticalSection(&g_cs);
    if (n > 16) n = 16;
    bool same = n == g_nlist[which];
    for (int i = 0; same && i < n; i++)
        same = g_lists[which][i].id == p[i].id && g_lists[which][i].lobby == p[i].lobby &&
               strcmp(g_lists[which][i].name, p[i].name) == 0;
    if (!same) {
        for (int i = 0; i < n; i++) g_lists[which][i] = p[i];
        g_nlist[which] = n;
        g_list_dirty[which] = true;
    }
    LeaveCriticalSection(&g_cs);
}

static void push_cmd(CmdKind k, u64 arg)
{
    EnterCriticalSection(&g_cs);
    int next = (g_cmd_tail + 1) % 32;
    if (next != g_cmd_head) {
        g_cmds[g_cmd_tail].kind = k;
        g_cmds[g_cmd_tail].arg = arg;
        g_cmd_tail = next;
    }
    LeaveCriticalSection(&g_cs);
}

static uint8_t g_ff_buf[2048];
static int g_ff_n;

int ui_take_ff(uint8_t *out, int cap)
{
    EnterCriticalSection(&g_cs);
    int n = g_ff_n <= cap ? g_ff_n : 0;
    memcpy(out, g_ff_buf, (size_t)n);
    g_ff_n = 0;
    LeaveCriticalSection(&g_cs);
    return n;
}

bool ui_poll(Cmd *c)
{
    bool got = false;
    EnterCriticalSection(&g_cs);
    if (g_cmd_head != g_cmd_tail) {
        *c = g_cmds[g_cmd_head];
        g_cmd_head = (g_cmd_head + 1) % 32;
        got = true;
    }
    LeaveCriticalSection(&g_cs);
    return got;
}


/* ---------- window ---------- */

static HWND g_wnd, g_c[ID_LAST + 1];
static HFONT g_font, g_bold, g_big, g_mid, g_cap, g_brand, g_icon, g_icon_big;
/* g_mid: 14 px bold; g_cap: small bold for card titles; g_brand: the name in the sidebar;
   g_icon*: Segoe MDL2 Assets (Windows 10 and 11) */

/* Icons: Segoe MDL2 Assets glyphs */
#define IC_PLAY 0xE768
#define IC_SETTINGS 0xE713
#define IC_LIST 0xE8FD
#define IC_HELP 0xE897
#define IC_MOON 0xE708
#define IC_SUN 0xE706
#define IC_COPY 0xE8C8
#define IC_REFRESH 0xE72C
#define IC_PERSON 0xE77B
#define IC_HOST 0xE704
#define IC_JOIN 0xE8FA
#define IC_STOP 0xE71A
#define IC_INFO 0xE946
#define IC_DOWNLOAD 0xE896
#define IC_CHECK 0xE73E
#define IC_KICK 0xE8F8
#define IC_SEARCH 0xE721
#define IC_DELETE 0xE74D
#define IC_SAVE 0xE74E
#define IC_RESET 0xE777

static int g_dpi = 96;
static HANDLE g_thread;
static bool g_closing;
static u64 g_close_ms;
static UiPerson g_view[2][16];
static int g_nview[2];
static int g_list_shown = -1;
enum { TAB_PLAY, TAB_SET, TAB_LOG, TAB_HOWTO };
static int g_tab = TAB_PLAY;
static bool g_log_unseen;
static int g_role;      /* Play: 0 host, 1 join (what the start button does while stopped) */
static int g_settab;    /* Settings: 0 your game, 1 co-op */

static int S_(int v) { return v * g_dpi / 96; }
static WCHAR *wide(const char *s, WCHAR *buf, int n);

/* ---------- colors: SCBLLink's logo and the game's cover ---------- */

typedef struct {
    bool dark;
    DWORD bg, panel, text, dim, accent, accent_down, btn, btn_down, border, dis_bg, dis_text, status, sel_text;
    DWORD side, navsel, field;
} Theme;

/* dark: smoky grey page, lighter cards, a charcoal sidebar, night-vision green */
static const Theme DARK = { true,
    RGBC(42, 45, 48), RGBC(51, 55, 59), RGBC(223, 226, 220), RGBC(151, 155, 149),
    RGBC(154, 227, 95), RGBC(132, 204, 76), RGBC(59, 64, 69), RGBC(51, 55, 59), RGBC(67, 72, 77),
    RGBC(47, 50, 54), RGBC(95, 99, 104), RGBC(154, 227, 95), RGBC(20, 32, 16),
    RGBC(23, 24, 26), RGBC(34, 38, 31), RGBC(38, 41, 45) };
/* light: silver like the cover; the green darker so it reads on light grey */
static const Theme LIGHT = { false,
    RGBC(230, 232, 228), RGBC(246, 247, 245), RGBC(35, 38, 42), RGBC(96, 100, 94),
    RGBC(79, 154, 31), RGBC(63, 128, 24), RGBC(255, 255, 255), RGBC(236, 238, 233), RGBC(201, 205, 198),
    RGBC(236, 238, 233), RGBC(166, 170, 163), RGBC(61, 125, 20), RGBC(255, 255, 255),
    RGBC(223, 226, 221), RGBC(246, 247, 245), RGBC(255, 255, 255) };

/* The window (96 dpi units): the sidebar on the left, the page on the right. */
#define SIDE_W 184
#define CX 204   /* the page's left edge */
#define CW 636   /* its width */
#define WIN_W 860
#define WIN_H 552

static Theme T;
static HBRUSH g_br_bg, g_br_panel, g_br_accent, g_br_border, g_br_side, g_br_field;

/* text boxes and the list sit inside rounded frames drawn in the theme's colors */
typedef struct { HWND h; int x, y, w, hh; } Frame;
static Frame g_frames[64];
static int g_nframes;

/* A rounded box with smooth edges (device pixels): GDI's RoundRect has none, so it's drawn
   4 times bigger on bg and shrunk with averaging (HALFTONE). edge == fill: no outline. */
#define AA 4
static void aa_box(HDC dc, int l, int t, int r, int b, int radius, DWORD fill, DWORD edge, DWORD bg)
{
    int w = r - l, h = b - t;
    if (w <= 0 || h <= 0) return;
    HDC m = CreateCompatibleDC(dc);
    HANDLE bmp = CreateCompatibleBitmap(dc, w * AA, h * AA);
    if (!m || !bmp) { if (bmp) DeleteObject(bmp); if (m) DeleteDC(m); return; }
    HANDLE obmp = SelectObject(m, bmp);
    RECT all = { 0, 0, w * AA, h * AA };
    HBRUSH bb = CreateSolidBrush(bg);
    FillRect(m, &all, bb);
    DeleteObject(bb);
    HBRUSH fb = CreateSolidBrush(fill);
    int pw = edge == fill ? 1 : AA * (g_dpi >= 144 ? 2 : 1);
    HANDLE p = CreatePen(0, pw, edge);
    HANDLE ob = SelectObject(m, fb), op = SelectObject(m, p);
    int k = pw / 2;
    RoundRect(m, k, k, w * AA - k, h * AA - k, radius * AA, radius * AA);
    SelectObject(m, ob);
    SelectObject(m, op);
    DeleteObject(fb);
    DeleteObject(p);
    SetStretchBltMode(dc, 4 /* HALFTONE */);
    SetBrushOrgEx(dc, 0, 0, 0);
    StretchBlt(dc, l, t, w, h, m, 0, 0, w * AA, h * AA, 0x00CC0020 /* SRCCOPY */);
    SelectObject(m, obmp);
    DeleteObject(bmp);
    DeleteDC(m);
}

static void draw_frames(HDC dc)
{
    for (int i = 0; i < g_nframes; i++) {
        Frame *f = &g_frames[i];
        if (!IsWindowVisible(f->h)) continue;
        aa_box(dc, S_(f->x), S_(f->y), S_(f->x + f->w), S_(f->y + f->hh), S_(8), T.field, T.border, T.panel);
    }
}

/* a rounded box in fill with an edge, on bg (96 dpi units) */
static void rbox(HDC dc, int x, int y, int w, int h, int r, DWORD fill, DWORD edge, DWORD bg)
{
    aa_box(dc, S_(x), S_(y), S_(x + w), S_(y + h), S_(r), fill, edge, bg);
}
/* a dot (device pixels), on bg */
static void dot(HDC dc, int x, int y, int r, DWORD color, DWORD bg)
{
    aa_box(dc, x, y, x + r, y + r, r, color, color, bg);
}
/* an icon (one glyph) centred in r, device pixels */
static void glyph(HDC dc, WCHAR g, RECT r, HFONT f, DWORD color)
{
    WCHAR t[2] = { g, 0 };
    HANDLE of = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT_BK);
    SetTextColor(dc, color);
    DrawTextW(dc, t, 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, of);
}
/* a button's face: an icon and its text, centred together (icon 0: text only) */
static void icon_text(HDC dc, RECT r, WCHAR ic, const WCHAR *t, int n, HFONT f, DWORD color)
{
    HANDLE of = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT_BK);
    SetTextColor(dc, color);
    RECT m = { 0, 0, 2000, 100 };
    DrawTextW(dc, t, n, &m, DT_SINGLELINE | DT_CALCRECT);
    int iw = ic ? S_(16) + S_(8) : 0, tw = m.right - m.left, room = r.right - r.left - S_(12);
    if (iw + tw > room) tw = room - iw;
    int x = (r.left + r.right - iw - tw) / 2;
    if (ic) { RECT ir = { x, r.top, x + S_(16), r.bottom }; glyph(dc, ic, ir, g_icon, color); }
    SelectObject(dc, f);
    SetTextColor(dc, color);
    RECT tr = { x + iw, r.top, x + iw + tw, r.bottom };
    DrawTextW(dc, t, n, &tr, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, of);
}
static void card(HDC dc, int x, int y, int w, int h) { rbox(dc, x, y, w, h, 12, T.panel, T.border, T.bg); }
/* text at x, y, w, h (96 dpi units) */
static void label(HDC dc, const WCHAR *t, int x, int y, int w, int h, HFONT f, DWORD color, UINT fmt)
{
    HANDLE of = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT_BK);
    SetTextColor(dc, color);
    RECT r = { S_(x), S_(y), S_(x + w), S_(y + h) };
    DrawTextW(dc, t, -1, &r, fmt | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, of);
}
/* SCBLLink's logo: the program's own icon (res/scbllink.ico has 16 to 256 px, so
   Windows picks a sharp one for the screen's scale) */
static void draw_logo(HDC dc, int x, int y, int size)
{
    static HICON ic;
    int px = S_(size);
    if (!ic) ic = (HICON)LoadImageW(GetModuleHandleW(0), (const WCHAR *)(uintptr_t)1, 1 /* IMAGE_ICON */, px, px, 0);
    if (ic) DrawIconEx(dc, S_(x), S_(y), ic, px, px, 0, 0, 3 /* DI_NORMAL */);
}

static DWORD red_text(void) { return T.dark ? RGBC(240, 104, 98) : RGBC(192, 57, 47); }
static bool fixed(void) { return g_shown.game_found && g_shown.dll_ok; }

/* Play's SCBL Fix card: green when it's in, red when it's missing or old */
static DWORD banner_fill(void)
{
    return g_shown.fix_ok ? (T.dark ? RGBC(44, 53, 40) : RGBC(229, 241, 220)) : (T.dark ? RGBC(58, 42, 43) : RGBC(251, 236, 235));
}
static DWORD banner_edge(void)
{
    return g_shown.fix_ok ? (T.dark ? RGBC(76, 107, 54) : RGBC(159, 203, 127)) : (T.dark ? RGBC(107, 58, 58) : RGBC(226, 169, 165));
}
static HBRUSH banner_brush(void)
{
    static HBRUSH b;
    static DWORD c = 0xffffffff;
    DWORD f = banner_fill();
    if (f != c || !b) { if (b) DeleteObject(b); b = CreateSolidBrush(f); c = f; }
    return b;
}
/* Settings' co-op note: a soft red box */
static DWORD warn_fill(void) { return T.dark ? RGBC(58, 42, 43) : RGBC(251, 236, 235); }
static DWORD warn_edge(void) { return T.dark ? RGBC(107, 58, 58) : RGBC(226, 169, 165); }
static HBRUSH warn_brush(void)
{
    static HBRUSH b;
    static DWORD c = 0xffffffff;
    DWORD f = warn_fill();
    if (f != c || !b) { if (b) DeleteObject(b); b = CreateSolidBrush(f); c = f; }
    return b;
}

static void *sys_proc(const char *dll, const char *fn)
{
    void *m = LoadLibraryExA(dll, 0, 0x800 /* only from System32 */);
    return m ? GetProcAddress(m, fn) : 0;
}

static HWND g_ffc[64];   /* the game settings' controls, by setting */
static HWND g_ffl[64];   /* their names (numbers and choices; switches carry their own) */
static HWND g_ffr[64];   /* their limits (numbers) */

/* Title bar, scroll bars and drop-down lists follow the theme (Windows 10 1809+ and 11) */
static void apply_system_parts(void)
{
    typedef long(WINAPI * DwmSet)(HWND, DWORD, const void *, DWORD);
    typedef long(WINAPI * SetTheme)(HWND, const WCHAR *, const WCHAR *);
    static DwmSet dwm;
    static SetTheme theme;
    static bool looked;
    if (!looked) {
        looked = true;
        dwm = (DwmSet)sys_proc("dwmapi.dll", "DwmSetWindowAttribute");
        theme = (SetTheme)sys_proc("uxtheme.dll", "SetWindowTheme");
    }
    if (dwm) {
        BOOL on = T.dark;
        if (dwm(g_wnd, 20, &on, sizeof on) != 0) dwm(g_wnd, 19, &on, sizeof on);
    }
    if (theme) {
        const WCHAR *name = T.dark ? WTEXT("DarkMode_Explorer") : WTEXT("Explorer");
        theme(g_c[ID_LOG], name, 0);
        theme(g_c[ID_HOWTO], name, 0);
        theme(g_c[ID_LIST], name, 0);
    }
}

static void save_theme(void) { file_write_text("scbllink_theme.txt", T.dark ? "dark\n" : "light\n"); }

static bool saved_light(void)
{
    FILE *f = fopen("scbllink_theme.txt", "rb");
    if (!f) return false;
    char b[16] = { 0 };
    fread(b, 1, sizeof b - 1, f);
    fclose(f);
    return b[0] == 'l';
}

static void set_theme(bool dark)
{
    T = dark ? DARK : LIGHT;
    if (g_br_bg) {
        DeleteObject(g_br_bg); DeleteObject(g_br_panel); DeleteObject(g_br_accent); DeleteObject(g_br_border);
        DeleteObject(g_br_side); DeleteObject(g_br_field);
    }
    g_br_side = CreateSolidBrush(T.side);
    g_br_field = CreateSolidBrush(T.field);
    g_br_border = CreateSolidBrush(T.border);
    g_br_bg = CreateSolidBrush(T.bg);
    g_br_panel = CreateSolidBrush(T.panel);
    g_br_accent = CreateSolidBrush(T.accent);
    if (g_wnd) {
        SetWindowTextW(g_c[ID_THEME], T.dark ? WTEXT("Light theme") : WTEXT("Dark theme"));
        apply_system_parts();
        RedrawWindow(g_wnd, 0, 0, 0x1 | 0x4 | 0x80 | 0x400); /* invalidate, erase, children, frame */
    }
}

/* ---------- the game settings: groups (cards), what each looks like ---------- */

enum { G_DISPLAY, G_MOUSE, G_GAME, G_LOOK, G_UNLOCKS, G_GRIM, G_KESTREL, G_BRIGGS, G_KOBIN, G_CHARLIE, G_N };
static const char *const G_TITLE[G_N] = { "DISPLAY", "MOUSE", "GAME", "LOOK", "UNLOCKS", "GRIM (STEALTH)", "KESTREL",
                                          "BRIGGS (CO-OP)", "KOBIN (HUNTER)", "CHARLIE (WAVES)" };
static const int G_COL[G_N] = { 0, 0, 1, 1, 0, 0, 0, 1, 1, 2 };
static RECT g_grect[G_N];  /* each card (96 dpi units, as x, y, x + w, y + h) */

static int group_of(int i)
{
    const FfSetting *f = &FF_SETTINGS[i];
    if (f->shared) {
        if (!strcmp(f->sec, "GHOST")) return G_GRIM;
        if (!strcmp(f->sec, "KESTREL")) return G_KESTREL;
        if (!strcmp(f->sec, "COOP")) return G_BRIGGS;
        if (!strcmp(f->sec, "HUNTER")) return G_KOBIN;
        if (!strcmp(f->sec, "EXTRACTION")) return G_CHARLIE;
        return G_UNLOCKS;
    }
    if (!strcmp(f->sec, FF_SCBL_SEC)) return G_LOOK;
    static const char *const disp[] = { "UltraWideSupport", "FOVFactor", "ScreenCullBias", "FPSLimit", "UseMotionBlur", "DisableNightVisionFlash", 0 };
    for (int k = 0; disp[k]; k++) if (!strcmp(f->key, disp[k])) return G_DISPLAY;
    if (!strcmp(f->key, "DisableNegativeMouseAcceleration") || !strcmp(f->key, "TurnSpeed")) return G_MOUSE;
    return G_GAME;
}
static bool group_shared(int g) { return g >= G_UNLOCKS; }

/* the name a setting shows in its card: without the card's name in front ("Briggs: ") */
static void short_label(int i, char *out, int n)
{
    const char *l = FF_SETTINGS[i].label;
    int g = group_of(i);
    if (g >= G_GRIM && g != G_KESTREL) { const char *c = strstr(l, ": "); if (c) l = c + 2; }
    sfmt(out, (size_t)n, "%s", l);
    if (out[0] >= 'a' && out[0] <= 'z') out[0] = (char)(out[0] - 32);
}
/* a number's limits, shown small next to its box */
static void range_text(int i, char *out, int n)
{
    const FfSetting *f = &FF_SETTINGS[i];
    if (f->kind == FF_FLOAT) {
        int a = (int)(f->min * 100 + 0.5), b = (int)(f->max * 100 + 0.5);
        sfmt(out, (size_t)n, "%d.%02d\xE2\x80\x93%d.%02d", a / 100, a % 100, b / 100, b % 100);
    } else if (f->kind == FF_INT && f->zero_ok) sfmt(out, (size_t)n, "0 = off");
    else if (f->kind == FF_INT) sfmt(out, (size_t)n, "%d\xE2\x80\x93%d", (int)f->min, (int)f->max);
    else out[0] = 0;
}
static int setting_of(HWND h)
{
    for (int i = 0; i < FF_NSETTINGS && i < 64; i++) if (g_ffc[i] == h || g_ffl[i] == h || g_ffr[i] == h) return i;
    return -1;
}

/* ---------- drawing the owner-drawn controls ---------- */

static bool g_swon[65];   /* the switches, by setting; [64] = the co-op lock */
static int g_choice[64];  /* the choices (a button: each click picks the next one), by setting */
static bool g_ff_unlocked;

/* a choice setting's k-th choice ("A|B|C"), and how many it has */
static int choice_text(int i, int k, char *out, int n)
{
    const char *p = FF_SETTINGS[i].choices;
    int count = 0;
    out[0] = 0;
    while (p) {
        const char *e = strchr(p, '|');
        int l = e ? (int)(e - p) : (int)strlen(p);
        if (count == k) { if (l >= n) l = n - 1; memcpy(out, p, (size_t)l); out[l] = 0; }
        count++;
        p = e ? e + 1 : 0;
    }
    return count;
}

static bool is_primary(UINT id) { return id == ID_LAUNCH || id == ID_FF_SAVE || (id == ID_JOIN && g_shown.mode == MODE_IDLE); }
static bool is_nav(UINT id) { return id == ID_NAV_PLAY || id == ID_TAB_SET || id == ID_TAB_LOG || id == ID_TAB_HOWTO || id == ID_THEME; }
static int tab_of(UINT id)
{
    return id == ID_NAV_PLAY ? TAB_PLAY : id == ID_TAB_SET ? TAB_SET : id == ID_TAB_LOG ? TAB_LOG : id == ID_TAB_HOWTO ? TAB_HOWTO : -1;
}
/* what a button sits on: the sidebar, a card, the Fix card or the page */
static HBRUSH under(UINT id)
{
    if (id == ID_DEVSOLO || id == ID_DEVTWO) return g_br_side;
    if (id == ID_FIX) return banner_brush();
    if (id == ID_ACTION || id == ID_REFRESH || id == ID_COPYID || id == ID_JOINID || id == ID_SAVENAME || id == ID_FF_SWITCH) return g_br_panel;
    if (id >= ID_FF_CTL && id < ID_FF_CTL + 64) return g_br_panel;
    return g_br_bg;
}
static DWORD under_color(UINT id)
{
    HBRUSH b = under(id);
    return b == g_br_side ? T.side : b == g_br_panel ? T.panel : b == g_br_bg ? T.bg : banner_fill();
}

/* the sidebar's pages: the picked one on a lighter pill with a green bar on its left */
static void draw_nav(DrawItem *d)
{
    bool on = tab_of(d->id) == g_tab;
    FillRect(d->dc, &d->rc, g_br_side);
    if (on) {
        aa_box(d->dc, d->rc.left, d->rc.top, d->rc.right, d->rc.bottom, S_(8), T.navsel, T.dark ? T.navsel : T.border, T.side);
        aa_box(d->dc, d->rc.left + S_(5), d->rc.top + S_(9), d->rc.left + S_(8), d->rc.bottom - S_(9), S_(3), T.accent, T.accent, T.navsel);
    }
    WCHAR w[32];
    int n = GetWindowTextW(d->hwnd, w, 32);
    WCHAR ic = d->id == ID_NAV_PLAY ? IC_PLAY : d->id == ID_TAB_SET ? IC_SETTINGS : d->id == ID_TAB_LOG ? IC_LIST
             : d->id == ID_TAB_HOWTO ? IC_HELP : T.dark ? IC_SUN : IC_MOON;
    RECT ir = { d->rc.left + S_(16), d->rc.top, d->rc.left + S_(34), d->rc.bottom };
    glyph(d->dc, ic, ir, g_icon, on ? T.text : T.dim);
    HANDLE of = SelectObject(d->dc, on ? g_bold : g_font);
    SetBkMode(d->dc, TRANSPARENT_BK);
    SetTextColor(d->dc, on ? T.text : T.dim);
    RECT tr = d->rc;
    tr.left += S_(44);
    DrawTextW(d->dc, w, n, &tr, DT_VCENTER | DT_SINGLELINE);
    SelectObject(d->dc, of);
    if (d->id == ID_TAB_LOG && g_log_unseen) { /* a red dot: a warning arrived */
        int r = S_(8);
        dot(d->dc, d->rc.right - S_(20), (d->rc.top + d->rc.bottom - r) / 2, r, red_text(), on ? T.navsel : T.side);
    }
}

/* Host | Join (Play) and Your game | Co-op (Settings): two halves, the picked one raised */
static void draw_segment(DrawItem *d)
{
    bool off = d->state & ODS_DISABLED;
    bool host = d->id == ID_HOST;
    const UiState *s = &g_shown;
    bool right = host ? (s->mode == MODE_JOIN || (s->mode == MODE_IDLE && g_role == 1)) : g_settab == 1;
    DWORD tray = T.dark ? T.side : T.btn_down;
    aa_box(d->dc, d->rc.left, d->rc.top, d->rc.right, d->rc.bottom, S_(10), tray, T.border, T.bg);
    int mid = (d->rc.left + d->rc.right) / 2, k = S_(4);
    RECT half = { right ? mid + k / 2 : d->rc.left + k, d->rc.top + k, right ? d->rc.right - k : mid - k / 2, d->rc.bottom - k };
    aa_box(d->dc, half.left, half.top, half.right, half.bottom, S_(8), T.navsel, off ? T.border : T.accent, tray);
    SetBkMode(d->dc, TRANSPARENT_BK);
    for (int i = 0; i < 2; i++) {
        bool on = (i == 1) == right;
        int l = i ? mid : d->rc.left, r = i ? d->rc.right : mid;
        DWORD tc = off ? T.dis_text : on ? T.text : T.dim;
        if (host) { /* an icon, the name and a line under it */
            RECT ir = { l + S_(14), d->rc.top, l + S_(40), d->rc.bottom };
            glyph(d->dc, i ? IC_JOIN : IC_HOST, ir, g_icon_big, off ? T.dis_text : on ? T.status : T.dim);
            HANDLE of = SelectObject(d->dc, g_bold);
            SetTextColor(d->dc, tc);
            RECT tr = { l + S_(50), d->rc.top + S_(10), r - S_(8), d->rc.top + S_(29) };
            DrawTextW(d->dc, i ? WTEXT("JOIN A FRIEND") : WTEXT("HOST A GAME"), -1, &tr, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            SelectObject(d->dc, g_font);
            SetTextColor(d->dc, off ? T.dis_text : T.dim);
            RECT sr = { l + S_(50), d->rc.top + S_(29), r - S_(8), d->rc.bottom - S_(8) };
            DrawTextW(d->dc, i ? WTEXT("From the list or a code") : WTEXT("Friends-only lobby"), -1, &sr, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            SelectObject(d->dc, of);
        } else {
            HANDLE of = SelectObject(d->dc, g_bold);
            SetTextColor(d->dc, on ? (T.dark ? T.accent : T.status) : T.dim);
            RECT tr = { l, d->rc.top, r, d->rc.bottom };
            DrawTextW(d->dc, i ? WTEXT("CO-OP") : WTEXT("YOUR GAME"), -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(d->dc, of);
        }
    }
}

/* a settings row: its name on the left, a small switch on the right */
static void draw_switch(DrawItem *d, bool on)
{
    bool off = d->state & ODS_DISABLED, focus = d->state & ODS_FOCUS;
    FillRect(d->dc, &d->rc, under(d->id));
    int h = S_(18), w = S_(34), x = d->rc.right - w, y = (d->rc.top + d->rc.bottom - h) / 2;
    DWORD fill = on ? (off ? (T.dark ? RGBC(62, 84, 46) : RGBC(178, 210, 156)) : T.accent) : (off ? T.dis_bg : (T.dark ? RGBC(80, 85, 91) : RGBC(185, 189, 182)));
    aa_box(d->dc, x, y, x + w, y + h, h, fill, focus && !off ? T.text : fill, under_color(d->id));
    int k = h - S_(6), kx = on ? x + w - k - S_(3) : x + S_(3), ky = y + S_(3);
    DWORD kc = off ? T.dis_text : on ? (T.dark ? T.sel_text : RGBC(255, 255, 255)) : RGBC(255, 255, 255);
    dot(d->dc, kx, ky, k, kc, fill);
    SetBkMode(d->dc, TRANSPARENT_BK);
    HANDLE of = SelectObject(d->dc, d->id == ID_FF_SWITCH ? g_bold : g_font);
    WCHAR t[128];
    int n = GetWindowTextW(d->hwnd, t, 128);
    SetTextColor(d->dc, off ? T.dis_text : T.text);
    RECT tr = { d->rc.left, d->rc.top, x - S_(10), d->rc.bottom };
    DrawTextW(d->dc, t, n, &tr, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(d->dc, of);
}

/* the "i" button: a ring with an i in it */
static void draw_info(DrawItem *d)
{
    bool down = d->state & ODS_SELECTED, focus = d->state & ODS_FOCUS;
    FillRect(d->dc, &d->rc, g_br_bg);
    int sz = d->rc.right - d->rc.left;
    if (d->rc.bottom - d->rc.top < sz) sz = d->rc.bottom - d->rc.top;
    RECT r = { d->rc.left, d->rc.top + (down ? 1 : 0), d->rc.left + sz, d->rc.top + sz + (down ? 1 : 0) };
    glyph(d->dc, IC_INFO, r, g_icon_big, focus ? T.accent : T.dim);
}

/* a choice setting: a box with the picked one and a chevron; each click picks the next */
static void draw_choice(DrawItem *d, int i)
{
    bool off = d->state & ODS_DISABLED, focus = d->state & ODS_FOCUS;
    aa_box(d->dc, d->rc.left, d->rc.top, d->rc.right, d->rc.bottom, S_(8), off ? T.dis_bg : T.field, focus && !off ? T.accent : T.border, T.panel);
    char t[32];
    WCHAR w[32];
    choice_text(i, g_choice[i], t, sizeof t);
    DWORD tc = off ? T.dis_text : T.text;
    RECT ir = { d->rc.right - S_(22), d->rc.top, d->rc.right - S_(6), d->rc.bottom };
    glyph(d->dc, 0xE70D /* chevron down */, ir, g_icon, off ? T.dis_text : T.dim);
    HANDLE of = SelectObject(d->dc, g_font);
    SetBkMode(d->dc, TRANSPARENT_BK);
    SetTextColor(d->dc, tc);
    RECT tr = { d->rc.left + S_(8), d->rc.top, d->rc.right - S_(24), d->rc.bottom };
    DrawTextW(d->dc, wide(t, w, 32), -1, &tr, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(d->dc, of);
}

static void draw_button(DrawItem *d)
{
    if (is_nav(d->id)) { draw_nav(d); return; }
    if (d->id == ID_HOST || d->id == ID_SETTAB) { draw_segment(d); return; }
    if (d->id == ID_FF_INFO) { draw_info(d); return; }
    if (d->id == ID_FF_SWITCH) { draw_switch(d, g_swon[64]); return; }
    if (d->id >= ID_FF_CTL && d->id < ID_FF_CTL + 64) {
        int i = (int)d->id - ID_FF_CTL;
        if (FF_SETTINGS[i].kind == FF_CHOICE) draw_choice(d, i);
        else draw_switch(d, g_swon[i]);
        return;
    }
    bool down = d->state & ODS_SELECTED, off = d->state & ODS_DISABLED, focus = d->state & ODS_FOCUS;
    bool stop = d->id == ID_JOIN && g_shown.mode != MODE_IDLE;
    bool kick = d->id == ID_ACTION && g_shown.mode == MODE_HOST;
    bool dev_on = (d->id == ID_DEVTWO && g_shown.dev_two) || (d->id == ID_DEVSOLO && g_shown.dev_solo);
    DWORD fill, text, edge;
    if (off) { fill = T.dis_bg; text = T.dis_text; edge = T.dis_bg; }
    else if (is_primary(d->id)) { fill = down ? T.accent_down : T.accent; text = T.sel_text; edge = fill; }
    else if (stop || kick) { fill = down ? T.btn_down : T.btn; text = red_text(); edge = T.dark ? RGBC(107, 58, 58) : RGBC(226, 169, 165); }
    else if (dev_on) { fill = down ? T.btn_down : T.navsel; text = T.status; edge = T.accent; }
    else { fill = down ? T.btn_down : T.btn; text = T.text; edge = T.border; }
    int r = S_(8);
    aa_box(d->dc, d->rc.left, d->rc.top, d->rc.right, d->rc.bottom, r, fill, edge, under_color(d->id));
    if (focus && !off) { /* keyboard focus: a ring inside */
        int k = S_(3);
        aa_box(d->dc, d->rc.left + k, d->rc.top + k, d->rc.right - k, d->rc.bottom - k, r - k / 2, fill, is_primary(d->id) ? T.sel_text : T.dim, fill);
    }
    WCHAR w[64];
    int n = GetWindowTextW(d->hwnd, w, 64);
    RECT tr = d->rc;
    if (down) { tr.top += 1; tr.bottom += 1; }
    WCHAR ic = d->id == ID_LAUNCH ? IC_PLAY : d->id == ID_COPYID || d->id == ID_COPYLOG ? IC_COPY
             : d->id == ID_REFRESH ? IC_REFRESH : d->id == ID_JOINID ? IC_JOIN
             : d->id == ID_ACTION ? (kick ? IC_KICK : IC_JOIN)
             : d->id == ID_JOIN ? (stop ? IC_STOP : g_role ? IC_JOIN : IC_PLAY)
             : d->id == ID_FIX ? (g_shown.fix_ok ? IC_DELETE : IC_DOWNLOAD)
             : d->id == ID_DETAILS ? IC_INFO : d->id == ID_TEST ? (g_shown.invite_from[0] ? IC_JOIN : IC_SEARCH)
             : d->id == ID_SAVENAME ? IC_CHECK : d->id == ID_FF_SAVE ? IC_SAVE : d->id == ID_FF_DEFAULTS ? IC_RESET : 0;
    icon_text(d->dc, tr, ic, w, n, is_primary(d->id) || d->id == ID_JOIN ? g_bold : g_font, text);
}

static void draw_list_item(DrawItem *d)
{
    bool sel = d->state & ODS_SELECTED;
    FillRect(d->dc, &d->rc, sel ? g_br_accent : g_br_field);
    if (d->item == (UINT)-1) return;
    WCHAR w[UI_NAME + 8];
    int n = (int)SendMessageW(d->hwnd, LB_GETTEXT, d->item, (LPARAM)w);
    if (n <= 0) return;
    RECT ir = { d->rc.left + S_(8), d->rc.top, d->rc.left + S_(26), d->rc.bottom };
    glyph(d->dc, IC_PERSON, ir, g_icon, sel ? T.sel_text : T.status);
    HANDLE of = SelectObject(d->dc, g_font);
    SetBkMode(d->dc, TRANSPARENT_BK);
    SetTextColor(d->dc, sel ? T.sel_text : T.text);
    RECT tr = d->rc;
    tr.left += S_(34);
    DrawTextW(d->dc, w, n, &tr, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(d->dc, of);
}

/* colors for labels and text boxes; returns the background brush */
static LRESULT control_colors(UINT msg, HDC dc, HWND c)
{
    int s = setting_of(c);
    if (c == g_c[ID_FIXTITLE] || c == g_c[ID_FIXSUB]) {
        SetTextColor(dc, c == g_c[ID_FIXSUB] ? T.dim : g_shown.fix_ok ? T.status : red_text());
        SetBkColor(dc, banner_fill());
        return (LRESULT)banner_brush();
    }
    if (c == g_c[ID_FF_NOTE]) {
        SetTextColor(dc, red_text());
        SetBkColor(dc, warn_fill());
        return (LRESULT)warn_brush();
    }
    if (c == g_c[ID_FF_FOOT] || c == g_c[ID_FF_LOCKED] || c == g_c[ID_FF_HINT]) {
        SetTextColor(dc, T.dim);
        SetBkColor(dc, T.bg);
        return (LRESULT)g_br_bg;
    }
    /* text boxes (read-only and greyed ones ask as labels do) and the list: the field color;
       everything else here is on a card */
    bool field = (msg == WM_CTLCOLOREDIT && c != g_c[ID_LOG] && c != g_c[ID_HOWTO]) || c == g_c[ID_LOBBY] || c == g_c[ID_NAME] ||
                 (s >= 0 && c == g_ffc[s]);
    DWORD tc = T.text;
    if (c == g_c[ID_LISTLABEL] || c == g_c[ID_LOBBYLABEL] || c == g_c[ID_NAMELABEL]) tc = T.dim;
    if (c == g_c[ID_STATUS]) tc = g_shown.ready ? T.status : g_shown.status[0] == '!' ? red_text() : T.text;
    if (s >= 0) {
        bool locked = FF_SETTINGS[s].shared && (!g_ff_unlocked || g_shown.ff_host_rules);
        tc = c == g_ffr[s] ? (locked ? T.dis_text : T.dim) : locked ? T.dis_text : T.text;
        if (c == g_ffc[s]) tc = locked ? T.dis_text : T.text;
    }
    SetTextColor(dc, tc);
    SetBkColor(dc, field ? T.field : T.panel);
    return (LRESULT)(field ? g_br_field : g_br_panel);
}

static WCHAR *wide(const char *s, WCHAR *buf, int n)
{
    int r = MultiByteToWideChar(CP_UTF8, 0, s, -1, buf, n);
    if (r <= 0) buf[0] = 0;
    buf[n - 1] = 0;
    return buf;
}

static void set_text(int id, const char *utf8)
{
    static WCHAR w[4096];
    SetWindowTextW(g_c[id], wide(utf8, w, 4096));
}

static void set_text_once(int id, const char *literal)
{
    static const char *last[ID_LAST + 1];
    if (last[id] == literal) return;
    last[id] = literal;
    set_text(id, literal);
}

static HWND make(int id, const WCHAR *cls, const WCHAR *text, DWORD style, int x, int y, int w, int h, HFONT f)
{
    HWND h_ = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, S_(x), S_(y), S_(w), S_(h), g_wnd,
                              (HMENU)(uintptr_t)id, GetModuleHandleW(0), 0);
    SendMessageW(h_, WM_SETFONT, (WPARAM)f, 1);
    return h_;
}
static HWND ctl(int id, const WCHAR *cls, const WCHAR *text, DWORD style, int x, int y, int w, int h, HFONT f)
{
    return g_c[id] = make(id, cls, text, style, x, y, w, h, f);
}
/* a box (text box or list) inside a drawn frame; ix/iy = the space inside the frame */
static HWND framed(int id, const WCHAR *cls, DWORD style, int x, int y, int w, int h, int ix, int iy)
{
    HWND c = make(id, cls, WTEXT(""), style, x + ix, y + iy, w - 2 * ix, h - 2 * iy, g_font);
    if (g_nframes < 64) g_frames[g_nframes++] = (Frame){ c, x, y, w, h };
    if (id < ID_LAST) g_c[id] = c;
    return c;
}

/* ---------- tool tips: what each setting does, on the mouse ---------- */

static HWND g_tip;
typedef struct { UINT size, flags; HWND hwnd; uintptr_t id; RECT rect; HINSTANCE inst; WCHAR *text; LPARAM lp; void *reserved; } ToolInfo;
#define TTM_ADDTOOLW 0x432
#define TTM_SETMAXTIPWIDTH 0x418
#define TTM_SETDELAYTIME 0x403

static void tip(HWND on, const char *text)
{
    if (!g_tip || !on || !text) return;
    static WCHAR w[400];
    ToolInfo t;
    memset(&t, 0, sizeof t);
    t.size = sizeof t;
    t.flags = 0x10 /* TTF_SUBCLASS */ | 0x1 /* TTF_IDISHWND */;
    t.hwnd = g_wnd;
    t.id = (uintptr_t)on;
    t.text = wide(text, w, 400);
    SendMessageW(g_tip, TTM_ADDTOOLW, 0, (LPARAM)&t);
}

/* ---------- building the window ---------- */

/* Play (96 dpi units) */
#define FIX_Y 16
#define FIX_H 58
#define ROW_Y 88
#define ROW_H 56
#define LIST_Y 158
#define LIST_H 266
#define STAT_Y 438
#define STAT_H 48
#define BTN_Y 498
/* Settings */
#define SET_Y 58          /* the cards' top (Your game) */
#define LOCK_Y 58         /* co-op: the lock card */
#define LOCK_H 82
#define COOP_Y 150        /* co-op: the cards' top */
#define ROW 26            /* a settings row */
#define FOOT_Y (WIN_H - 50)

static void build(void)
{
    const WCHAR *B = WTEXT("BUTTON"), *ST = WTEXT("STATIC"), *E = WTEXT("EDIT"), *L = WTEXT("LISTBOX");
    const DWORD OD = WS_TABSTOP | BS_OWNERDRAW, ELL = 0x4000 /* SS_ENDELLIPSIS */;
    g_tip = CreateWindowExW(0x8 /* topmost */, WTEXT("tooltips_class32"), 0, 0x80000000 | 0x1 | 0x2 /* popup, always tip, no prefix */,
                            0, 0, 0, 0, g_wnd, 0, GetModuleHandleW(0), 0);
    if (g_tip) {
        SendMessageW(g_tip, TTM_SETMAXTIPWIDTH, 0, S_(360));
        SendMessageW(g_tip, TTM_SETDELAYTIME, 2 /* TTDT_AUTOPOP */, 30000);
        SendMessageW(g_tip, TTM_SETDELAYTIME, 3 /* TTDT_INITIAL */, 300);
    }

    /* the sidebar: the logo and the name are drawn (draw_page), the pages are buttons */
    ctl(ID_NAV_PLAY, B, WTEXT("PLAY"), OD, 12, 92, SIDE_W - 24, 36, g_font);
    ctl(ID_TAB_SET, B, WTEXT("SETTINGS"), OD, 12, 132, SIDE_W - 24, 36, g_font);
    ctl(ID_TAB_LOG, B, WTEXT("LOG"), OD, 12, 172, SIDE_W - 24, 36, g_font);
#ifdef SCBL_DEV
    ctl(ID_DEVTWO, B, WTEXT("2nd game: off"), OD, 12, 246, SIDE_W - 24, 28, g_font);
    ctl(ID_DEVSOLO, B, WTEXT("Record alone: off"), OD, 12, 280, SIDE_W - 24, 28, g_font);
#endif
    ctl(ID_TAB_HOWTO, B, WTEXT("How to"), OD, 12, WIN_H - 92, SIDE_W - 24, 36, g_font);
    ctl(ID_THEME, B, WTEXT(""), OD, 12, WIN_H - 52, SIDE_W - 24, 36, g_font);

    /* Play: the SCBL Fix card */
    ctl(ID_FIXTITLE, ST, WTEXT(""), ELL, CX + 16, FIX_Y + 10, CW - 200, 20, g_bold);
    ctl(ID_FIXSUB, ST, WTEXT(""), ELL, CX + 16, FIX_Y + 31, CW - 200, 18, g_font);
    ctl(ID_FIX, B, WTEXT("Install SCBL Fix"), OD, CX + CW - 16 - 164, FIX_Y + 13, 164, 32, g_font);
    /* Host | Join, and the button that starts it (Stop while running) */
    ctl(ID_HOST, B, WTEXT("Host or join"), OD, CX, ROW_Y, 420, ROW_H, g_bold);
    ctl(ID_JOIN, B, WTEXT("HOST A GAME"), OD, CX + 432, ROW_Y, CW - 432, ROW_H, g_bold);
    /* the players card: the list, the lobby code, your name */
    ctl(ID_LISTLABEL, ST, WTEXT(""), ELL, CX + 96, LIST_Y + 15, 300, 18, g_font);
    ctl(ID_ACTION, B, WTEXT("Join"), OD, CX + CW - 16 - 204, LIST_Y + 10, 100, 28, g_bold);
    ctl(ID_REFRESH, B, WTEXT("Refresh"), OD, CX + CW - 16 - 96, LIST_Y + 10, 96, 28, g_font);
    framed(ID_LIST, L, WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
           CX + 14, LIST_Y + 46, CW - 28, 128, 4, 4);
    ctl(ID_LOBBYLABEL, ST, WTEXT("Lobby code"), 0, CX + 16, LIST_Y + 192, 150, 18, g_font);
    framed(ID_LOBBY, E, WS_TABSTOP | ES_AUTOHSCROLL, CX + 172, LIST_Y + 186, 190, 30, 8, 7);
    ctl(ID_COPYID, B, WTEXT("Copy code"), OD, CX + 372, LIST_Y + 186, 118, 30, g_font);
    ctl(ID_JOINID, B, WTEXT("Join this code"), OD, CX + 498, LIST_Y + 186, CW - 498 - 14, 30, g_font);
    ctl(ID_NAMELABEL, ST, WTEXT("Your name in the game"), 0, CX + 16, LIST_Y + 230, 150, 18, g_font);
    framed(ID_NAME, E, WS_TABSTOP | ES_AUTOHSCROLL, CX + 172, LIST_Y + 224, 190, 30, 8, 7);
    SendMessageW(g_c[ID_NAME], EM_SETLIMITTEXT, 16, 0);
    ctl(ID_SAVENAME, B, WTEXT("Save name"), OD, CX + 372, LIST_Y + 224, 118, 30, g_font);
    /* the status (a dot in front, drawn), then Test, Details and Start */
    ctl(ID_STATUS, ST, WTEXT(""), ELL, CX + 38, STAT_Y + 14, CW - 54, 22, g_mid);
    ctl(ID_TEST, B, WTEXT("Test the link"), OD, CX, BTN_Y, 140, 38, g_font);
    ctl(ID_DETAILS, B, WTEXT("Details"), OD, CX + 148, BTN_Y, 140, 38, g_font);
    ctl(ID_LAUNCH, B, WTEXT("START BLACKLIST"), OD, CX + CW - 220, BTN_Y, 220, 38, g_bold);

    /* Log and How to: a title (drawn) and the box in a card; Log's button on the title's line */
    ctl(ID_COPYLOG, B, WTEXT("Copy log"), OD, CX + CW - 120, 16, 120, 30, g_font);
    ctl(ID_LOG, E, WTEXT(""), WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, CX + 14, 70, CW - 28, WIN_H - 70 - 32, g_font);
    ctl(ID_HOWTO, E, WTEXT(""), WS_VSCROLL | ES_MULTILINE | ES_READONLY, CX + 14, 70, CW - 28, WIN_H - 70 - 32, g_font);
    SendMessageW(g_c[ID_LOG], EM_SETLIMITTEXT, 0x7FFFFFFE, 0);

    /* Settings: Your game | Co-op, what to point at, the cards (drawn) and their rows */
    ctl(ID_SETTAB, B, WTEXT("Your game or co-op"), OD, CX, 14, 300, 34, g_bold);
    ctl(ID_FF_HINT, ST, WTEXT("Point at an option to see what it does"), 0x2 /* SS_RIGHT */ | ELL, CX + 316, 22, CW - 316 - 40, 18, g_font);
    ctl(ID_FF_INFO, B, WTEXT("What every option does"), OD, CX + CW - 30, 16, 30, 30, g_font);
    tip(g_c[ID_FF_INFO], "What every option does");
    ctl(ID_FF_LOCKED, ST, WTEXT(""), 0x1 /* SS_CENTER */, CX, 200, CW, 60, g_font);
    /* co-op: the lock and the note */
    ctl(ID_FF_SWITCH, B, WTEXT("Change the co-op settings"), OD, CX + CW - 16 - 250, LOCK_Y + 10, 250, 24, g_font);
    tip(g_c[ID_FF_SWITCH], "Off: the co-op settings are locked, so they aren't changed by mistake. Turn it on to change them. "
                           "Enemies, game over and unlocks change the missions themselves, so everyone who plays together needs the same ones.");
    ctl(ID_FF_NOTE, ST, WTEXT(""), 0, CX + 26, LOCK_Y + 44, CW - 52, 30, g_font);
    /* the cards: each group's place, then each setting's row in it */
    int rows[G_N] = { 0 };
    for (int i = 0; i < FF_NSETTINGS && i < 64; i++) rows[group_of(i)]++;
    {
        int top[3] = { SET_Y, SET_Y, SET_Y }, ctop[3] = { COOP_Y, COOP_Y, COOP_Y };
        int w2 = (CW - 12) / 2, w3 = (CW - 24) / 3;
        for (int g = 0; g < G_N; g++) {
            bool sh = group_shared(g);
            int w = sh ? w3 : w2, x = CX + G_COL[g] * (w + 12), *t = sh ? &ctop[G_COL[g]] : &top[G_COL[g]];
            int h = 36 + rows[g] * ROW + 8;
            g_grect[g] = (RECT){ x, *t, x + w, *t + h };
            *t += h + 10;
        }
    }
    int at[G_N] = { 0 };
    for (int i = 0; i < FF_NSETTINGS && i < 64; i++) {
        const FfSetting *f = &FF_SETTINGS[i];
        int g = group_of(i);
        RECT gr = g_grect[g];
        int x = gr.left + 14, w = gr.right - gr.left - 28, y = gr.top + 34 + at[g]++ * ROW;
        char name[96], lim[32];
        WCHAR wn[96], wl[32];
        short_label(i, name, sizeof name);
        wide(name, wn, 96);
        if (f->kind == FF_BOOL) {
            g_ffc[i] = make(ID_FF_CTL + i, B, wn, OD, x, y + 1, w, 24, g_font);
        } else {
            int bw = f->kind == FF_CHOICE ? (f->shared ? 92 : 112) : 52;
            range_text(i, lim, sizeof lim);
            int rw = lim[0] && !f->shared ? 62 : 0; /* the narrow co-op cards: the limits are in the tool tip */
            g_ffl[i] = make(ID_FF_LABEL + i, ST, wn, ELL | 0x100 /* SS_NOTIFY */, x, y + 5, w - bw - rw - 6, 18, g_font);
            if (rw) g_ffr[i] = make(ID_FF_RANGE + i, ST, wide(lim, wl, 32), 0x2 /* SS_RIGHT */ | 0x100, x + w - bw - rw - 4, y + 6, rw - 4, 16, g_font);
            if (f->kind == FF_CHOICE) { /* drawn by hand (Windows' drop-downs stay white in the dark theme) */
                g_ffc[i] = make(ID_FF_CTL + i, B, WTEXT(""), OD, x + w - bw, y + 1, bw, 24, g_font);
            } else {
                g_ffc[i] = framed(ID_FF_CTL + i, E, WS_TABSTOP | ES_AUTOHSCROLL, x + w - bw, y + 1, bw, 24, 7, 4);
                SendMessageW(g_ffc[i], EM_SETLIMITTEXT, 10, 0);
            }
            tip(g_ffl[i], f->help);
            tip(g_ffr[i], f->help);
        }
        tip(g_ffc[i], f->help);
    }
    ctl(ID_FF_FOOT, ST, WTEXT("Fusion Fix is by ThirteenAG. Changes count from the next start of Blacklist."), ELL, CX, FOOT_Y + 9, CW - 260, 18, g_font);
    ctl(ID_FF_DEFAULTS, B, WTEXT("Defaults"), OD, CX + CW - 236, FOOT_Y, 112, 34, g_font);
    ctl(ID_FF_SAVE, B, WTEXT("SAVE"), OD, CX + CW - 116, FOOT_Y, 116, 34, g_bold);
}

/* ---------- which controls show ---------- */

static int page_of(int id)
{
    if (is_nav((UINT)id) || id == ID_DEVSOLO || id == ID_DEVTWO) return -1;
    if (id == ID_LOG || id == ID_COPYLOG) return TAB_LOG;
    if (id == ID_HOWTO) return TAB_HOWTO;
    if (id >= ID_SETTAB && id <= ID_FF_HINT) return TAB_SET;
    return TAB_PLAY;
}
static bool ff_ready(void) { return g_shown.ff_ok && fixed(); }
static bool ctl_wanted(int id)
{
    int pg = page_of(id);
    if (pg >= 0 && pg != g_tab) return false;
    const UiState *s = &g_shown;
    if (pg == TAB_SET) {
        if (id == ID_FF_LOCKED) return !ff_ready();
        if (!ff_ready()) return false;
        if (id == ID_FF_SWITCH || id == ID_FF_NOTE) return g_settab == 1;
        return true;
    }
    if (id == ID_ACTION) return s->mode != MODE_IDLE;
    if (id == ID_REFRESH) return s->mode == MODE_JOIN;
    return true;
}
/* by the control's own WS_VISIBLE: IsWindowVisible says "no" for every control until the window itself is shown */
static void show_ctl(HWND h, bool want)
{
    if (h && want != ((GetWindowLongW(h, -16 /* GWL_STYLE */) & WS_VISIBLE) != 0)) ShowWindow(h, want ? 5 : 0);
}
static void apply_visibility(void)
{
    for (int i = ID_HOST; i < ID_LAST; i++) show_ctl(g_c[i], ctl_wanted(i));
    bool set = g_tab == TAB_SET && ff_ready();
    for (int i = 0; i < FF_NSETTINGS && i < 64; i++) {
        bool want = set && FF_SETTINGS[i].shared == (g_settab == 1);
        show_ctl(g_ffc[i], want);
        show_ctl(g_ffl[i], want);
        show_ctl(g_ffr[i], want);
    }
}

static void append_log(const char *utf8)
{
    static WCHAR w[70000];
    wide(utf8, w, 70000);
    HWND h = g_c[ID_LOG];
    int len = (int)SendMessageW(h, WM_GETTEXTLENGTH, 0, 0);
    if (len > 400000) {
        SendMessageW(h, EM_SETSEL, 0, 200000);
        SendMessageW(h, EM_REPLACESEL, 0, (LPARAM)WTEXT(""));
        len = (int)SendMessageW(h, WM_GETTEXTLENGTH, 0, 0);
    }
    SendMessageW(h, EM_SETSEL, (WPARAM)len, len);
    SendMessageW(h, EM_REPLACESEL, 0, (LPARAM)w);
    SendMessageW(h, EM_SCROLLCARET, 0, 0);
}

static void show_list(int which)
{
    HWND lb = g_c[ID_LIST];
    SendMessageW(lb, LB_RESETCONTENT, 0, 0);
    static WCHAR w[UI_NAME + 8];
    for (int i = 0; i < g_nview[which]; i++)
        SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)wide(g_view[which][i].name, w, UI_NAME + 8));
    if (g_nview[which] == 1) SendMessageW(lb, LB_SETCURSEL, 0, 0);
    g_list_shown = which;
}

/* ---------- "How to": the next step, ticked off as it's done ---------- */

static const char *mark(bool done, bool *open)
{
    if (done) return "[x] ";
    if (*open) return "[ ] ";
    *open = true;
    return "=> ";
}

static void build_howto(const UiState *s, char *o, int n)
{
    bool open = false;
    int k = 0;
#define ADD(...) do { if (k < n) { sfmt(o + k, (size_t)(n - k), __VA_ARGS__); k += (int)strlen(o + k); } } while (0)
    if (!s->game_found) {
        ADD("! Splinter Cell: Blacklist was not found through Steam.\r\n");
        ADD("Install it from Steam, start it once until the main menu, then open SCBLLink again.\r\n\r\n");
    } else if (s->fix_old) {
        ADD("=> First click \"Update SCBL Fix\" (Play page): the game folder has the game DLL of another SCBLLink version.\r\n");
        ADD("   Everything else stays locked until then, so the game always gets the DLL made for this SCBLLink.\r\n\r\n");
    } else if (!s->dll_ok) {
        ADD("=> First click \"Install SCBL Fix\" (Play page; once, and again after a Steam \"Verify integrity\").\r\n");
        ADD("   It puts SCBLLink's game DLL in the game folder and installs Fusion Fix.\r\n");
        ADD("   Everything else stays locked until then.\r\n\r\n");
    } else if (!s->fix_ok) {
        ADD("Fusion Fix isn't in yet: click \"Install SCBL Fix\" again to get it (see the log).\r\n\r\n");
    }
    if (s->mode == MODE_IDLE) {
        ADD("HOSTING (the one whose PC runs the game's server):\r\n");
        ADD("  1. Pick \"Host a game\" and click the green button next to it.\r\n");
        ADD("  2. Wait for \"Ready\", then click \"Start Blacklist\".\r\n");
        ADD("  3. Your friend picks \"Join a friend\" in their SCBLLink.\r\n\r\n");
        ADD("JOINING A FRIEND:\r\n");
        ADD("  1. Pick \"Join a friend\" and click the green button, then pick the friend who hosts and click Join\r\n");
        ADD("     (or paste their lobby code and click \"Join this code\").\r\n");
        ADD("  2. Wait for \"Ready\", then click \"Start Blacklist\".\r\n\r\n");
        ADD("Don't start the game before SCBLLink says Ready: the game looks for its server only once, when it starts.\r\n\r\n");
        ADD("Only your Steam friends can join, always through Valve's relay: nobody sees anyone's IP address.\r\n");
        ADD("The game's options, and SCBLLink's own (balaclava, crosshair, motion blur), are on the Settings page.\r\n");
    } else if (s->mode == MODE_HOST) {
        ADD("%sHost: your lobby is open%s\r\n", mark(s->lobby != 0, &open), s->lobby ? "" : " (creating...)");
        ADD("%sThe game's server answers (Ready)\r\n", mark(s->ready, &open));
        ADD("%sStart Blacklist (click \"Start Blacklist\")\r\n", mark(s->game_running, &open));
        ADD("%sYour friend links to you (they pick \"Join a friend\")\r\n", mark(s->linked, &open));
        ADD("\r\nIn the game: Online, then invite your friend from the party (or use the F5 overlay).\r\n");
        ADD("If your friend doesn't find you, click \"Copy code\" and send them the lobby code in Steam chat.\r\n");
    } else {
        ADD("%sFind your friend's lobby\r\n", mark(s->lobby != 0, &open));
        ADD("%sLink to them through Steam\r\n", mark(s->linked, &open));
        ADD("%sTheir game's server answers (Ready)\r\n", mark(s->ready, &open));
        ADD("%sStart Blacklist (click \"Start Blacklist\")\r\n", mark(s->game_running, &open));
        ADD("\r\nIn the game: Online. Accept your friend's invite (F5 overlay) or join their party.\r\n");
    }
#undef ADD
}

static void update_howto(void)
{
    static char last[2048];
    char now[2048];
    build_howto(&g_shown, now, sizeof now);
    if (strcmp(now, last) == 0) return;
    memcpy(last, now, sizeof last);
    set_text(ID_HOWTO, now);
}

/* ---------- the pages' drawn parts ---------- */

static void draw_play(HDC dc)
{
    rbox(dc, CX, FIX_Y, CW, FIX_H, 12, banner_fill(), banner_edge(), T.bg);
    card(dc, CX, LIST_Y, CW, LIST_H);
    label(dc, WTEXT("PLAYERS"), CX + 16, LIST_Y + 14, 80, 20, g_bold, T.text, DT_VCENTER);
    card(dc, CX, STAT_Y, CW, STAT_H);
    const UiState *s = &g_shown;
    DWORD c = !fixed() ? red_text() : s->ready ? T.status : s->status[0] == '!' ? red_text() : s->mode == MODE_IDLE ? T.dim : T.status;
    dot(dc, S_(CX + 16), S_(STAT_Y + 19), S_(10), c, T.panel);
}

static void draw_settings(HDC dc)
{
    if (!ff_ready()) return;
    if (g_settab == 1) {
        card(dc, CX, LOCK_Y, CW, LOCK_H);
        label(dc, WTEXT("THE SAME FOR EVERYONE WHO PLAYS TOGETHER"), CX + 16, LOCK_Y + 12, CW - 300, 20, g_cap, T.status, DT_VCENTER);
        rbox(dc, CX + 14, LOCK_Y + 40, CW - 28, 36, 8, warn_fill(), warn_edge(), T.panel);
    }
    for (int g = 0; g < G_N; g++) {
        if (group_shared(g) != (g_settab == 1)) continue;
        RECT r = g_grect[g];
        card(dc, r.left, r.top, r.right - r.left, r.bottom - r.top);
        WCHAR w[32];
        label(dc, wide(G_TITLE[g], w, 32), r.left + 14, r.top + 10, r.right - r.left - 28, 18, g_cap, T.status, DT_VCENTER);
    }
}

/* the whole window's background: the sidebar with the logo, then the page */
static void draw_page(HDC dc)
{
    RECT r;
    GetClientRect(g_wnd, &r);
    FillRect(dc, &r, g_br_bg);
    RECT side = { 0, 0, S_(SIDE_W), r.bottom };
    FillRect(dc, &side, g_br_side);
    RECT line = { S_(SIDE_W) - 1, 0, S_(SIDE_W), r.bottom };
    FillRect(dc, &line, g_br_border);
    draw_logo(dc, 16, 18, 40);
    label(dc, WTEXT("SCBLLINK"), 64, 18, SIDE_W - 70, 40, g_brand, T.text, DT_VCENTER);
    for (int i = 0; i < 3; i++) dot(dc, S_(66 + i * 11), S_(64), S_(6), T.accent, T.side); /* the goggles' lights */
#ifdef SCBL_DEV
    label(dc, WTEXT("DEVELOPER"), 16, 224, SIDE_W - 32, 18, g_cap, T.dim, DT_VCENTER);
#endif
    label(dc, WTEXT("v" SCBLLINK_VERSION), 16, WIN_H - 116, SIDE_W - 32, 18, g_font, T.dim, DT_VCENTER);
    switch (g_tab) {
    case TAB_PLAY: draw_play(dc); break;
    case TAB_SET: draw_settings(dc); break;
    default:
        label(dc, g_tab == TAB_LOG ? WTEXT("LOG") : WTEXT("HOW TO PLAY"), CX + 2, 16, 300, 30, g_big, T.text, DT_VCENTER);
        card(dc, CX, 60, CW, WIN_H - 60 - 20);
        break;
    }
    draw_frames(dc);
}

/* ---------- the game settings: values in, values out ---------- */

static void sw_set(int slot, bool on)
{
    g_swon[slot] = on;
    HWND h = slot == 64 ? g_c[ID_FF_SWITCH] : g_ffc[slot];
    if (h) RedrawWindow(h, 0, 0, 0x1 | 0x4);
}

static void ff_show(int i, const char *v)
{
    const FfSetting *f = &FF_SETTINGS[i];
    HWND c = g_ffc[i];
    if (f->kind == FF_BOOL) sw_set(i, v[0] == '1');
    else if (f->kind == FF_CHOICE) {
        int sel = 0, k = 0;
        const char *p = f->choices;
        char num[8];
        while (p) {
            const char *e = strchr(p, '|');
            int l = e ? (int)(e - p) : (int)strlen(p);
            sfmt(num, sizeof num, "%d", k);
            if (f->numbered ? !strcmp(v, num) : ((int)strlen(v) == l && !strncmp(v, p, (size_t)l))) sel = k;
            p = e ? e + 1 : 0;
            k++;
        }
        g_choice[i] = sel;
        if (c) RedrawWindow(c, 0, 0, 0x1 | 0x4);
    } else {
        WCHAR w[FF_VAL];
        SetWindowTextW(c, wide(v, w, FF_VAL));
    }
}

static void ff_fill(bool defaults)
{
    int n = 0;
    char *ini = defaults || !g_shown.ff_ini[0] ? 0 : file_read_all(g_shown.ff_ini, &n);
    char *vid = defaults || !g_shown.video_ini[0] ? 0 : file_read_all(g_shown.video_ini, &n); /* motion blur: the game's own file */
    for (int i = 0; i < FF_NSETTINGS && i < 64; i++) {
        char raw[64], v[FF_VAL];
        const char *from = FF_SETTINGS[i].video ? vid : ini;
        if (!from || !ff_ini_get(from, FF_SETTINGS[i].sec, FF_SETTINGS[i].key, raw, sizeof raw) || !ff_normalize(i, raw, v, sizeof v))
            ff_normalize(i, FF_SETTINGS[i].def, v, sizeof v);
        ff_show(i, v);
    }
    free(ini);
    free(vid);
}

static void ff_fill_shared_from_file(void)
{
    int n = 0;
    char *ini = g_shown.ff_ini[0] ? file_read_all(g_shown.ff_ini, &n) : 0;
    for (int i = 0; ini && i < FF_NSETTINGS && i < 64; i++) {
        char raw[64], v[FF_VAL];
        if (!FF_SETTINGS[i].shared) continue;
        if (!ff_ini_get(ini, FF_SETTINGS[i].sec, FF_SETTINGS[i].key, raw, sizeof raw) || !ff_normalize(i, raw, v, sizeof v)) continue;
        ff_show(i, v);
    }
    free(ini);
}

/* the page's values, checked; false (and a message) when one isn't valid */
static bool ff_collect(void)
{
    uint8_t buf[2048];
    int k = 0;
    for (int i = 0; i < FF_NSETTINGS && i < 64; i++) {
        const FfSetting *f = &FF_SETTINGS[i];
        char in[FF_VAL] = "", v[FF_VAL];
        if (f->kind == FF_BOOL) sfmt(in, sizeof in, "%d", g_swon[i] ? 1 : 0);
        else if (f->kind == FF_CHOICE) {
            int sel = g_choice[i];
            if (f->numbered) sfmt(in, sizeof in, "%d", sel);
            else {
                const char *p = f->choices;
                for (int k2 = 0; p && k2 < sel; k2++) { p = strchr(p, '|'); if (p) p++; }
                if (p) { const char *e = strchr(p, '|'); int l = e ? (int)(e - p) : (int)strlen(p); if (l < FF_VAL) { memcpy(in, p, (size_t)l); in[l] = 0; } }
            }
        } else {
            WCHAR w[FF_VAL];
            GetWindowTextW(g_ffc[i], w, FF_VAL);
            WideCharToMultiByte(CP_UTF8, 0, w, -1, in, FF_VAL, 0, 0);
        }
        if (!ff_normalize(i, in, v, sizeof v)) {
            char q[256];
            static WCHAR wq[256];
            if (f->kind == FF_INT && f->zero_ok) sfmt(q, sizeof q, "\"%s\": use 0, or a whole number from %d to %d.", f->label, (int)f->min, (int)f->max);
            else if (f->kind == FF_INT) sfmt(q, sizeof q, "\"%s\": use a whole number from %d to %d.", f->label, (int)f->min, (int)f->max);
            else {
                int a = (int)(f->min * 100 + 0.5), b = (int)(f->max * 100 + 0.5);
                sfmt(q, sizeof q, "\"%s\": use a number from %d.%02d to %d.%02d (like 1.25).", f->label, a / 100, a % 100, b / 100, b % 100);
            }
            if (g_settab != (f->shared ? 1 : 0)) { g_settab = f->shared ? 1 : 0; apply_visibility(); RedrawWindow(g_wnd, 0, 0, 0x1 | 0x4 | 0x80); }
            MessageBoxW(g_wnd, wide(q, wq, 256), WTEXT("Game settings"), MB_ICONWARNING);
            SetFocus(g_ffc[i]);
            return false;
        }
        int l = (int)strlen(v);
        if (k + 2 + l > (int)sizeof buf) return false;
        buf[k++] = (uint8_t)i;
        buf[k++] = (uint8_t)l;
        memcpy(buf + k, v, (size_t)l);
        k += l;
    }
    EnterCriticalSection(&g_cs);
    memcpy(g_ff_buf, buf, (size_t)k);
    g_ff_n = k;
    LeaveCriticalSection(&g_cs);
    return true;
}

/* the co-op settings: locked until the switch is on; always while playing with a friend's (joined) */
static void ff_lock_shared(void)
{
    bool joined = g_shown.ff_host_rules;
    if (joined) g_ff_unlocked = false;
    sw_set(64, g_ff_unlocked);
    EnableWindow(g_c[ID_FF_SWITCH], !joined);
    for (int i = 0; i < FF_NSETTINGS && i < 64; i++)
        if (FF_SETTINGS[i].shared) {
            EnableWindow(g_ffc[i], g_ff_unlocked && !joined);
            if (g_ffl[i]) RedrawWindow(g_ffl[i], 0, 0, 0x1 | 0x4);
            if (g_ffr[i]) RedrawWindow(g_ffr[i], 0, 0, 0x1 | 0x4);
        }
    SetWindowTextW(g_c[ID_FF_NOTE],
                   joined ? WTEXT("You joined a friend: you both play with their co-op settings, so they're locked here. Yours come back when you host.")
                          : WTEXT("You and your friend must have the same co-op settings. Don't copy them by hand: when you host, "
                                  "SCBLLink sends yours to your friends."));
}

/* "What every option does": every option and what it does, in one window */
static HWND g_help, g_help_text;

static LRESULT WINAPI help_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_ERASEBKGND: {
        RECT r;
        GetClientRect(w, &r);
        FillRect((HDC)wp, &r, g_br_panel);
        return 1;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, T.text);
        SetBkColor((HDC)wp, T.panel);
        return (LRESULT)g_br_panel;
    case WM_CLOSE: DestroyWindow(w); return 0;
    case WM_DESTROY: if (w == g_help) g_help = g_help_text = 0; return 0;
    default: return DefWindowProcW(w, m, wp, lp);
    }
}

static void help_open(void)
{
    if (g_help) { ShowWindow(g_help, 5); return; }
    static bool registered;
    if (!registered) {
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof wc);
        wc.cbSize = sizeof wc;
        wc.proc = help_proc;
        wc.inst = GetModuleHandleW(0);
        wc.icon = LoadIconW(wc.inst, (const WCHAR *)(uintptr_t)1);
        wc.iconSm = wc.icon;
        wc.cursor = LoadCursorW(0, (const WCHAR *)(uintptr_t)32512);
        wc.className = WTEXT("SCBLLinkHelp");
        RegisterClassExW(&wc);
        registered = true;
    }
    static char t[16000];
    int k = 0;
#define ADD(...) do { if (k < (int)sizeof t) { sfmt(t + k, sizeof t - (size_t)k, __VA_ARGS__); k += (int)strlen(t + k); } } while (0)
    for (int pass = 0; pass < 2; pass++) {
        ADD("%s", pass == 0 ? "YOUR GAME (only you: each player picks their own)\r\n\r\n"
                            : "\r\nCO-OP (the same for everyone who plays together: the host's are sent to the friends)\r\n\r\n");
        for (int i = 0; i < FF_NSETTINGS; i++) {
            const FfSetting *f = &FF_SETTINGS[i];
            if (f->shared != (pass == 1)) continue;
            char lim[48] = "";
            if (f->kind == FF_FLOAT) {
                int a = (int)(f->min * 100 + 0.5), b = (int)(f->max * 100 + 0.5);
                sfmt(lim, sizeof lim, " (%d.%02d to %d.%02d)", a / 100, a % 100, b / 100, b % 100);
            } else if (f->kind == FF_INT && f->zero_ok) sfmt(lim, sizeof lim, " (0 = off, %d to %d)", (int)f->min, (int)f->max);
            else if (f->kind == FF_INT) sfmt(lim, sizeof lim, " (%d to %d)", (int)f->min, (int)f->max);
            else if (f->kind == FF_CHOICE) {
                char c[48];
                sfmt(c, sizeof c, "%s", f->choices);
                for (char *q = c; *q; q++) if (*q == '|') *q = '/';
                sfmt(lim, sizeof lim, " (%s)", c);
            } else sfmt(lim, sizeof lim, " (on/off)");
            ADD("%s%s\r\n    %s\r\n\r\n", f->label, lim, f->help);
        }
    }
    ADD("Changes count from the next start of Blacklist. Fusion Fix is by ThirteenAG.");
#undef ADD
    RECT pr;
    GetWindowRect(g_wnd, &pr);
    DWORD style = WS_CAPTION | WS_SYSMENU;
    RECT r = { 0, 0, S_(560), S_(520) };
    AdjustWindowRectEx(&r, style, 0, 0);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    int x = pr.right - ww - S_(20), y = pr.top + S_(40);
    if (x < 0) x = 0;
    g_help = CreateWindowExW(0, WTEXT("SCBLLinkHelp"), WTEXT("What the game settings do"), style, x, y, ww, wh, g_wnd, 0, GetModuleHandleW(0), 0);
    if (!g_help) return;
    g_help_text = CreateWindowExW(0, WTEXT("EDIT"), WTEXT(""), WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                  S_(14), S_(10), S_(560 - 20), S_(520 - 20), g_help, (HMENU)(uintptr_t)1, GetModuleHandleW(0), 0);
    SendMessageW(g_help_text, WM_SETFONT, (WPARAM)g_font, 1);
    static WCHAR wt[16000];
    SetWindowTextW(g_help_text, wide(t, wt, 16000));
    typedef long(WINAPI * SetTheme)(HWND, const WCHAR *, const WCHAR *);
    SetTheme theme = (SetTheme)sys_proc("uxtheme.dll", "SetWindowTheme");
    if (theme) theme(g_help_text, T.dark ? WTEXT("DarkMode_Explorer") : WTEXT("Explorer"), 0);
    typedef long(WINAPI * DwmSet)(HWND, DWORD, const void *, DWORD);
    DwmSet dwm = (DwmSet)sys_proc("dwmapi.dll", "DwmSetWindowAttribute");
    BOOL on = T.dark;
    if (dwm && dwm(g_help, 20, &on, sizeof on) != 0) dwm(g_help, 19, &on, sizeof on);
    ShowWindow(g_help, 5);
}

/* ---------- pages ---------- */

static void show_tab(int tab)
{
    if (tab == TAB_SET && g_tab != TAB_SET && ff_ready()) { /* fresh from the files each time */
        g_ff_unlocked = false;
        ff_fill(false);
        ff_lock_shared();
    }
    g_tab = tab;
    if (tab == TAB_LOG) g_log_unseen = false;
    apply_visibility();
    RedrawWindow(g_wnd, 0, 0, 0x1 | 0x4 | 0x80 /* invalidate, erase, children */);
}

static void copy_log(void)
{
    HWND h = g_c[ID_LOG];
    int len = (int)SendMessageW(h, WM_GETTEXTLENGTH, 0, 0);
    if (len <= 0) return;
    WCHAR *w = malloc(((size_t)len + 1) * 2);
    char *a = malloc((size_t)len * 3 + 1);
    if (w && a) {
        GetWindowTextW(h, w, len + 1);
        WideCharToMultiByte(CP_UTF8, 0, w, -1, a, len * 3 + 1, 0, 0);
        clipboard_set(a);
    }
    free(w);
    free(a);
}

static void refresh_controls(void)
{
    const UiState *s = &g_shown;
    bool idle = s->mode == MODE_IDLE, host = s->mode == MODE_HOST, join = s->mode == MODE_JOIN;
    /* nothing but "Install SCBL Fix" until the fix (our game DLL, as shipped next to this
       SCBLLink.exe) is in the game folder: no hosting or joining with a missing or old DLL */
    bool fx = fixed();
    /* the game has to start after SCBLLink is ready: while it runs, no new link */
    EnableWindow(g_c[ID_HOST], fx && idle && !g_closing && !s->game_running);
    EnableWindow(g_c[ID_JOIN], !g_closing && (idle ? fx && !s->game_running : true));
    {
        static int shown = -1;
        int now = idle ? g_role : 2;
        if (now != shown) {
            SetWindowTextW(g_c[ID_JOIN], now == 2 ? WTEXT("STOP") : now ? WTEXT("JOIN A FRIEND") : WTEXT("HOST A GAME"));
            RedrawWindow(g_c[ID_JOIN], 0, 0, 0x1 | 0x4);
            RedrawWindow(g_c[ID_HOST], 0, 0, 0x1 | 0x4);
            shown = now;
        }
    }
    EnableWindow(g_c[ID_LOBBY], fx || !idle);
    EnableWindow(g_c[ID_NAME], fx);
    static int ro = -1;
    int want_ro = host || (join && s->lobby);
    if (want_ro != ro) { SendMessageW(g_c[ID_LOBBY], EM_SETREADONLY, (WPARAM)want_ro, 0); ro = want_ro; }
    EnableWindow(g_c[ID_COPYID], host && s->lobby);
    EnableWindow(g_c[ID_JOINID], fx && (idle || join) && !s->lobby && !g_closing);
    int which = host ? LIST_LINKED : LIST_HOSTING;
    if (which != g_list_shown) show_list(which);
    set_text_once(ID_LISTLABEL, idle ? "Host or join to see your friends here" : host ? "Friends linked to your game right now" : "Friends hosting: pick one and click Join");
    set_text_once(ID_ACTION, host ? "Kick" : "Join");
    EnableWindow(g_c[ID_ACTION], !g_closing && ((join && !s->lobby && g_nview[LIST_HOSTING] > 0) || (host && g_nview[LIST_LINKED] > 0)));
    EnableWindow(g_c[ID_REFRESH], !g_closing && join && !s->lobby);
    EnableWindow(g_c[ID_LAUNCH], !g_closing && s->ready && !s->game_running);
    {
        /* until the in-game overlay: an invitation turns "Test the link" into "Join <name>",
           and with SCBLLink's own game DLL linked, "Details" into "Refresh friends" */
        static char shown_inv[20] = "-";
        static int shown_ref = -1;
        if (strcmp(shown_inv, s->invite_from)) {
            WCHAR w[40];
            char t[40];
            if (s->invite_from[0]) sfmt(t, sizeof t, "Join %s", s->invite_from);
            else sfmt(t, sizeof t, "Test the link");
            SetWindowTextW(g_c[ID_TEST], wide(t, w, 40));
            sfmt(shown_inv, sizeof shown_inv, "%s", s->invite_from);
        }
        if (shown_ref != (int)s->game_linked) {
            SetWindowTextW(g_c[ID_DETAILS], s->game_linked ? WTEXT("Refresh friends") : WTEXT("Details"));
            shown_ref = s->game_linked;
        }
    }
#ifdef SCBL_DEV
    {
        static int shown_solo = -1, shown_two = -1;
        if (shown_solo != (int)s->dev_solo) {
            SetWindowTextW(g_c[ID_DEVSOLO], s->dev_solo ? WTEXT("Record alone: ON") : WTEXT("Record alone: off"));
            RedrawWindow(g_c[ID_DEVSOLO], 0, 0, 0x1 | 0x4);
            shown_solo = s->dev_solo;
        }
        if (shown_two != (int)s->dev_two) {
            SetWindowTextW(g_c[ID_DEVTWO], s->dev_two ? WTEXT("2nd game: ON") : WTEXT("2nd game: off"));
            RedrawWindow(g_c[ID_DEVTWO], 0, 0, 0x1 | 0x4);
            shown_two = s->dev_two;
        }
        EnableWindow(g_c[ID_DEVSOLO], fx && idle && !g_closing);
        EnableWindow(g_c[ID_DEVTWO], fx && idle && !g_closing);
    }
#endif
    EnableWindow(g_c[ID_TEST], !g_closing && (s->my_ip[0] || s->invite_from[0]));
    EnableWindow(g_c[ID_DETAILS], !idle);
    EnableWindow(g_c[ID_SAVENAME], fx && !g_closing);
    /* the SCBL Fix card */
    const char *title, *sub;
    if (!s->game_found) { title = "BLACKLIST WASN'T FOUND"; sub = "Install it from Steam and start it once to the menu, then open SCBLLink again"; }
    else if (s->fix_old) { title = "THE SCBL FIX IS FROM ANOTHER VERSION"; sub = "Click Update: everything else stays locked until then"; }
    else if (!s->dll_ok) { title = "THE SCBL FIX ISN'T INSTALLED"; sub = "Click Install: everything else stays locked until then"; }
    else if (!s->fix_ok) { title = "FUSION FIX IS MISSING"; sub = "Click Install SCBL Fix again to get it (see the log)"; }
    else { title = "SCBL FIX INSTALLED"; sub = "SCBLLink's game DLL and Fusion Fix are in the game"; }
    {
        static const char *shown_title;
        if (shown_title != title) {
            set_text(ID_FIXTITLE, title);
            set_text(ID_FIXSUB, sub);
            shown_title = title;
            if (g_tab == TAB_PLAY) RedrawWindow(g_wnd, 0, 0, 0x1 | 0x4 | 0x80);
        }
    }
    set_text_once(ID_FIX, s->fix_ok ? "Remove" : s->fix_old ? "Update SCBL Fix" : "Install SCBL Fix");
    EnableWindow(g_c[ID_FIX], idle && !g_closing && s->game_found && !s->game_running);
    /* Settings: Fusion Fix's file there, and the host's co-op settings while joined */
    {
        static int ready = -1;
        if (ready != (int)ff_ready()) {
            ready = ff_ready();
            set_text(ID_FF_LOCKED, !fixed() ? "Install the SCBL Fix first (Play page): the game settings come with it."
                                            : "Fusion Fix isn't installed: click \"Install SCBL Fix\" on the Play page to get it.");
            if (ready && g_tab == TAB_SET) { g_ff_unlocked = false; ff_fill(false); ff_lock_shared(); }
            apply_visibility();
            if (g_tab == TAB_SET) RedrawWindow(g_wnd, 0, 0, 0x1 | 0x4 | 0x80);
        }
        static int locked = -1;
        if (locked != (int)s->ff_host_rules) { locked = s->ff_host_rules; if (ff_ready()) ff_lock_shared(); }
        EnableWindow(g_c[ID_FF_SAVE], ff_ready() && !g_closing);
    }
}

/* which half of a two-part button was clicked: 0 left, 1 right */
static int clicked_half(int id)
{
    POINT p;
    RECT r;
    if (!GetCursorPos(&p) || !ScreenToClient(g_c[id], &p) || !GetClientRect(g_c[id], &r)) return -1;
    return p.x < (r.right - r.left) / 2 ? 0 : 1;
}

static void on_command(int id, int code)
{
    /* the game settings' switches */
    if (id >= ID_FF_CTL && id < ID_FF_CTL + 64) {
        int i = id - ID_FF_CTL;
        if (i >= FF_NSETTINGS || (code != 0 && code != 5 /* clicked, double-clicked */)) return;
        if (FF_SETTINGS[i].kind == FF_BOOL) sw_set(i, !g_swon[i]);
        else if (FF_SETTINGS[i].kind == FF_CHOICE) {
            char t[32];
            int n = choice_text(i, 0, t, sizeof t);
            if (n > 0) g_choice[i] = (g_choice[i] + 1) % n;
            RedrawWindow(g_ffc[i], 0, 0, 0x1 | 0x4);
        }
        return;
    }
    switch (id) {
    case ID_HOST: { /* Host | Join: pick one */
        int h = clicked_half(ID_HOST);
        if (h >= 0 && g_shown.mode == MODE_IDLE) { g_role = h; refresh_controls(); }
        break;
    }
    case ID_JOIN: /* start what's picked; Stop while running */
        if (g_shown.mode != MODE_IDLE) push_cmd(CMD_STOP, 0);
        else if (g_role == 0) { push_cmd(CMD_HOST, 0); show_tab(TAB_LOG); } /* the setup steps show in the log */
        else push_cmd(CMD_JOIN, 0);
        break;
    case ID_COPYID:
        if (g_shown.lobby) {
            char c[16];
            lobby_code(g_shown.lobby, c);
            clipboard_set(c);
            ui_log("Lobby code copied. Send it to your friend (Steam chat).");
        }
        break;
    case ID_JOINID: {
        WCHAR w[64];
        char a[64];
        GetWindowTextW(g_c[ID_LOBBY], w, 64);
        WideCharToMultiByte(CP_UTF8, 0, w, -1, a, 64, 0, 0);
        u64 lob = lobby_parse(a);
        if (!lob) {
            MessageBoxW(g_wnd, WTEXT("Paste the lobby code your friend sent you (like 1PCW-4HB) into the Lobby code box first."),
                        WTEXT("SCBLLink"), MB_ICONWARNING);
            break;
        }
        push_cmd(CMD_JOIN_LOBBY, lob);
        break;
    }
    case ID_LIST:
        if (code != LBN_DBLCLK) break;
        /* fall through: double-click = the button (never for Kick) */
    case ID_ACTION: {
        int sel = (int)SendMessageW(g_c[ID_LIST], LB_GETCURSEL, 0, 0);
        if (g_shown.mode == MODE_HOST) {
            if (code == LBN_DBLCLK) break;
            if (sel < 0 || sel >= g_nview[LIST_LINKED]) {
                MessageBoxW(g_wnd, WTEXT("Pick a friend in the list first."), WTEXT("SCBLLink"), MB_ICONWARNING);
                break;
            }
            UiPerson *p = &g_view[LIST_LINKED][sel];
            char q[256];
            static WCHAR wq[256];
            sfmt(q, sizeof q, "Kick %s?\n\nThey are cut off and can't link to your game again until you click Stop.", p->name);
            if (MessageBoxW(g_wnd, wide(q, wq, 256), WTEXT("SCBLLink"), MB_YESNO | MB_ICONQUESTION) == IDYES)
                push_cmd(CMD_KICK, p->id);
            break;
        }
        if (sel < 0 || sel >= g_nview[LIST_HOSTING]) {
            MessageBoxW(g_wnd, WTEXT("Pick a friend in the list first."), WTEXT("SCBLLink"), MB_ICONWARNING);
            break;
        }
        push_cmd(CMD_JOIN_LOBBY, g_view[LIST_HOSTING][sel].lobby);
        break;
    }
    case 1: /* Enter: join the pasted code */
        if (g_tab == TAB_PLAY && fixed() && g_shown.mode != MODE_HOST && !g_shown.lobby && SendMessageW(g_c[ID_LOBBY], WM_GETTEXTLENGTH, 0, 0) > 0)
            on_command(ID_JOINID, 0);
        break;
    case ID_LAUNCH: push_cmd(CMD_LAUNCH, 0); break;
    case ID_REFRESH: push_cmd(CMD_JOIN, 0); break;
    case ID_TEST:
        if (g_shown.invite_from[0]) push_cmd(CMD_ACCEPT, 0);
        else { push_cmd(CMD_TEST, 0); show_tab(TAB_LOG); }
        break;
    case ID_FIX:
        if (g_shown.fix_ok) {
            if (MessageBoxW(g_wnd, WTEXT("Remove the SCBL Fix?\n\nThe game gets its own uplay_r1_loader.dll back, and Fusion Fix is removed. Online play with SCBLLink needs the fix."),
                            WTEXT("SCBLLink"), MB_YESNO | MB_ICONQUESTION) == IDYES)
                push_cmd(CMD_FIX_REMOVE, 0);
        } else if (g_shown.fix_old) {
            if (MessageBoxW(g_wnd, WTEXT("Update the SCBL Fix?\n\nThe game folder has the game DLL of another SCBLLink version. "
                                         "SCBLLink puts in the one that came with this version (scbllink-game.dll). The game's own DLL stays kept."),
                            WTEXT("SCBLLink"), MB_YESNO | MB_ICONQUESTION) == IDYES) {
                push_cmd(CMD_FIX_INSTALL, 0);
                show_tab(TAB_LOG);
            }
        } else {
            if (MessageBoxW(g_wnd, WTEXT("Install the SCBL Fix?\n\nSCBLLink puts its game DLL (scbllink-game.dll) in the game folder as "
                                         "uplay_r1_loader.dll: it takes the game to the host's server over SCBLLink. The game's own DLL is kept, and Remove puts it back.\n\n"
                                         "It also installs ThirteenAG's Fusion Fix from its GitHub page (github.com/ThirteenAG/WidescreenFixesPack): "
                                         "skips the intro, wide screens, unlocks the DLC and the 4th Echelon missions. It is downloaded and checked (SHA-256). Change it on the Settings page."),
                            WTEXT("SCBLLink"), MB_YESNO | MB_ICONQUESTION) == IDYES) {
                push_cmd(CMD_FIX_INSTALL, 0);
                show_tab(TAB_LOG);
            }
        }
        break;
    case ID_DETAILS:
        if (g_shown.game_linked) push_cmd(CMD_REFRESH, 0);
        else { push_cmd(CMD_DETAILS, 0); show_tab(TAB_LOG); }
        break;
    case ID_SAVENAME: {
        WCHAR w[32];
        char a[64], c[NAME_MAX_LEN + 1];
        GetWindowTextW(g_c[ID_NAME], w, 32);
        WideCharToMultiByte(CP_UTF8, 0, w, -1, a, 64, 0, 0);
        name_clean(a, c);
        if (!name_ok(c)) {
            MessageBoxW(g_wnd, WTEXT("Names are 3 to 16 characters: English letters, digits, _ - and . (no spaces)."), WTEXT("SCBLLink"), MB_ICONWARNING);
            break;
        }
        name_save(c);
        set_text(ID_NAME, c);
        push_cmd(CMD_SET_NAME, 0);
        break;
    }
    /* Settings */
    case ID_SETTAB: {
        int h = clicked_half(ID_SETTAB);
        if (h >= 0 && h != g_settab) { g_settab = h; apply_visibility(); RedrawWindow(g_wnd, 0, 0, 0x1 | 0x4 | 0x80); }
        break;
    }
    case ID_FF_SWITCH:
        if (code != 0 && code != 5) break;
        g_ff_unlocked = !g_ff_unlocked;
        if (!g_ff_unlocked) ff_fill_shared_from_file(); /* switched off: back to the saved ones */
        ff_lock_shared();
        break;
    case ID_FF_INFO: help_open(); break;
    case ID_FF_DEFAULTS:
        ff_fill(true);
        if (!g_ff_unlocked) ff_fill_shared_from_file(); /* locked ones stay as they are */
        break;
    case ID_FF_SAVE:
        if (ff_collect()) {
            push_cmd(CMD_FF_SAVE, 0);
            g_ff_unlocked = false;
            ff_lock_shared();
        }
        break;
    /* the sidebar */
    case ID_NAV_PLAY: show_tab(TAB_PLAY); break;
    case ID_TAB_SET: show_tab(TAB_SET); break;
    case ID_TAB_HOWTO: show_tab(TAB_HOWTO); break;
    case ID_TAB_LOG: show_tab(TAB_LOG); break;
    case ID_COPYLOG: copy_log(); ui_log("(Log copied. Paste it with Ctrl+V.)"); break;
    case ID_THEME: set_theme(!T.dark); save_theme(); break;
    case ID_DEVSOLO: push_cmd(CMD_DEV_SOLO, 0); show_tab(TAB_LOG); break;
    case ID_DEVTWO: push_cmd(CMD_DEV_TWO, 0); show_tab(TAB_LOG); break;
    default: break;
    }
}

static void on_timer(void)
{
    static char pend[65536];
    UiState st;
    bool st_dirty, ld[2];
    EnterCriticalSection(&g_cs);
    int n = g_npend;
    memcpy(pend, g_pend, (size_t)n);
    g_npend = 0;
    bool alert = g_alert, warned = g_warned;
    g_alert = g_warned = false;
    st = g_state;
    st_dirty = g_state_dirty;
    g_state_dirty = false;
    for (int k = 0; k < 2; k++) {
        ld[k] = g_list_dirty[k];
        if (ld[k]) {
            g_nview[k] = g_nlist[k];
            for (int i = 0; i < g_nlist[k]; i++) g_view[k][i] = g_lists[k][i];
            g_list_dirty[k] = false;
        }
    }
    LeaveCriticalSection(&g_cs);

    if (n) { pend[n] = 0; append_log(pend); }
    if (warned && g_tab != TAB_LOG && !g_log_unseen) {
        g_log_unseen = true;
        RedrawWindow(g_c[ID_TAB_LOG], 0, 0, 0x1 | 0x4);
    }
    if (alert && GetForegroundWindow() != g_wnd) {
        FLASHWINFO f = { sizeof f, g_wnd, 0x3 | 0xC, 0, 0 };
        FlashWindowEx(&f);
        MessageBeep(0x40);
    }
    if (st_dirty) {
        bool lobby_changed = st.lobby != g_shown.lobby || st.mode != g_shown.mode;
        bool mode_changed = st.mode != g_shown.mode, dot_changed = st.ready != g_shown.ready || (st.status[0] == '!') != (g_shown.status[0] == '!');
        g_shown = st;
        static char shown_name[20];
        if (strcmp(shown_name, st.my_name) != 0) { memcpy(shown_name, st.my_name, sizeof shown_name); set_text(ID_NAME, st.my_name); }
        set_text(ID_STATUS, st.status[0] == '!' ? st.status + 1 : st.status);
        RedrawWindow(g_c[ID_STATUS], 0, 0, 0x1 | 0x4);
        if (lobby_changed) {
            char id[24] = "";
            if (st.lobby) lobby_code(st.lobby, id);
            if (st.lobby || st.mode != MODE_JOIN) set_text(ID_LOBBY, id);
        }
        if (mode_changed) apply_visibility();
        if ((mode_changed || dot_changed) && g_tab == TAB_PLAY) {
            RECT r = { S_(CX), S_(STAT_Y), S_(CX + 40), S_(STAT_Y + STAT_H) }; /* the status dot */
            RedrawWindow(g_wnd, &r, 0, 0x1 | 0x4);
        }
    }
    for (int k = 0; k < 2; k++)
        if (ld[k] && g_list_shown == k) show_list(k);
    refresh_controls();
    update_howto();
    if (g_closing && (WaitForSingleObject(g_thread, 0) == 0 || now_ms() - g_close_ms > 8000)) DestroyWindow(g_wnd);
}

static LRESULT WINAPI wndproc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_ERASEBKGND:
        draw_page((HDC)wp);
        return 1;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        return control_colors(m, (HDC)wp, (HWND)lp);
    case WM_MEASUREITEM: {
        MeasureItem *mi = (MeasureItem *)lp;
        if (mi->type == ODT_LISTBOX) mi->height = (UINT)S_(26);
        return 1;
    }
    case WM_DRAWITEM: {
        DrawItem *d = (DrawItem *)lp;
        if (d->type == ODT_LISTBOX) draw_list_item(d);
        else draw_button(d);
        return 1;
    }
    case WM_COMMAND:
        on_command((int)(wp & 0xFFFF), (int)((wp >> 16) & 0xFFFF));
        return 0;
    case WM_TIMER:
        on_timer();
        return 0;
    case WM_CLOSE:
        if (g_help) { DestroyWindow(g_help); g_help = g_help_text = 0; }
        if (!g_closing) {
            g_closing = true;
            g_close_ms = now_ms();
            push_cmd(CMD_QUIT, 0); /* the engine stops the server and removes the adapter first */
            ui_log("Closing... (removing the SCBLLink adapter and putting your files back)");
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(w, m, wp, lp);
    }
}

static DWORD WINAPI engine_thread(void *arg)
{
    (void)arg;
    engine_run();
    return 0;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    plat_init();
    InitCommonControls();
    InitializeCriticalSection(&g_cs);

    HDC dc = GetDC(0);
    g_dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(0, dc);
    if (g_dpi < 96) g_dpi = 96;
    {
        /* a scaled-up screen with little room: shrink to fit */
        int fit = GetSystemMetrics(62) - 16, fitw = GetSystemMetrics(61) - 16;
        if (fit > 300 && WIN_H * g_dpi / 96 > fit) g_dpi = fit * 96 / WIN_H;
        if (fitw > 300 && WIN_W * g_dpi / 96 > fitw) g_dpi = fitw * 96 / WIN_W;
        if (g_dpi < 72) g_dpi = 72;
    }
    const WCHAR *face = WTEXT("Segoe UI");
    g_font = CreateFontW(-S_(12), 0, 0, 0, FW_NORMAL, 0, 0, 0, 1, 0, 0, CLEARTYPE_QUALITY, 0, face);
    g_bold = CreateFontW(-S_(12), 0, 0, 0, FW_BOLD, 0, 0, 0, 1, 0, 0, CLEARTYPE_QUALITY, 0, face);
    g_big = CreateFontW(-S_(18), 0, 0, 0, FW_BOLD, 0, 0, 0, 1, 0, 0, CLEARTYPE_QUALITY, 0, face);
    g_mid = CreateFontW(-S_(14), 0, 0, 0, FW_BOLD, 0, 0, 0, 1, 0, 0, CLEARTYPE_QUALITY, 0, face);
    g_cap = CreateFontW(-S_(11), 0, 0, 0, FW_BOLD, 0, 0, 0, 1, 0, 0, CLEARTYPE_QUALITY, 0, face);
    g_brand = CreateFontW(-S_(18), 0, 0, 0, FW_BOLD, 1 /* italic */, 0, 0, 1, 0, 0, CLEARTYPE_QUALITY, 0, face);
    g_icon = CreateFontW(-S_(15), 0, 0, 0, FW_NORMAL, 0, 0, 0, 1, 0, 0, CLEARTYPE_QUALITY, 0, WTEXT("Segoe MDL2 Assets"));
    g_icon_big = CreateFontW(-S_(20), 0, 0, 0, FW_NORMAL, 0, 0, 0, 1, 0, 0, CLEARTYPE_QUALITY, 0, WTEXT("Segoe MDL2 Assets"));

    HINSTANCE inst = GetModuleHandleW(0);
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.proc = wndproc;
    wc.inst = inst;
    wc.icon = LoadIconW(inst, (const WCHAR *)(uintptr_t)1);
    wc.iconSm = wc.icon;
    wc.cursor = LoadCursorW(0, (const WCHAR *)(uintptr_t)32512);
    set_theme(!saved_light());
    wc.background = 0;
    wc.className = WTEXT("SCBLLinkWindow");
    RegisterClassExW(&wc);

    DWORD style = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r = { 0, 0, S_(WIN_W), S_(WIN_H) };
    AdjustWindowRectEx(&r, style, 0, 0);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    int x = (GetSystemMetrics(0) - ww) / 2, y = (GetSystemMetrics(1) - wh) / 2;
    g_wnd = CreateWindowExW(0, wc.className,
#ifdef SCBL_DEV
                            WTEXT("SCBLLink " SCBLLINK_VERSION " DEV - Splinter Cell: Blacklist")
#else
                            WTEXT("SCBLLink " SCBLLINK_VERSION " - Splinter Cell: Blacklist")
#endif
                            , style, x < 0 ? 0 : x, y < 0 ? 0 : y, ww, wh, 0, 0, inst, 0);
    if (!g_wnd) return 1;
    build();
    set_theme(T.dark);
    show_tab(TAB_PLAY);
    on_timer();
    ShowWindow(g_wnd, 1);
    UpdateWindow(g_wnd);
    SetTimer(g_wnd, 1, 100, 0);

    g_thread = CreateThread(0, 0, engine_thread, 0, 0, 0);
    if (!g_thread) {
        MessageBoxW(g_wnd, WTEXT("Could not start."), WTEXT("SCBLLink"), MB_ICONWARNING);
        return 1;
    }
    MSG msg;
    while (GetMessageW(&msg, 0, 0, 0) > 0) {
        if (IsDialogMessageW(g_wnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
