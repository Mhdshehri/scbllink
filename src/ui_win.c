/* ui_win.c - the SCBLLink window (Windows), built from SCCTLink's window:
 * "Host a game" or "Join a friend", the list of friends, the lobby code,
 * "Start Blacklist", "How to" and the log. The engine (scbllink.c) runs on a
 * second thread; this file only shows its state and sends it commands.
 * Win32 is declared by hand (no Windows SDK). By Mhdshehri. */
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

enum {
    ID_HOST = 101, ID_JOIN, ID_STOP, ID_STATUS, ID_LOBBY, ID_COPYID, ID_JOINID, ID_LISTLABEL, ID_LIST,
    ID_ACTION, ID_REFRESH, ID_LAUNCH, ID_TEST, ID_DETAILS, ID_LOG, ID_HOWTO, ID_TITLE, ID_ACCENT,
    ID_THEME, ID_TAB_HOWTO, ID_TAB_LOG, ID_COPYLOG, ID_LOBBYLABEL, ID_FIX, ID_NAMELABEL, ID_NAME, ID_SAVENAME, ID_FFSET, ID_DEVSOLO, ID_DEVTWO,
    ID_LAST /* keep last */
};

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
static HFONT g_font, g_bold, g_big;
static int g_dpi = 96;
static HANDLE g_thread;
static bool g_closing;
static u64 g_close_ms;
static UiPerson g_view[2][16];
static int g_nview[2];
static int g_list_shown = -1;
static HWND g_ff; /* the game settings window, 0 when closed */
static void ff_lock_shared(void);
static void ff_close(void);
enum { TAB_HOWTO, TAB_LOG };
static int g_tab;
static bool g_log_unseen;

static int S_(int v) { return v * g_dpi / 96; }

/* ---------- dark / light theme (SCCTLink's colors) ---------- */

typedef struct {
    bool dark;
    DWORD bg, panel, text, dim, accent, accent_down, btn, btn_down, border, dis_bg, dis_text, status, sel_text;
} Theme;

static const Theme DARK = { true,
    RGBC(18, 18, 20), RGBC(28, 29, 33), RGBC(232, 230, 227), RGBC(139, 142, 150),
    RGBC(22, 138, 52), RGBC(14, 102, 38), RGBC(40, 42, 47), RGBC(30, 31, 35), RGBC(62, 65, 72),
    RGBC(26, 27, 31), RGBC(85, 88, 96), RGBC(87, 214, 120), RGBC(255, 255, 255) };
static const Theme LIGHT = { false,
    RGBC(244, 245, 247), RGBC(255, 255, 255), RGBC(28, 30, 34), RGBC(106, 112, 122),
    RGBC(30, 128, 56), RGBC(20, 98, 42), RGBC(255, 255, 255), RGBC(234, 236, 240), RGBC(208, 212, 219),
    RGBC(236, 238, 241), RGBC(166, 171, 180), RGBC(30, 128, 56), RGBC(255, 255, 255) };

static Theme T;
typedef struct { int id, x, y, w, h, tab; } Frame;
static Frame g_frames[8];
static int g_nframes;
static HBRUSH g_br_bg, g_br_panel, g_br_accent, g_br_border;

static void draw_frames(HDC dc)
{
    HBRUSH b = CreateSolidBrush(T.panel);
    HANDLE p = CreatePen(0, 1, T.border);
    HANDLE ob = SelectObject(dc, b), op = SelectObject(dc, p);
    for (int i = 0; i < g_nframes; i++) {
        Frame *f = &g_frames[i];
        if (f->tab >= 0 && (f->tab != g_tab || !IsWindowVisible(g_c[f->id]))) continue;
        RoundRect(dc, S_(f->x), S_(f->y), S_(f->x + f->w), S_(f->y + f->h), S_(8), S_(8));
    }
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
}

static void *sys_proc(const char *dll, const char *fn)
{
    void *m = LoadLibraryExA(dll, 0, 0x800 /* only from System32 */);
    return m ? GetProcAddress(m, fn) : 0;
}

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
        if (dwm(g_wnd, 20, &on, sizeof on) != 0) dwm(g_wnd, 19, &on, sizeof on); /* dark title bar */
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
    if (g_br_bg) { DeleteObject(g_br_bg); DeleteObject(g_br_panel); DeleteObject(g_br_accent); DeleteObject(g_br_border); }
    g_br_border = CreateSolidBrush(T.border);
    g_br_bg = CreateSolidBrush(T.bg);
    g_br_panel = CreateSolidBrush(T.panel);
    g_br_accent = CreateSolidBrush(T.accent);
    if (g_wnd) {
        SetWindowTextW(g_c[ID_THEME], T.dark ? WTEXT("Light mode") : WTEXT("Dark mode"));
        apply_system_parts();
        RedrawWindow(g_wnd, 0, 0, 0x1 | 0x4 | 0x80 | 0x400);
    }
}

enum { ID_FF_CTL = 2000, ID_FF_LABEL = 2100, ID_FF_SAVE = 2201, ID_FF_CANCEL, ID_FF_DEFAULTS, ID_FF_HEAD1, ID_FF_HEAD2, ID_FF_NOTE, ID_FF_FOOT, ID_FF_SWITCH, ID_FF_NOTE1, ID_FF_INFO };
static bool is_primary(UINT id) { return id == ID_HOST || id == ID_JOIN || id == ID_ACTION || id == ID_LAUNCH || id == ID_FF_SAVE; }
static bool is_tab(UINT id) { return id == ID_TAB_HOWTO || id == ID_TAB_LOG; }

