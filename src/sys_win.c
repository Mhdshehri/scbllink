/* sys_win.c - see sys.h. */
#include "sys.h"

#define WINAPI __attribute__((stdcall))
#define IMP __declspec(dllimport)
typedef void *HANDLE;
typedef int BOOL;
typedef unsigned long DWORD;
typedef struct { DWORD cb; char *reserved, *desktop, *title; DWORD x, y, w, h, cx, cy, fill, flags;
                 uint16_t show, reserved2; void *reserved3; HANDLE in, out, err; } StartupInfoA;
typedef struct { HANDLE process, thread; DWORD pid, tid; } ProcInfo;
IMP BOOL WINAPI CreateProcessA(const char *app, char *cmd, void *pa, void *ta, BOOL inherit, DWORD flags,
                               void *env, const char *dir, StartupInfoA *si, ProcInfo *pi);
IMP BOOL WINAPI CloseHandle(HANDLE h);
IMP DWORD WINAPI WaitForSingleObject(HANDLE h, DWORD ms);
IMP BOOL WINAPI GetExitCodeProcess(HANDLE h, DWORD *code);
IMP BOOL WINAPI TerminateProcess(HANDLE h, unsigned int code);
IMP HANDLE WINAPI CreateThread(void *sa, size_t stack, DWORD (WINAPI *fn)(void *), void *arg, DWORD flags, DWORD *tid);
IMP long WINAPI RegGetValueA(void *key, const char *sub, const char *value, DWORD flags, DWORD *type, void *data, DWORD *len);

#define CREATE_NO_WINDOW 0x08000000
#define STARTF_USESHOWWINDOW 1
#define HKEY_LOCAL_MACHINE ((void *)(uintptr_t)0x80000002)
#define RRF_RT_REG_SZ 0x2
#define RRF_SUBKEY_WOW6432KEY 0x20000
#define RRF_SUBKEY_WOW6464KEY 0x10000

static bool start(const char *app, char *cmd, const char *dir, bool hidden, ProcInfo *pi)
{
    StartupInfoA si;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    if (hidden) { si.flags = STARTF_USESHOWWINDOW; si.show = 0; }
    memset(pi, 0, sizeof *pi);
    return CreateProcessA(app, cmd, 0, 0, 0, hidden ? CREATE_NO_WINDOW : 0, 0, dir, &si, pi) != 0;
}

int run_hidden(const char *cmdline, int timeout_ms)
{
    static char cmd[2048];
    sfmt(cmd, sizeof cmd, "%s", cmdline);
    ProcInfo pi;
    if (!start(0, cmd, 0, true, &pi)) return -1;
    CloseHandle(pi.thread);
    DWORD code = (DWORD)-1;
    if (WaitForSingleObject(pi.process, (DWORD)timeout_ms) != 0) { TerminateProcess(pi.process, 1); code = (DWORD)-1; }
    else if (!GetExitCodeProcess(pi.process, &code)) code = (DWORD)-1;
    CloseHandle(pi.process);
    return (int)code;
}

proc_t proc_start(const char *exe, const char *args, const char *dir, bool hidden)
{
    static char cmd[2048];
    sfmt(cmd, sizeof cmd, "\"%s\" %s", exe, args ? args : "");
    ProcInfo pi;
    if (!start(exe, cmd, dir, hidden, &pi)) return 0;
    CloseHandle(pi.thread);
    return pi.process;
}

bool proc_alive(proc_t p) { return p && WaitForSingleObject(p, 0) != 0; }

void proc_stop(proc_t p)
{
    if (!p) return;
    if (WaitForSingleObject(p, 0) != 0) { TerminateProcess(p, 0); WaitForSingleObject(p, 3000); }
    CloseHandle(p);
}

bool reg_read_hklm(const char *subkey, const char *value, char *out, int n)
{
    DWORD views[2] = { RRF_SUBKEY_WOW6432KEY, RRF_SUBKEY_WOW6464KEY };
    for (int i = 0; i < 2; i++) {
        DWORD len = (DWORD)n;
        if (RegGetValueA(HKEY_LOCAL_MACHINE, subkey, value, RRF_RT_REG_SZ | views[i], 0, out, &len) == 0 && out[0]) return true;
    }
    out[0] = 0;
    return false;
}

char *file_read_all(const char *path, int *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int cap = 1 << 16, n = 0;
    char *b = malloc((size_t)cap + 1);
    while (b) {
        size_t r = fread(b + n, 1, (size_t)(cap - n), f);
        n += (int)r;
        if (n < cap) break;
        if (cap >= (8 << 20)) break; /* config files are small */
        cap *= 2;
        char *nb = realloc(b, (size_t)cap + 1);
        if (!nb) { free(b); b = 0; }
        b = nb;
    }
    fclose(f);
    if (!b) return 0;
    b[n] = 0;
    if (len) *len = n;
    return b;
}

typedef struct { void (*fn)(void *); void *arg; } ThreadArg;
static DWORD WINAPI thread_main(void *p)
{
    ThreadArg a = *(ThreadArg *)p;
    free(p);
    a.fn(a.arg);
    return 0;
}

bool thread_start(void (*fn)(void *), void *arg)
{
    ThreadArg *a = malloc(sizeof *a);
    if (!a) return false;
    a->fn = fn;
    a->arg = arg;
    HANDLE h = CreateThread(0, 0, thread_main, a, 0, 0);
    if (!h) { free(a); return false; }
    CloseHandle(h);
    return true;
}

bool file_copy(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb");
    if (!in) return false;
    char tmp[1024];
    sfmt(tmp, sizeof tmp, "%s.scbllink-tmp", to);
    FILE *out = fopen(tmp, "wb");
    if (!out) { fclose(in); return false; }
    static char buf[65536];
    bool ok = true;
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0)
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
    fclose(in);
    if (fclose(out) != 0) ok = false;
    if (!ok) { remove(tmp); return false; }
    remove(to);
    if (rename(tmp, to) != 0) { remove(tmp); return false; }
    return true;
}

IMP BOOL WINAPI SystemFunction036(void *buf, unsigned long len); /* RtlGenRandom */
bool random_bytes(void *p, int n) { return n > 0 && SystemFunction036(p, (unsigned long)n) != 0; }
