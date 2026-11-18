/* wintun_win.c - see wintun.h. */
#include "wintun.h"
#include "plat.h"

#define WINAPI __attribute__((stdcall))
#define IMP __declspec(dllimport)
typedef void *HANDLE;
typedef unsigned long DWORD;
typedef uint16_t WCHAR;
typedef struct { uint32_t d1; uint16_t d2, d3; uint8_t d4[8]; } GUID;
IMP void *WINAPI LoadLibraryExA(const char *path, void *file, DWORD flags);
IMP void *WINAPI GetProcAddress(void *mod, const char *name);
IMP DWORD WINAPI GetLastError(void);
IMP DWORD WINAPI WaitForSingleObject(HANDLE h, DWORD ms);
#define LOAD_WITH_ALTERED_SEARCH_PATH 0x8
#define ERROR_NO_MORE_ITEMS 259

bool sha256_file(const char *path, char hex[65]);

/* Wintun's x86 wintun.dll files we accept. Empty = any file (test builds
   only: the hash is printed so it can be pinned here). */
/* Wintun 0.14.1, bin\x86\wintun.dll from wintun-0.14.1.zip (zip SHA-256 07c25618...24ef51,
   signed by WireGuard LLC). The x86 DLL also installs the amd64 driver on 64-bit Windows. */
static const char *const PINNED[] = { "d694fa46ab4cfebcb2632d094c7aa97278eef2f8052438621766d863ae98a931", 0 };

typedef void *(WINAPI *CreateAdapter_t)(const WCHAR *name, const WCHAR *type, const GUID *guid);
typedef void *(WINAPI *OpenAdapter_t)(const WCHAR *name);
typedef void (WINAPI *CloseAdapter_t)(void *adapter);
typedef DWORD (WINAPI *DriverVersion_t)(void);
typedef void *(WINAPI *StartSession_t)(void *adapter, DWORD capacity);
typedef void (WINAPI *EndSession_t)(void *session);
typedef HANDLE (WINAPI *ReadEvent_t)(void *session);
typedef uint8_t *(WINAPI *Receive_t)(void *session, DWORD *size);
typedef void (WINAPI *Release_t)(void *session, const uint8_t *p);
typedef uint8_t *(WINAPI *Alloc_t)(void *session, DWORD size);
typedef void (WINAPI *Send_t)(void *session, const uint8_t *p);

static CreateAdapter_t pCreate;
static OpenAdapter_t pOpen;
static CloseAdapter_t pClose;
static DriverVersion_t pVersion;
static StartSession_t pStart;
static EndSession_t pEnd;
static ReadEvent_t pEvent;
static Receive_t pRecv;
static Release_t pRelease;
static Alloc_t pAlloc;
static Send_t pSend;

static void *g_dll, *g_adapter, *g_session;
static HANDLE g_event;
static char g_hash[65];

/* "SCBLLink" as UTF-16 */
static const WCHAR NAME[] = { 'S', 'C', 'B', 'L', 'L', 'i', 'n', 'k', 0 };
/* A fixed id, so Windows reuses the same adapter (and its settings) every
   time instead of making "SCBLLink 2", "SCBLLink 3"... */
static const GUID ADAPTER_GUID = { 0x6b1d2a9eu, 0x51c4, 0x4f0a, { 0x9b, 0x3e, 0x2d, 0x71, 0x0c, 0x88, 0x5a, 0xe4 } };

const char *tun_dll_hash(void) { return g_hash; }
bool tun_is_open(void) { return g_session != 0; }

static const char *load_dll(void)
{
    static char path[700], err[900];
    if (g_dll) return 0;
    sfmt(path, sizeof path, "%s\\wintun.dll", exe_dir());
    if (!sha256_file(path, g_hash)) {
        sfmt(err, sizeof err, "wintun.dll is missing. Put the x86 wintun.dll from the Wintun zip next to SCBLLink.exe (%s).", exe_dir());
        return err;
    }
    if (PINNED[0]) {
        bool ok = false;
        for (int i = 0; PINNED[i]; i++) ok |= strcmp(PINNED[i], g_hash) == 0;
        if (!ok) {
            sfmt(err, sizeof err, "wintun.dll is not a known Wintun file, so it was NOT loaded (SHA-256 %s).", g_hash);
            return err;
        }
    }
    g_dll = LoadLibraryExA(path, 0, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!g_dll) return "wintun.dll could not be loaded (is it the x86 one?).";
#define LOAD(var, type, name) if (!(var = (type)GetProcAddress(g_dll, name))) return "wintun.dll has no " name "."
    LOAD(pCreate, CreateAdapter_t, "WintunCreateAdapter");
    LOAD(pOpen, OpenAdapter_t, "WintunOpenAdapter");
    LOAD(pClose, CloseAdapter_t, "WintunCloseAdapter");
    LOAD(pVersion, DriverVersion_t, "WintunGetRunningDriverVersion");
    LOAD(pStart, StartSession_t, "WintunStartSession");
    LOAD(pEnd, EndSession_t, "WintunEndSession");
    LOAD(pEvent, ReadEvent_t, "WintunGetReadWaitEvent");
    LOAD(pRecv, Receive_t, "WintunReceivePacket");
    LOAD(pRelease, Release_t, "WintunReleaseReceivePacket");
    LOAD(pAlloc, Alloc_t, "WintunAllocateSendPacket");
    LOAD(pSend, Send_t, "WintunSendPacket");
#undef LOAD
    return 0;
}

const char *tun_open(void)
{
    static char err[200];
    if (g_session) return 0;
    const char *e = load_dll();
    if (e) return e;
    if (!g_adapter) g_adapter = pCreate(NAME, NAME, &ADAPTER_GUID);
    if (!g_adapter) g_adapter = pOpen(NAME); /* left over from a crash */
    if (!g_adapter) {
        sfmt(err, sizeof err, "Could not create the SCBLLink network adapter (error %lu). SCBLLink must run as administrator.", GetLastError());
        return err;
    }
    g_session = pStart(g_adapter, 0x400000 /* 4 MB ring */);
    if (!g_session) {
        sfmt(err, sizeof err, "Could not start the SCBLLink adapter (error %lu).", GetLastError());
        pClose(g_adapter);
        g_adapter = 0;
        return err;
    }
    g_event = pEvent(g_session);
    return 0;
}

void tun_close(void)
{
    if (g_session) { pEnd(g_session); g_session = 0; g_event = 0; }
    if (g_adapter) { pClose(g_adapter); g_adapter = 0; }
}

int tun_recv(const uint8_t **p)
{
    if (!g_session) return 0;
    DWORD n = 0;
    uint8_t *d = pRecv(g_session, &n);
    if (!d) return 0; /* ERROR_NO_MORE_ITEMS, or the adapter went away */
    *p = d;
    return (int)n;
}

void tun_release(const uint8_t *p) { if (g_session && p) pRelease(g_session, p); }

bool tun_send(const uint8_t *p, int n)
{
    if (!g_session || n <= 0 || n > 0xFFFF) return false;
    uint8_t *d = pAlloc(g_session, (DWORD)n);
    if (!d) return false; /* ring full: drop, like a busy network would */
    memcpy(d, p, (size_t)n);
    pSend(g_session, d);
    return true;
}

void tun_wait(int ms)
{
    if (g_event) WaitForSingleObject(g_event, (DWORD)ms);
    else sleep_ms(ms);
}