static void draw_tab(DrawItem *d)
{
    bool on = (d->id == ID_TAB_LOG) == (g_tab == TAB_LOG);
    FillRect(d->dc, &d->rc, g_br_bg);
    if (on) {
        HBRUSH b = CreateSolidBrush(T.btn);
        HANDLE p = CreatePen(0, 1, T.border);
        HANDLE ob = SelectObject(d->dc, b), op = SelectObject(d->dc, p);
        RoundRect(d->dc, d->rc.left, d->rc.top, d->rc.right, d->rc.bottom, S_(8), S_(8));
        SelectObject(d->dc, ob);
        SelectObject(d->dc, op);
        DeleteObject(b);
        DeleteObject(p);
        RECT u = { d->rc.left + S_(10), d->rc.bottom - S_(3), d->rc.right - S_(10), d->rc.bottom - S_(1) };
        FillRect(d->dc, &u, g_br_accent);
    }
    WCHAR w[32];
    int n = GetWindowTextW(d->hwnd, w, 32);
    HANDLE of = SelectObject(d->dc, on ? g_bold : g_font);
    SetBkMode(d->dc, TRANSPARENT_BK);
    SetTextColor(d->dc, on ? T.text : T.dim);
    RECT tr = d->rc;
    DrawTextW(d->dc, w, n, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(d->dc, of);
    if (d->id == ID_TAB_LOG && g_log_unseen) { /* dot: a warning arrived */
        HANDLE p = CreatePen(0, 1, T.accent);
        HANDLE ob = SelectObject(d->dc, g_br_accent), op = SelectObject(d->dc, p);
        int r = S_(7), x = d->rc.right - S_(12), y = d->rc.top + S_(6);
        RoundRect(d->dc, x, y, x + r, y + r, r, r);
        SelectObject(d->dc, ob);
        SelectObject(d->dc, op);
        DeleteObject(p);
    }
}

static DWORD red_text(void) { return T.dark ? RGBC(240, 98, 98) : RGBC(196, 40, 40); }

static void draw_button(DrawItem *d)
{
    if (is_tab(d->id)) { draw_tab(d); return; }
    bool down = d->state & ODS_SELECTED, off = d->state & ODS_DISABLED, focus = d->state & ODS_FOCUS;
    bool kick = d->id == ID_ACTION && g_shown.mode == MODE_HOST;
    DWORD fill, text, edge;
    if (off) { fill = T.dis_bg; text = T.dis_text; edge = T.dis_bg; }
    else if (kick) { fill = down ? T.btn_down : T.btn; text = red_text(); edge = T.dark ? RGBC(90, 46, 48) : RGBC(230, 180, 180); }
    else if (is_primary(d->id)) { fill = down ? T.accent_down : T.accent; text = T.sel_text; edge = fill; }
    else { fill = down ? T.btn_down : T.btn; text = T.text; edge = T.border; }
    FillRect(d->dc, &d->rc, g_br_bg);
    HBRUSH b = CreateSolidBrush(fill);
    HANDLE p = CreatePen(0, 1, edge);
    HANDLE ob = SelectObject(d->dc, b), op = SelectObject(d->dc, p);
    int r = S_(10);
    RoundRect(d->dc, d->rc.left, d->rc.top, d->rc.right, d->rc.bottom, r, r);
    if (focus && !off) {
        HANDLE fp = CreatePen(0, 1, is_primary(d->id) && !kick ? T.sel_text : T.dim);
        SelectObject(d->dc, fp);
        SelectObject(d->dc, GetStockObject(NULL_BRUSH));
        int k = S_(3);
        RoundRect(d->dc, d->rc.left + k, d->rc.top + k, d->rc.right - k, d->rc.bottom - k, r, r);
        SelectObject(d->dc, p);
        DeleteObject(fp);
    }
    SelectObject(d->dc, ob);
    SelectObject(d->dc, op);
    DeleteObject(b);
    DeleteObject(p);
    WCHAR w[64];
    int n = GetWindowTextW(d->hwnd, w, 64);
    HANDLE of = SelectObject(d->dc, is_primary(d->id) ? g_bold : g_font);
    SetBkMode(d->dc, TRANSPARENT_BK);
    SetTextColor(d->dc, text);
    RECT tr = d->rc;
    if (down) { tr.top += 1; tr.bottom += 1; }
    DrawTextW(d->dc, w, n, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(d->dc, of);
}

static void draw_list_item(DrawItem *d)
{
    bool sel = d->state & ODS_SELECTED;
    FillRect(d->dc, &d->rc, sel ? g_br_accent : g_br_panel);
    if (d->item == (UINT)-1) return;
    WCHAR w[UI_NAME + 8];
    int n = (int)SendMessageW(d->hwnd, LB_GETTEXT, d->item, (LPARAM)w);
    if (n <= 0) return;
    HANDLE of = SelectObject(d->dc, g_font);
    SetBkMode(d->dc, TRANSPARENT_BK);
    SetTextColor(d->dc, sel ? T.sel_text : T.text);
    RECT tr = d->rc;
    tr.left += S_(8);
    DrawTextW(d->dc, w, n, &tr, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(d->dc, of);
}

static LRESULT control_colors(UINT msg, HDC dc, HWND c)
{
    if (c == g_c[ID_ACCENT]) return (LRESULT)g_br_accent;
    bool box = msg != WM_CTLCOLORSTATIC || c == g_c[ID_LOG] || c == g_c[ID_LOBBY] || c == g_c[ID_HOWTO] || c == g_c[ID_NAME];
    bool dim = c == g_c[ID_LISTLABEL] || c == g_c[ID_LOBBYLABEL] || c == g_c[ID_NAMELABEL];
    DWORD tc = dim ? T.dim : T.text;
    if (c == g_c[ID_STATUS]) tc = g_shown.ready ? T.status : g_shown.status[0] == '!' ? red_text() : T.text;
    SetTextColor(dc, tc);
    SetBkColor(dc, box ? T.panel : T.bg);
    return (LRESULT)(box ? g_br_panel : g_br_bg);
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

static HWND ctl(int id, const WCHAR *cls, const WCHAR *text, DWORD style, DWORD ex, int x, int y, int w, int h, HFONT f)
{
    HWND h_ = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, S_(x), S_(y), S_(w), S_(h), g_wnd,
                              (HMENU)(uintptr_t)id, GetModuleHandleW(0), 0);
    SendMessageW(h_, WM_SETFONT, (WPARAM)f, 1);
    g_c[id] = h_;
    return h_;
}

static int g_frame_tab = -1;
#define BOX_Y 418
#define BOX_H 300
static void framed(int id, const WCHAR *cls, DWORD style, int x, int y, int w, int h, int ix, int iy)
{
    if (g_nframes < 8) g_frames[g_nframes++] = (Frame){ id, x, y, w, h, g_frame_tab };
    ctl(id, cls, WTEXT(""), style, 0, x + ix, y + iy, w - 2 * ix, h - 2 * iy, g_font);
}

static void build(void)
{
    const WCHAR *B = WTEXT("BUTTON"), *T_ = WTEXT("STATIC"), *E = WTEXT("EDIT"), *L = WTEXT("LISTBOX");
    const DWORD OD = WS_TABSTOP | BS_OWNERDRAW;
#ifdef SCBL_DEV
    ctl(ID_TITLE, T_, WTEXT("DEV"), 0, 0, 14, 12, 40, 20, g_bold);
    ctl(ID_DEVTWO, B, WTEXT("2nd game: off"), OD, 0, 56, 9, 128, 24, g_font);
    ctl(ID_DEVSOLO, B, WTEXT("Record alone: off"), OD, 0, 190, 9, 154, 24, g_font);
#else
    ctl(ID_TITLE, T_, WTEXT("Splinter Cell: Blacklist co-op with Steam friends: no VPN, your IP stays hidden"), 0, 0, 14, 12, 460, 20, g_bold);
#endif
    ctl(ID_THEME, B, WTEXT(""), OD, 0, 486, 9, 100, 24, g_font);
    ctl(ID_ACCENT, T_, WTEXT(""), 0, 0, 14, 36, 572, 2, g_font);
    ctl(ID_HOST, B, WTEXT("Host a game"), OD, 0, 14, 48, 228, 38, g_bold);
    ctl(ID_JOIN, B, WTEXT("Join a friend"), OD, 0, 252, 48, 228, 38, g_bold);
    ctl(ID_STOP, B, WTEXT("Stop"), OD, 0, 486, 48, 100, 38, g_font);
    ctl(ID_STATUS, T_, WTEXT(""), 0x4000 /* SS_ENDELLIPSIS */, 0, 14, 96, 572, 26, g_big);
    ctl(ID_LOBBYLABEL, T_, WTEXT("Lobby code:"), 0, 0, 14, 134, 74, 22, g_font);
    framed(ID_LOBBY, E, WS_TABSTOP | ES_AUTOHSCROLL, 88, 130, 246, 26, 7, 5);
    ctl(ID_COPYID, B, WTEXT("Copy code"), OD, 0, 342, 130, 100, 26, g_font);
    ctl(ID_JOINID, B, WTEXT("Join this code"), OD, 0, 450, 130, 136, 26, g_font);
    ctl(ID_NAMELABEL, T_, WTEXT("Your name in the game:"), 0, 0, 14, 172, 140, 22, g_font);
    framed(ID_NAME, E, WS_TABSTOP | ES_AUTOHSCROLL, 156, 166, 178, 26, 7, 5);
    SendMessageW(g_c[ID_NAME], EM_SETLIMITTEXT, 16, 0);
    ctl(ID_SAVENAME, B, WTEXT("Save name"), OD, 0, 342, 166, 100, 26, g_font);
    ctl(ID_LISTLABEL, T_, WTEXT(""), 0, 0, 14, 202, 572, 20, g_font);
    framed(ID_LIST, L, WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
           14, 224, 430, 112, 4, 4);
    ctl(ID_ACTION, B, WTEXT("Join"), OD, 0, 452, 224, 134, 34, g_bold);
    ctl(ID_REFRESH, B, WTEXT("Refresh"), OD, 0, 452, 264, 134, 30, g_font);
    ctl(ID_LAUNCH, B, WTEXT("Start Blacklist"), OD, 0, 14, 346, 180, 32, g_bold);
    ctl(ID_FIX, B, WTEXT("Install SCBL Fix"), OD, 0, 202, 346, 150, 32, g_font);
    ctl(ID_TEST, B, WTEXT("Test the link"), OD, 0, 360, 346, 110, 32, g_font);
    ctl(ID_DETAILS, B, WTEXT("Details"), OD, 0, 478, 346, 108, 32, g_font);
    ctl(ID_TAB_HOWTO, B, WTEXT("How to"), OD, 0, 14, 384, 96, 28, g_font);
    ctl(ID_TAB_LOG, B, WTEXT("Log"), OD, 0, 114, 384, 80, 28, g_font);
    ctl(ID_FFSET, B, WTEXT("Game settings"), OD, 0, 360, 384, 110, 28, g_font);
    ctl(ID_COPYLOG, B, WTEXT("Copy log"), OD, 0, 478, 384, 108, 28, g_font);
    framed(ID_LOG, E, WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 14, BOX_Y, 572, BOX_H, 7, 5);
    ctl(ID_HOWTO, E, WTEXT(""), WS_VSCROLL | ES_MULTILINE | ES_READONLY, 0, 14 + 7, BOX_Y + 5, 572 - 14, BOX_H - 10, g_font);
    SendMessageW(g_c[ID_LOG], EM_SETLIMITTEXT, 0x7FFFFFFE, 0);
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
        ADD("=> First click \"Update SCBL Fix\": the game folder has the game DLL of another SCBLLink version.\r\n");
        ADD("   Everything else stays locked until then, so the game always gets the DLL made for this SCBLLink.\r\n\r\n");
    } else if (!s->dll_ok) {
        ADD("=> First click \"Install SCBL Fix\" (once; again after a Steam \"Verify integrity\").\r\n");
        ADD("   It puts SCBLLink's game DLL in the game folder and installs Fusion Fix.\r\n");
        ADD("   Everything else stays locked until then.\r\n\r\n");
    } else if (!s->fix_ok) {
        ADD("Fusion Fix isn't in yet: click \"Install SCBL Fix\" again to get it (see the log).\r\n\r\n");
    }
    if (s->mode == MODE_IDLE) {
        ADD("HOSTING (the one whose PC runs the game's server):\r\n");
        ADD("  1. Click \"Host a game\".\r\n");
        ADD("  2. Wait for \"Ready\", then click \"Start Blacklist\".\r\n");
        ADD("  3. Your friend clicks \"Join a friend\" in their SCBLLink.\r\n\r\n");
        ADD("JOINING A FRIEND:\r\n");
        ADD("  1. Click \"Join a friend\", pick the friend who hosts and click Join\r\n");
        ADD("     (or paste their lobby code and click \"Join this code\").\r\n");
        ADD("  2. Wait for \"Ready\", then click \"Start Blacklist\".\r\n\r\n");
        ADD("Don't start the game before SCBLLink says Ready: the game looks for its server only once, when it starts.\r\n\r\n");
        ADD("Only your Steam friends can join, always through Valve's relay: nobody sees anyone's IP address.\r\n");
    } else if (s->mode == MODE_HOST) {
        ADD("%sHost: your lobby is open%s\r\n", mark(s->lobby != 0, &open), s->lobby ? "" : " (creating...)");
        ADD("%sThe game's server answers (Ready)\r\n", mark(s->ready, &open));
        ADD("%sStart Blacklist (click \"Start Blacklist\")\r\n", mark(s->game_running, &open));
        ADD("%sYour friend links to you (they click \"Join a friend\")\r\n", mark(s->linked, &open));
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

static void show_tab(int tab)
{
    g_tab = tab;
    ShowWindow(g_c[ID_HOWTO], tab == TAB_HOWTO ? 5 : 0);
    ShowWindow(g_c[ID_LOG], tab == TAB_LOG ? 5 : 0);
    if (tab == TAB_LOG) g_log_unseen = false;
    RedrawWindow(g_c[ID_TAB_HOWTO], 0, 0, 0x1 | 0x4);
    RedrawWindow(g_c[ID_TAB_LOG], 0, 0, 0x1 | 0x4);
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
    bool fixed = s->game_found && s->dll_ok;
    /* the game has to start after SCBLLink is ready: while it runs, no new link */
    EnableWindow(g_c[ID_HOST], fixed && idle && !g_closing && !s->game_running);
    EnableWindow(g_c[ID_JOIN], fixed && idle && !g_closing && !s->game_running);
    EnableWindow(g_c[ID_LOBBY], fixed || !idle);
    EnableWindow(g_c[ID_NAME], fixed);
    EnableWindow(g_c[ID_STOP], !idle && !g_closing);
    static int ro = -1;
    int want_ro = host || (join && s->lobby);
    if (want_ro != ro) { SendMessageW(g_c[ID_LOBBY], EM_SETREADONLY, (WPARAM)want_ro, 0); ro = want_ro; }
    EnableWindow(g_c[ID_COPYID], host && s->lobby);
    EnableWindow(g_c[ID_JOINID], fixed && (idle || join) && !s->lobby && !g_closing);
    int which = host ? LIST_LINKED : LIST_HOSTING;
    if (which != g_list_shown) show_list(which);
    set_text_once(ID_LISTLABEL, idle ? "" : host ? "Friends linked to your game right now:" : "Friends hosting - pick one and click Join:");
    set_text_once(ID_ACTION, host ? "Kick" : "Join");
    ShowWindow(g_c[ID_ACTION], idle ? 0 : 5);
    EnableWindow(g_c[ID_ACTION], !g_closing && ((join && !s->lobby && g_nview[LIST_HOSTING] > 0) || (host && g_nview[LIST_LINKED] > 0)));
    ShowWindow(g_c[ID_REFRESH], join ? 5 : 0);
    EnableWindow(g_c[ID_REFRESH], !g_closing && join && !s->lobby);
    EnableWindow(g_c[ID_LAUNCH], !g_closing && s->ready && !s->game_running);
    /* until the in-game overlay: an invitation turns "Test the link" into "Join <name>",
       and with SCBLLink's own game DLL linked, "Details" into "Refresh friends" */
    {
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
        static int shown_solo = -1;
        if (shown_solo != (int)s->dev_solo) {
            SetWindowTextW(g_c[ID_DEVSOLO], s->dev_solo ? WTEXT("Record alone: ON") : WTEXT("Record alone: off"));
            RedrawWindow(g_c[ID_DEVSOLO], 0, 0, 0x1 | 0x4);
            shown_solo = s->dev_solo;
        }
        EnableWindow(g_c[ID_DEVSOLO], fixed && idle && !g_closing);
        static int shown_two = -1;
        if (shown_two != (int)s->dev_two) {
            SetWindowTextW(g_c[ID_DEVTWO], s->dev_two ? WTEXT("2nd game: ON") : WTEXT("2nd game: off"));
            RedrawWindow(g_c[ID_DEVTWO], 0, 0, 0x1 | 0x4);
            shown_two = s->dev_two;
        }
        EnableWindow(g_c[ID_DEVTWO], fixed && idle && !g_closing);
    }
#endif
    EnableWindow(g_c[ID_TEST], !g_closing && (s->my_ip[0] || s->invite_from[0]));
    EnableWindow(g_c[ID_DETAILS], !idle);
    EnableWindow(g_c[ID_SAVENAME], fixed && !g_closing);
    set_text_once(ID_FIX, s->fix_ok ? "Remove SCBL Fix" : s->fix_old ? "Update SCBL Fix" : "Install SCBL Fix");
    EnableWindow(g_c[ID_FIX], idle && !g_closing && s->game_found && !s->game_running);
    EnableWindow(g_c[ID_FFSET], fixed && !g_closing && s->ff_ok && !g_ff);
    static bool locked;
    if (g_ff && locked != s->ff_host_rules) ff_lock_shared();
    locked = s->ff_host_rules;
    if (g_ff && !s->ff_ok) ff_close();
}


/* ---------- "Game settings": Fusion Fix's options, written to its .ini by the engine ---------- */

#define BM_GETCHECK 0xF0
#define BM_SETCHECK 0xF1
#define BS_AUTOCHECKBOX 0x3L
#define CB_ADDSTRING 0x143
#define CB_GETCURSEL 0x147
#define CB_SETCURSEL 0x14E
#define CBS_DROPDOWNLIST 0x3L

static void ff_dark_title(void)
{
    typedef long(WINAPI * DwmSet)(HWND, DWORD, const void *, DWORD);
    DwmSet dwm = (DwmSet)sys_proc("dwmapi.dll", "DwmSetWindowAttribute");
    BOOL on = T.dark;
    if (dwm && dwm(g_ff, 20, &on, sizeof on) != 0) dwm(g_ff, 19, &on, sizeof on);
}
static HWND g_ffc[64];      /* its controls by setting */
static RECT g_ffbox[64];    /* frames drawn behind the number boxes (unscaled) */
static int g_nffbox;
static HWND g_fftip;        /* the explanations shown on the mouse */

typedef struct { UINT size, flags; HWND hwnd; uintptr_t id; RECT rect; HINSTANCE inst; WCHAR *text; LPARAM lp; void *reserved; } ToolInfo;
#define TTM_ADDTOOLW 0x432
#define TTM_SETMAXTIPWIDTH 0x418
#define TTM_SETDELAYTIME 0x403
#define TTF_IDISHWND 0x1
#define TTF_SUBCLASS 0x10

static void ff_tip(HWND on, const RECT *area, uintptr_t id, const char *text)
{
    if (!g_fftip || !text) return;
    static WCHAR w[400];
    ToolInfo t;
    memset(&t, 0, sizeof t);
    t.size = sizeof t;
    t.flags = TTF_SUBCLASS | (area ? 0 : TTF_IDISHWND);
    t.hwnd = area ? on : g_ff;
    t.id = area ? id : (uintptr_t)on;
    if (area) t.rect = (RECT){ S_(area->left), S_(area->top), S_(area->right), S_(area->bottom) };
    t.text = wide(text, w, 400);
    SendMessageW(g_fftip, TTM_ADDTOOLW, 0, (LPARAM)&t);
}

/* ---------- on/off switches (drawn by hand, like a phone's) ---------- */

static bool g_swon[65];     /* by setting; [64] = the co-op switch */
static RECT g_ffwarn;       /* the red note's box (unscaled) */

static int sw_slot(int id)
{
    if (id >= ID_FF_CTL && id < ID_FF_CTL + FF_NSETTINGS && id < ID_FF_CTL + 64 && FF_SETTINGS[id - ID_FF_CTL].kind == FF_BOOL) return id - ID_FF_CTL;
    if (id == ID_FF_SWITCH) return 64;
    return -1;
}

static void sw_set(int slot, bool on)
{
    g_swon[slot] = on;
    HWND h = slot == 64 ? GetDlgItem(g_ff, ID_FF_SWITCH) : g_ffc[slot];
    if (h) RedrawWindow(h, 0, 0, 0x1 | 0x4);
}

static void draw_switch(DrawItem *d, bool on)
{
    bool off = d->state & ODS_DISABLED, focus = d->state & ODS_FOCUS;
    FillRect(d->dc, &d->rc, g_br_bg);
    int h = S_(18), w = S_(34);
    int x = d->rc.left, y = d->rc.top + ((d->rc.bottom - d->rc.top) - h) / 2;
    DWORD track = on ? (off ? (T.dark ? RGBC(24, 70, 38) : RGBC(150, 196, 162)) : T.accent)
                     : (off ? T.dis_bg : (T.dark ? RGBC(66, 69, 76) : RGBC(196, 200, 207)));
    DWORD edge = focus && !off ? T.dim : track;
    HBRUSH b = CreateSolidBrush(track);
    HANDLE p = CreatePen(0, 1, off && !on ? T.border : edge);
    HANDLE ob = SelectObject(d->dc, b), op = SelectObject(d->dc, p);
    RoundRect(d->dc, x, y, x + w, y + h, h, h);
    SelectObject(d->dc, ob);
    SelectObject(d->dc, op);
    DeleteObject(b);
    DeleteObject(p);
    int k = h - S_(6), kx = on ? x + w - S_(3) - k : x + S_(3), ky = y + S_(3);
    DWORD knob = off ? T.dis_text : RGBC(255, 255, 255);
    b = CreateSolidBrush(knob);
    p = CreatePen(0, 1, knob);
    ob = SelectObject(d->dc, b);
    op = SelectObject(d->dc, p);
    RoundRect(d->dc, kx, ky, kx + k, ky + k, k, k);
    SelectObject(d->dc, ob);
    SelectObject(d->dc, op);
    DeleteObject(b);
    DeleteObject(p);
    WCHAR t[128];
    int n = GetWindowTextW(d->hwnd, t, 128);
    HANDLE of = SelectObject(d->dc, d->id == ID_FF_SWITCH ? g_bold : g_font);
    SetBkMode(d->dc, TRANSPARENT_BK);
    SetTextColor(d->dc, off ? T.dis_text : T.text);
    RECT tr = d->rc;
    tr.left = x + w + S_(10);
    DrawTextW(d->dc, t, n, &tr, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(d->dc, of);
}

/* the "i" (information) button: a ring with an i in it */
static void draw_info(DrawItem *d)
{
    bool down = d->state & ODS_SELECTED, focus = d->state & ODS_FOCUS;
    FillRect(d->dc, &d->rc, g_br_bg);
    int sz = d->rc.right - d->rc.left;
    if (d->rc.bottom - d->rc.top < sz) sz = d->rc.bottom - d->rc.top;
    int x = d->rc.left, y = d->rc.top;
    HBRUSH b = CreateSolidBrush(down ? T.btn_down : T.btn);
    HANDLE p = CreatePen(0, S_(2) > 1 ? S_(2) : 2, focus ? T.accent : T.dim);
    HANDLE ob = SelectObject(d->dc, b), op = SelectObject(d->dc, p);
    RoundRect(d->dc, x + 1, y + 1, x + sz - 1, y + sz - 1, sz, sz);
    SelectObject(d->dc, ob);
    SelectObject(d->dc, op);
    DeleteObject(b);
    DeleteObject(p);
    static HFONT f;
    if (!f) f = CreateFontW(-S_(16), 0, 0, 0, FW_BOLD, 0, 0, 0, 1, 0, 0, CLEARTYPE_QUALITY, 0, WTEXT("Georgia"));
    HANDLE of = SelectObject(d->dc, f);
    SetBkMode(d->dc, TRANSPARENT_BK);
    SetTextColor(d->dc, T.text);
    RECT tr = { x, y + (down ? 1 : 0), x + sz, y + sz + (down ? 1 : 0) };
    DrawTextW(d->dc, WTEXT("i"), 1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(d->dc, of);
}

static DWORD warn_fill(void) { return T.dark ? RGBC(36, 31, 33) : RGBC(251, 242, 242); }
static DWORD warn_edge(void) { return T.dark ? RGBC(94, 50, 54) : RGBC(228, 184, 186); }
static DWORD warn_text(void) { return T.dark ? RGBC(214, 92, 92) : RGBC(168, 38, 38); }

static HWND ff_ctl(int id, const WCHAR *cls, const WCHAR *text, DWORD style, int x, int y, int w, int h, HFONT f)
{
    HWND h_ = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, S_(x), S_(y), S_(w), S_(h), g_ff,
                              (HMENU)(uintptr_t)id, GetModuleHandleW(0), 0);
    SendMessageW(h_, WM_SETFONT, (WPARAM)f, 1);
    return h_;
}

/* show a value (already checked) in its control */
static void ff_show(int i, const char *v)
{
    const FfSetting *f = &FF_SETTINGS[i];
    HWND c = g_ffc[i];
    if (f->kind == FF_BOOL) { (void)c; sw_set(i, v[0] == '1'); }
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
        SendMessageW(c, CB_SETCURSEL, (WPARAM)sel, 0);
    } else {
        WCHAR w[FF_VAL];
        SetWindowTextW(c, wide(v, w, FF_VAL));
    }
}

static void ff_fill(bool defaults)
{
    int n = 0;
    char *ini = defaults ? 0 : file_read_all(g_shown.ff_ini, &n);
    /* motion blur and occlusion live in the game's own videoSettings.ini */
    char *vid = defaults || !g_shown.video_ini[0] ? 0 : file_read_all(g_shown.video_ini, &n);
    for (int i = 0; i < FF_NSETTINGS; i++) {
        char raw[64], v[FF_VAL];
        const char *from = FF_SETTINGS[i].video ? vid : ini;
        if (!from || !ff_ini_get(from, FF_SETTINGS[i].sec, FF_SETTINGS[i].key, raw, sizeof raw) || !ff_normalize(i, raw, v, sizeof v))
            ff_normalize(i, FF_SETTINGS[i].def, v, sizeof v);
        ff_show(i, v);
    }
    free(ini);
    free(vid);
}

/* the window's values, checked; false (and a message) when one isn't valid */
static bool ff_collect(void)
{
    uint8_t buf[2048];
    int k = 0;
    for (int i = 0; i < FF_NSETTINGS; i++) {
        const FfSetting *f = &FF_SETTINGS[i];
        char in[FF_VAL] = "", v[FF_VAL];
        if (f->kind == FF_BOOL) sfmt(in, sizeof in, "%d", g_swon[i] ? 1 : 0);
        else if (f->kind == FF_CHOICE) {
            int sel = (int)SendMessageW(g_ffc[i], CB_GETCURSEL, 0, 0);
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
            MessageBoxW(g_ff, wide(q, wq, 256), WTEXT("Game settings"), MB_ICONWARNING);
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

static void ff_fill_shared_from_file(void)
{
    int n = 0;
    char *ini = file_read_all(g_shown.ff_ini, &n);
    for (int i = 0; ini && i < FF_NSETTINGS; i++) {
        char raw[64], v[FF_VAL];
        if (!FF_SETTINGS[i].shared) continue;
        if (!ff_ini_get(ini, FF_SETTINGS[i].sec, FF_SETTINGS[i].key, raw, sizeof raw) || !ff_normalize(i, raw, v, sizeof v)) continue;
        ff_show(i, v);
    }
    free(ini);
}

/* ---------- "What do these do?": every option and what it does, in one list ---------- */

static HWND g_ffhelp, g_ffhelp_text;

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
    case WM_CLOSE:
        DestroyWindow(w);
        return 0;
    case WM_DESTROY:
        if (w == g_ffhelp) { g_ffhelp = g_ffhelp_text = 0; }
        return 0;
    default: return DefWindowProcW(w, m, wp, lp);
    }
}

static void help_open(void)
{
    if (g_ffhelp) { ShowWindow(g_ffhelp, 5); return; }
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
    /* the text: each option's name, its limits, what it does */
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
    GetWindowRect(g_ff ? g_ff : g_wnd, &pr);
    DWORD style = WS_CAPTION | WS_SYSMENU;
    RECT r = { 0, 0, S_(560), S_(520) };
    AdjustWindowRectEx(&r, style, 0, 0);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    int x = pr.right - ww - S_(20), y = pr.top + S_(40);
    if (x < 0) x = 0;
    g_ffhelp = CreateWindowExW(0, WTEXT("SCBLLinkHelp"), WTEXT("What the game settings do"), style, x, y, ww, wh, g_ff ? g_ff : g_wnd, 0,
                               GetModuleHandleW(0), 0);
    if (!g_ffhelp) return;
    g_ffhelp_text = CreateWindowExW(0, WTEXT("EDIT"), WTEXT(""), WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                    S_(14), S_(10), S_(560 - 20), S_(520 - 20), g_ffhelp, (HMENU)(uintptr_t)1, GetModuleHandleW(0), 0);
    SendMessageW(g_ffhelp_text, WM_SETFONT, (WPARAM)g_font, 1);
    static WCHAR wt[16000];
    SetWindowTextW(g_ffhelp_text, wide(t, wt, 16000));
    typedef long(WINAPI * SetTheme)(HWND, const WCHAR *, const WCHAR *);
    SetTheme theme = (SetTheme)sys_proc("uxtheme.dll", "SetWindowTheme");
    if (theme) theme(g_ffhelp_text, T.dark ? WTEXT("DarkMode_Explorer") : WTEXT("Explorer"), 0); /* dark scroll bar */
    typedef long(WINAPI * DwmSet)(HWND, DWORD, const void *, DWORD);
    DwmSet dwm = (DwmSet)sys_proc("dwmapi.dll", "DwmSetWindowAttribute");
    BOOL on = T.dark;
    if (dwm && dwm(g_ffhelp, 20, &on, sizeof on) != 0) dwm(g_ffhelp, 19, &on, sizeof on);
    ShowWindow(g_ffhelp, 5);
}

static void ff_close(void)
{
    if (!g_ff) return;
    EnableWindow(g_wnd, 1);
    DestroyWindow(g_ff);
    g_ff = 0;
    g_fftip = 0;
    if (g_ffhelp) { DestroyWindow(g_ffhelp); g_ffhelp = g_ffhelp_text = 0; }
}

static bool g_ff_unlocked; /* the co-op switch is on */

static void ff_lock_shared(void)
{
    if (!g_ff) return;
    bool joined = g_shown.ff_host_rules;
    if (joined) g_ff_unlocked = false;
    HWND sw = GetDlgItem(g_ff, ID_FF_SWITCH);
    sw_set(64, g_ff_unlocked);
    EnableWindow(sw, !joined);
    for (int i = 0; i < FF_NSETTINGS; i++)
        if (FF_SETTINGS[i].shared) {
            EnableWindow(g_ffc[i], g_ff_unlocked && !joined);
            HWND lab = GetDlgItem(g_ff, ID_FF_LABEL + i);
            if (lab) RedrawWindow(lab, 0, 0, 0x1 | 0x4);
        }
    SetWindowTextW(GetDlgItem(g_ff, ID_FF_NOTE),
                   joined ? WTEXT("You joined a friend: you both play with their co-op settings, so they're locked here. Yours come back when you host.")
                          : WTEXT("You and your friend must have the same co-op settings. Don't copy them by hand: when you host, "
                                  "SCBLLink sends yours to your friends."));
}

static LRESULT WINAPI ff_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_ERASEBKGND: {
        RECT r;
        GetClientRect(w, &r);
        FillRect((HDC)wp, &r, g_br_bg);
        /* the number boxes' frames, like the main window's boxes */
        HBRUSH b = CreateSolidBrush(T.panel);
        HANDLE pen = CreatePen(0, 1, T.border);
        HANDLE ob = SelectObject((HDC)wp, b), op = SelectObject((HDC)wp, pen);
        for (int i = 0; i < g_nffbox; i++)
            RoundRect((HDC)wp, S_(g_ffbox[i].left), S_(g_ffbox[i].top), S_(g_ffbox[i].right), S_(g_ffbox[i].bottom), S_(8), S_(8));
        SelectObject((HDC)wp, ob);
        SelectObject((HDC)wp, op);
        DeleteObject(b);
        DeleteObject(pen);
        /* the red note: a soft grey box, dark red words */
        b = CreateSolidBrush(warn_fill());
        pen = CreatePen(0, 1, warn_edge());
        ob = SelectObject((HDC)wp, b);
        op = SelectObject((HDC)wp, pen);
        RoundRect((HDC)wp, S_(g_ffwarn.left), S_(g_ffwarn.top), S_(g_ffwarn.right), S_(g_ffwarn.bottom), S_(10), S_(10));
        SelectObject((HDC)wp, ob);
        SelectObject((HDC)wp, op);
        DeleteObject(b);
        DeleteObject(pen);
        return 1;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HWND c = (HWND)lp;
        if (c == GetDlgItem(w, ID_FF_NOTE)) {
            static HBRUSH wb;
            static DWORD wbc = 0xFFFFFFFF;
            if (wbc != warn_fill()) { if (wb) DeleteObject(wb); wbc = warn_fill(); wb = CreateSolidBrush(wbc); }
            SetTextColor((HDC)wp, warn_text());
            SetBkColor((HDC)wp, wbc);
            return (LRESULT)wb;
        }
        bool dim = c == GetDlgItem(w, ID_FF_FOOT) || c == GetDlgItem(w, ID_FF_NOTE1);
        bool box = m != WM_CTLCOLORSTATIC;
        /* a locked co-op setting's name is greyed like its switch */
        bool locked = false;
        for (int i = 0; m == WM_CTLCOLORSTATIC && i < FF_NSETTINGS; i++)
            if (FF_SETTINGS[i].shared && c == GetDlgItem(w, ID_FF_LABEL + i)) { locked = !g_ff_unlocked || g_shown.ff_host_rules; break; }
        SetTextColor((HDC)wp, locked ? T.dis_text : dim ? T.dim : T.text);
        SetBkColor((HDC)wp, box ? T.panel : T.bg);
        return (LRESULT)(box ? g_br_panel : g_br_bg);
    }
    case WM_DRAWITEM: {
        DrawItem *d = (DrawItem *)lp;
        int slot = sw_slot((int)d->id);
        if (d->id == ID_FF_INFO) draw_info(d);
        else if (slot >= 0) draw_switch(d, g_swon[slot]);
        else draw_button(d);
        return 1;
    }
    case WM_COMMAND: {
        int id = (int)(wp & 0xFFFF), code = (int)((wp >> 16) & 0xFFFF);
        int slot = sw_slot(id);
        if (slot >= 0 && slot < 64 && (code == 0 || code == 5 /* clicked, double-clicked */)) { sw_set(slot, !g_swon[slot]); return 0; }
        if (id == ID_FF_INFO) { help_open(); return 0; }
        if (id == ID_FF_SAVE || id == 1) {
            if (ff_collect()) { push_cmd(CMD_FF_SAVE, 0); ff_close(); }
        } else if (id == ID_FF_CANCEL || id == 2) ff_close();
        else if (id == ID_FF_DEFAULTS) {
            ff_fill(true);
            if (!g_ff_unlocked) ff_fill_shared_from_file(); /* locked ones stay as they are */
        } else if (id == ID_FF_SWITCH && (code == 0 || code == 5)) {
            g_ff_unlocked = !g_ff_unlocked;
            if (!g_ff_unlocked) ff_fill_shared_from_file(); /* switched off: back to the saved ones */
            ff_lock_shared();
        }
        return 0;
    }
    case WM_CLOSE: ff_close(); return 0;
    default: return DefWindowProcW(w, m, wp, lp);
    }
}

static void ff_open(void)
{
    if (g_ff) { ShowWindow(g_ff, 5); return; }
    if (!g_shown.ff_ok) return;
    static bool registered;
    if (!registered) {
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof wc);
        wc.cbSize = sizeof wc;
        wc.proc = ff_proc;
        wc.inst = GetModuleHandleW(0);
        wc.icon = LoadIconW(wc.inst, (const WCHAR *)(uintptr_t)1);
        wc.iconSm = wc.icon;
        wc.cursor = LoadCursorW(0, (const WCHAR *)(uintptr_t)32512);
        wc.className = WTEXT("SCBLLinkSettings");
        RegisterClassExW(&wc);
        registered = true;
    }
    enum { COLW = 310, GAP = 14, X0 = 14, ROW = 28, Y0 = 118 };
    int per = 0, sh = 0;
    for (int i = 0; i < FF_NSETTINGS; i++) { if (FF_SETTINGS[i].shared) sh++; else per++; }
    int sh_col2 = (sh + 1) / 2, rows = per > sh_col2 ? per : sh_col2;
    int cw = X0 * 2 + COLW * 3 + GAP * 2, chh = Y0 + rows * ROW + 64;
    DWORD style = WS_CAPTION | WS_SYSMENU;
    RECT r = { 0, 0, S_(cw), S_(chh) };
    AdjustWindowRectEx(&r, style, 0, 0);
    RECT mr;
    GetWindowRect(g_wnd, &mr);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    int x = mr.left + ((mr.right - mr.left) - ww) / 2, y = mr.top + 20;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    g_ff = CreateWindowExW(0, WTEXT("SCBLLinkSettings"), WTEXT("SCBLLink - Game settings (Fusion Fix)"), style, x, y, ww, wh, g_wnd, 0,
                           GetModuleHandleW(0), 0);
    if (!g_ff) return;
    g_fftip = CreateWindowExW(0x8 /* topmost */, WTEXT("tooltips_class32"), 0, 0x80000000 | 0x1 | 0x2 /* popup, always tip, no prefix */,
                              0, 0, 0, 0, g_ff, 0, GetModuleHandleW(0), 0);
    if (g_fftip) {
        SendMessageW(g_fftip, TTM_SETMAXTIPWIDTH, 0, S_(360));
        SendMessageW(g_fftip, TTM_SETDELAYTIME, 2 /* TTDT_AUTOPOP */, 30000);
        SendMessageW(g_fftip, TTM_SETDELAYTIME, 3 /* TTDT_INITIAL */, 300);
    }
    g_nffbox = 0;
    g_ff_unlocked = false;
    typedef long(WINAPI * SetTheme)(HWND, const WCHAR *, const WCHAR *);
    SetTheme theme = (SetTheme)sys_proc("uxtheme.dll", "SetWindowTheme");
    const WCHAR *B = WTEXT("BUTTON"), *ST = WTEXT("STATIC"), *E = WTEXT("EDIT"), *CB = WTEXT("COMBOBOX");
    int c1 = X0 + COLW + GAP, c12 = COLW * 2 + GAP;
    ff_ctl(ID_FF_HEAD1, ST, WTEXT("Your game"), 0, X0, 12, COLW, 20, g_bold);
    ff_ctl(ID_FF_NOTE1, ST, WTEXT("Only your screen and controls: each player picks their own. Point at an option to see what it does."),
           0, X0, 36, COLW, 52, g_font);
    ff_ctl(ID_FF_HEAD2, ST, WTEXT("Co-op: the same for everyone who plays together"), 0, c1, 12, c12 - 40, 20, g_bold);
    HWND info = ff_ctl(ID_FF_INFO, B, WTEXT("i"), WS_TABSTOP | BS_OWNERDRAW, cw - X0 - 28, 8, 28, 28, g_bold);
    ff_tip(info, 0, 0, "What every option does");
    HWND sw = ff_ctl(ID_FF_SWITCH, B, WTEXT("Change the co-op settings"), WS_TABSTOP | BS_OWNERDRAW, c1, 36, c12, 24, g_font);
    ff_tip(sw, 0, 0, "Off: the co-op settings are locked, so they aren't changed by mistake. Turn it on to change them. "
                     "Enemies, game over and unlocks change the missions themselves, so everyone who plays together needs the same ones.");
    g_ffwarn = (RECT){ c1, 66, c1 + c12, 104 };
    ff_ctl(ID_FF_NOTE, ST, WTEXT(""), 0, c1 + 12, 70, c12 - 24, 32, g_font);
    int pr = 0, sr = 0;
    for (int i = 0; i < FF_NSETTINGS && i < 64; i++) {
        const FfSetting *f = &FF_SETTINGS[i];
        int col, row;
        if (!f->shared) { col = 0; row = pr++; }
        else { col = sr < sh_col2 ? 1 : 2; row = sr < sh_col2 ? sr : sr - sh_col2; sr++; }
        int x0 = X0 + col * (COLW + GAP), y0 = Y0 + row * ROW;
        /* numbers: the label says what's allowed */
        char label[128];
        if (f->kind == FF_FLOAT) {
            int a = (int)(f->min * 100 + 0.5), b = (int)(f->max * 100 + 0.5);
            sfmt(label, sizeof label, "%s (%d.%02d to %d.%02d)", f->label, a / 100, a % 100, b / 100, b % 100);
        } else if (f->kind == FF_INT && f->zero_ok) sfmt(label, sizeof label, "%s (0 = off, %d to %d)", f->label, (int)f->min, (int)f->max);
        else if (f->kind == FF_INT) sfmt(label, sizeof label, "%s (%d to %d)", f->label, (int)f->min, (int)f->max);
        else sfmt(label, sizeof label, "%s", f->label);
        WCHAR wl[128];
        wide(label, wl, 128);
        RECT row_area = { x0, y0, x0 + COLW, y0 + ROW - 2 };
        if (f->kind == FF_BOOL) {
            g_ffc[i] = ff_ctl(ID_FF_CTL + i, B, wl, WS_TABSTOP | BS_OWNERDRAW, x0, y0, COLW, 24, g_font);
        } else {
            int cw2 = f->kind == FF_CHOICE ? 112 : 64;
            HWND lab = ff_ctl(ID_FF_LABEL + i, ST, wl, 0x4000 /* SS_ENDELLIPSIS */ | 0x100 /* SS_NOTIFY */, x0, y0 + 4, COLW - cw2 - 6, 20, g_font);
            ff_tip(lab, 0, 0, f->help);
            if (f->kind == FF_CHOICE) {
                g_ffc[i] = ff_ctl(ID_FF_CTL + i, CB, WTEXT(""), WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, x0 + COLW - cw2, y0, cw2, 160, g_font);
                if (theme && T.dark) theme(g_ffc[i], WTEXT("DarkMode_CFD"), 0);
                const char *p = f->choices;
                while (p) {
                    const char *e = strchr(p, '|');
                    char one[32];
                    int l = e ? (int)(e - p) : (int)strlen(p);
                    if (l > 31) l = 31;
                    memcpy(one, p, (size_t)l);
                    one[l] = 0;
                    WCHAR wo[32];
                    SendMessageW(g_ffc[i], CB_ADDSTRING, 0, (LPARAM)wide(one, wo, 32));
                    p = e ? e + 1 : 0;
                }
            } else {
                if (g_nffbox < 64) g_ffbox[g_nffbox++] = (RECT){ x0 + COLW - cw2, y0, x0 + COLW, y0 + 24 };
                g_ffc[i] = ff_ctl(ID_FF_CTL + i, E, WTEXT(""), WS_TABSTOP | ES_AUTOHSCROLL, x0 + COLW - cw2 + 7, y0 + 4, cw2 - 14, 16, g_font);
                SendMessageW(g_ffc[i], EM_SETLIMITTEXT, 10, 0);
            }
        }
        ff_tip(g_ffc[i], 0, 0, f->help);
        ff_tip(g_ff, &row_area, (uintptr_t)(100 + i), f->help); /* also when it's greyed out */
    }
    int by = Y0 + rows * ROW + 18;
    ff_ctl(ID_FF_FOOT, ST, WTEXT("Fusion Fix is by ThirteenAG. Changes count from the next start of Blacklist."), 0, X0, by + 8, 520, 20, g_font);
    const DWORD OD = WS_TABSTOP | BS_OWNERDRAW;
    ff_ctl(ID_FF_DEFAULTS, B, WTEXT("Defaults"), OD, cw - X0 - 100 * 3 - 16, by, 100, 32, g_font);
    ff_ctl(ID_FF_CANCEL, B, WTEXT("Cancel"), OD, cw - X0 - 100 * 2 - 8, by, 100, 32, g_font);
    ff_ctl(ID_FF_SAVE, B, WTEXT("Save"), OD, cw - X0 - 100, by, 100, 32, g_bold);
    ff_fill(false);
    ff_lock_shared();
    ff_dark_title();
    EnableWindow(g_wnd, 0);
    ShowWindow(g_ff, 5);
}

static void on_command(int id, int code)
{
    switch (id) {
    case ID_HOST: push_cmd(CMD_HOST, 0); show_tab(TAB_LOG); break; /* the setup steps show in the log */
    case ID_JOIN: push_cmd(CMD_JOIN, 0); break;
    case ID_STOP: push_cmd(CMD_STOP, 0); break;
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
        if (g_shown.dll_ok && g_shown.mode != MODE_HOST && !g_shown.lobby && SendMessageW(g_c[ID_LOBBY], WM_GETTEXTLENGTH, 0, 0) > 0)
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
                                         "skips the intro, wide screens, unlocks the DLC and the 4th Echelon missions. It is downloaded and checked (SHA-256). Change it with \"Game settings\"."),
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
    case ID_FFSET: ff_open(); break;
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
    case WM_ERASEBKGND: {
        RECT r;
        GetClientRect(w, &r);
        FillRect((HDC)wp, &r, g_br_bg);
        draw_frames((HDC)wp);
        return 1;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        return control_colors(m, (HDC)wp, (HWND)lp);
    case WM_MEASUREITEM: {
        MeasureItem *mi = (MeasureItem *)lp;
        if (mi->type == ODT_LISTBOX) mi->height = (UINT)S_(24);
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
        ff_close();
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
        int fit = GetSystemMetrics(62) - 16;
        int need = BOX_Y + BOX_H + 14 + 32;
        if (fit > 300 && need * g_dpi / 96 > fit) g_dpi = fit * 96 / need;
        if (g_dpi < 72) g_dpi = 72;
    }
    const WCHAR *face = WTEXT("Segoe UI");
    g_font = CreateFontW(-S_(12), 0, 0, 0, FW_NORMAL, 0, 0, 0, 1, 0, 0, CLEARTYPE_QUALITY, 0, face);
    g_bold = CreateFontW(-S_(12), 0, 0, 0, FW_BOLD, 0, 0, 0, 1, 0, 0, CLEARTYPE_QUALITY, 0, face);
    g_big = CreateFontW(-S_(16), 0, 0, 0, FW_BOLD, 0, 0, 0, 1, 0, 0, CLEARTYPE_QUALITY, 0, face);

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
    RECT r = { 0, 0, S_(600), S_(BOX_Y + BOX_H + 14) };
    AdjustWindowRectEx(&r, style, 0, 0);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    int x = (GetSystemMetrics(0) - ww) / 2, y = (GetSystemMetrics(1) - wh) / 2;
    g_wnd = CreateWindowExW(0, wc.className, 
#ifdef SCBL_DEV
                            WTEXT("SCBLLink " SCBLLINK_VERSION " DEV - Splinter Cell: Blacklist")
#else
                            WTEXT("SCBLLink " SCBLLINK_VERSION " - Splinter Cell: Blacklist")
#endif
                            , style,
                            x < 0 ? 0 : x, y < 0 ? 0 : y, ww, wh, 0, 0, inst, 0);
    if (!g_wnd) return 1;
    build();
    set_theme(T.dark);
    show_tab(TAB_HOWTO);
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
        if (g_ff && IsDialogMessageW(g_ff, &msg)) continue;
        if (IsDialogMessageW(g_wnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
