/* w32.h - the few Windows pieces SCBLLink's game DLL uses, declared by hand
 * (no Windows SDK). 32-bit only, like the game. By Mhdshehri. */
#pragma once
#include "../src/mini.h"

#define WINAPI __attribute__((stdcall))
#define IMP __declspec(dllimport)

typedef void *HANDLE;
typedef void *HMODULE;
typedef unsigned long DWORD;
typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;

#define INVALID_HANDLE ((HANDLE)(intptr_t)-1)
#define GENERIC_READ 0x80000000UL
#define GENERIC_WRITE 0x40000000UL
#define OPEN_EXISTING 3
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_READWRITE 0x04

typedef struct { void *a, *b; long c; long d; HANDLE e; uintptr_t f; } CRITICAL_SECTION; /* 24 bytes on x86 */

IMP HANDLE WINAPI CreateFileA(const char *name, DWORD access, DWORD share, void *sa, DWORD disp, DWORD flags, HANDLE tmpl);
IMP BOOL WINAPI ReadFile(HANDLE h, void *buf, DWORD n, DWORD *got, void *ov);
IMP BOOL WINAPI WriteFile(HANDLE h, const void *buf, DWORD n, DWORD *put, void *ov);
IMP BOOL WINAPI CloseHandle(HANDLE h);
IMP BOOL WINAPI WaitNamedPipeA(const char *name, DWORD ms);
IMP BOOL WINAPI SetNamedPipeHandleState(HANDLE h, DWORD *mode, DWORD *maxc, DWORD *timeout);
IMP BOOL WINAPI PeekNamedPipe(HANDLE h, void *buf, DWORD n, DWORD *read, DWORD *avail, DWORD *left);
IMP HANDLE WINAPI CreateThread(void *sa, size_t stack, DWORD(WINAPI *fn)(void *), void *arg, DWORD flags, DWORD *id);
IMP void WINAPI Sleep(DWORD ms);
IMP DWORD WINAPI GetTickCount(void);
IMP void WINAPI InitializeCriticalSection(CRITICAL_SECTION *c);
IMP void WINAPI EnterCriticalSection(CRITICAL_SECTION *c);
IMP void WINAPI LeaveCriticalSection(CRITICAL_SECTION *c);
IMP HMODULE WINAPI GetModuleHandleA(const char *name);
IMP void *WINAPI GetProcAddress(HMODULE m, const char *name);
IMP HMODULE WINAPI LoadLibraryA(const char *name);
IMP DWORD WINAPI GetModuleFileNameA(HMODULE m, char *buf, DWORD n);
IMP BOOL WINAPI VirtualProtect(void *p, size_t n, DWORD prot, DWORD *old);
IMP BOOL WINAPI FlushInstructionCache(HANDLE proc, const void *p, size_t n);
IMP HANDLE WINAPI GetCurrentProcess(void);
IMP DWORD WINAPI GetCurrentProcessId(void);
IMP DWORD WINAPI GetCurrentThreadId(void);
IMP BOOL WINAPI CreateDirectoryA(const char *path, void *sa);
IMP DWORD WINAPI GetFileAttributesA(const char *path);
IMP BOOL WINAPI CopyFileA(const char *from, const char *to, BOOL fail_if_exists);
IMP void WINAPI GetLocalTime(WORD *st /* SYSTEMTIME, 8 words */);
IMP BOOL WINAPI DisableThreadLibraryCalls(HMODULE m);

typedef struct {
    DWORD attrs;
    DWORD ctime[2], atime[2], wtime[2];
    DWORD size_hi, size_lo;
    DWORD r0, r1;
    char name[260];
    char alt[14];
} FIND_DATAA;
IMP HANDLE WINAPI FindFirstFileA(const char *pattern, FIND_DATAA *d);
IMP BOOL WINAPI FindNextFileA(HANDLE h, FIND_DATAA *d);
IMP BOOL WINAPI FindClose(HANDLE h);

/* winsock: only the shapes our hooks touch */
typedef struct hostent {
    char *h_name;
    char **h_aliases;
    short h_addrtype, h_length;
    char **h_addr_list;
} HOSTENT;
IMP int WINAPI gethostname(char *name, int n);
typedef struct { unsigned short family, port; uint32_t addr; char zero[8]; } SOCKADDR_IN;
IMP const char *WINAPI GetCommandLineA(void);

/* iphlpapi: IP_ADAPTER_INFO, the fields up to IpAddressList (that's all we read) */
typedef struct IP_ADDR_STRING {
    struct IP_ADDR_STRING *next;
    char ip[16], mask[16];
    DWORD context;
} IP_ADDR_STRING;
typedef struct IP_ADAPTER_INFO {
    struct IP_ADAPTER_INFO *next;
    DWORD combo_index;
    char name[260];
    char description[132];
    unsigned address_length;
    BYTE address[8];
    DWORD index;
    unsigned type, dhcp_enabled;
    IP_ADDR_STRING *current;
    IP_ADDR_STRING ip_list;
    /* more follows */
} IP_ADAPTER_INFO;
