/* gamepipe_win.c - see gamepipe.h. The pipe takes connections from this PC
 * only, is SCBLLink's alone (first instance), and lets the logged-in user's
 * game (not elevated, unlike SCBLLink) read and write it. By Mhdshehri. */
#include "gamepipe.h"
#include "sys.h"

#define WINAPI __attribute__((stdcall))
#define IMP __declspec(dllimport)
typedef void *HANDLE;
typedef int BOOL;
typedef unsigned long DWORD;
typedef struct { DWORD len; void *sd; BOOL inherit; } SecAttrs;
typedef struct { void *a, *b; long c; long d; HANDLE e; uintptr_t f; } CritSec;

IMP HANDLE WINAPI CreateNamedPipeA(const char *name, DWORD open, DWORD mode, DWORD max, DWORD out, DWORD in, DWORD timeout, SecAttrs *sa);
IMP BOOL WINAPI ConnectNamedPipe(HANDLE h, void *ov);
IMP BOOL WINAPI DisconnectNamedPipe(HANDLE h);
IMP BOOL WINAPI PeekNamedPipe(HANDLE h, void *buf, DWORD n, DWORD *read, DWORD *avail, DWORD *left);
IMP BOOL WINAPI ReadFile(HANDLE h, void *buf, DWORD n, DWORD *got, void *ov);
IMP BOOL WINAPI WriteFile(HANDLE h, const void *buf, DWORD n, DWORD *put, void *ov);
IMP BOOL WINAPI CloseHandle(HANDLE h);
IMP DWORD WINAPI GetLastError(void);
IMP void WINAPI Sleep(DWORD ms);
IMP void WINAPI InitializeCriticalSection(CritSec *c);
IMP void WINAPI EnterCriticalSection(CritSec *c);
IMP void WINAPI LeaveCriticalSection(CritSec *c);
IMP BOOL WINAPI ConvertStringSecurityDescriptorToSecurityDescriptorA(const char *s, DWORD rev, void **sd, DWORD *len);

#define PIPE_NAME "\\\\.\\pipe\\scbllink-game"
#define INVALID ((HANDLE)(intptr_t)-1)
#define LINE_MAX 4096
#define QUEUE 64

/* pipe 0: the game; pipe 1 (developer build): a second game on this PC, as a second player */
typedef struct { CritSec cs; HANDLE pipe; volatile bool conn; char *q[QUEUE]; int qh, qn; bool started; } Pipe;
static Pipe P[2] = { { .pipe = INVALID }, { .pipe = INVALID } };
#define g_cs (P[i].cs)
#define g_pipe (P[i].pipe)
#define g_conn (P[i].conn)
#define g_q (P[i].q)
#define g_qh (P[i].qh)
#define g_qn (P[i].qn)

static void push(int i, const char *line)
{
    char *c = malloc(strlen(line) + 1);
    if (!c) return;
    memcpy(c, line, strlen(line) + 1);
    EnterCriticalSection(&g_cs);
    if (g_qn < QUEUE) { g_q[(g_qh + g_qn) % QUEUE] = c; g_qn++; c = 0; }
    LeaveCriticalSection(&g_cs);
    free(c); /* the queue was full: dropped */
}

bool gpx_take(int i, char *line, int n)
{
    line[0] = 0;
    if (!P[i].started) return false;
    char *c = 0;
    EnterCriticalSection(&g_cs);
    if (g_qn) { c = g_q[g_qh]; g_qh = (g_qh + 1) % QUEUE; g_qn--; }
    LeaveCriticalSection(&g_cs);
    line[0] = 0;
    if (!c) return false;
    sfmt(line, (size_t)n, "%s", c);
    free(c);
    return true;
}

bool gpx_connected(int i) { return P[i].started && g_conn; }

static void send_line(int i, const char *fmt, va_list ap)
{
    if (!P[i].started) return;
    char line[2048]; /* the longest line (a friend with a session) is about 1100 */
    _vsnprintf(line, sizeof line - 2, fmt, ap);
    line[sizeof line - 2] = 0;
    size_t n = strlen(line);
    line[n++] = '\n';
    EnterCriticalSection(&g_cs);
    if (g_conn && g_pipe != INVALID) {
        DWORD put = 0;
        WriteFile(g_pipe, line, (DWORD)n, &put, 0);
    }
    LeaveCriticalSection(&g_cs);
}

void gpx_send(int i, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    send_line(i, fmt, ap);
    va_end(ap);
}

void gp_send(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    send_line(0, fmt, ap);
    va_end(ap);
}

bool gp_take(char *line, int n) { return gpx_take(0, line, n); }
bool gp_connected(void) { return gpx_connected(0); }

static void serve(void *arg)
{
    int i = (int)(intptr_t)arg;
    /* SYSTEM and administrators: everything; the signed-in user (the game): read and write.
       Low integrity can't write. */
    void *sd = 0;
    DWORD sdl = 0;
    ConvertStringSecurityDescriptorToSecurityDescriptorA("D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;IU)S:(ML;;NW;;;LW)", 1, &sd, &sdl);
    SecAttrs sa = { sizeof sa, sd, 0 };
    static char bufs[2][LINE_MAX];
    char *buf = bufs[i];
    for (;;) {
        /* duplex, first instance (nobody else may make this pipe), byte stream, this PC only */
        HANDLE h = CreateNamedPipeA(i ? PIPE_NAME "-2" : PIPE_NAME, 0x3 | 0x00080000, 0x0 | 0x8, 1, 65536, 65536, 0, sd ? &sa : 0);
        if (h == INVALID) { Sleep(2000); continue; }
        if (!ConnectNamedPipe(h, 0) && GetLastError() != 535 /* already connected */) { CloseHandle(h); Sleep(500); continue; }
        EnterCriticalSection(&g_cs);
        g_pipe = h;
        g_conn = true;
        LeaveCriticalSection(&g_cs);
        push(i, "CONNECTED");
        int n = 0;
        bool skipping = false;
        for (;;) {
            DWORD avail = 0, got = 0;
            char c[512];
            if (!PeekNamedPipe(h, 0, 0, 0, &avail, 0)) break;
            if (avail == 0) { Sleep(30); continue; }
            if (!ReadFile(h, c, avail < sizeof c ? avail : sizeof c, &got, 0) || got == 0) break;
            for (DWORD b = 0; b < got; b++) { /* b, not i: i is the pipe */
                if (c[b] == '\n') {
                    if (!skipping) { buf[n] = 0; push(i, buf); }
                    n = 0;
                    skipping = false;
                } else if (!skipping) {
                    if (n < LINE_MAX - 1) buf[n++] = c[b];
                    else skipping = true;
                }
            }
        }
        EnterCriticalSection(&g_cs);
        g_conn = false;
        g_pipe = INVALID;
        LeaveCriticalSection(&g_cs);
        DisconnectNamedPipe(h);
        CloseHandle(h);
        push(i, "GONE");
    }
}

void gpx_start(int i)
{
    if (P[i].started) return;
    P[i].started = true;
    InitializeCriticalSection(&g_cs);
    thread_start(serve, (void *)(intptr_t)i);
}

void gp_start(void) { gpx_start(0); }
