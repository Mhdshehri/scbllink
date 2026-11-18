#!/bin/sh
# Build SCBLLink's game DLL, uplay_r1_loader.dll (32-bit, like the game), with
# clang + lld only, the same way as SCBLLink.exe (build_windows.sh). By Mhdshehri.
set -eu
cd "$(dirname "$0")"
OUT=build/dll
mkdir -p "$OUT/lib"

mkdef() { # name, then exports
    name=$1; shift
    { echo "LIBRARY $name.dll"; echo "EXPORTS"; for e in "$@"; do echo "$e"; done; } > "$OUT/lib/$name.def"
    llvm-dlltool -m i386 -k -d "$OUT/lib/$name.def" -l "$OUT/lib/lib$name.a"
}
mkdef kernel32 CreateFileA@28 ReadFile@20 WriteFile@20 CloseHandle@4 WaitNamedPipeA@8 \
    SetNamedPipeHandleState@16 PeekNamedPipe@24 CreateThread@24 Sleep@4 GetTickCount@0 \
    InitializeCriticalSection@4 EnterCriticalSection@4 LeaveCriticalSection@4 GetModuleHandleA@4 \
    GetProcAddress@8 LoadLibraryA@4 GetModuleFileNameA@12 VirtualProtect@16 FlushInstructionCache@12 \
    GetCurrentProcess@0 GetCurrentProcessId@0 GetCurrentThreadId@0 CreateDirectoryA@8 GetFileAttributesA@4 CopyFileA@12 \
    GetLocalTime@4 DisableThreadLibraryCalls@4 AddVectoredExceptionHandler@8 FindFirstFileA@8 FindNextFileA@8 FindClose@4 GetCommandLineA@0
mkdef ws2_32 gethostname@8 bind@12
mkdef user32 CreateWindowExA@48 DestroyWindow@4 GetAsyncKeyState@4 GetForegroundWindow@0 GetWindowThreadProcessId@8 FillRect@12
mkdef gdi32 CreateCompatibleDC@4 CreateDIBSection@24 SelectObject@8 DeleteObject@4 DeleteDC@4 CreateSolidBrush@4 CreateFontA@56 \
    SetTextColor@8 SetBkMode@8 TextOutA@20 GdiFlush@0
mkdef msvcrt _vsnprintf strlen strcmp strncmp strchr strstr memcpy memmove memset memcmp \
    malloc calloc realloc free fopen fread fwrite fclose fseek ftell fflush getenv remove

CFLAGS="--target=i686-w64-windows-gnu -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter
  -ffreestanding -fno-stack-protector -fno-asynchronous-unwind-tables
  -fno-ident -fno-builtin -D_WIN32"

# shellcheck disable=SC2086
clang $CFLAGS -fuse-ld=lld -nostdlib -nostartfiles -shared \
    -Wl,-e,_DllMain@12 -Wl,--dynamicbase -Wl,--nxcompat -Wl,--no-insert-timestamp -Wl,--build-id=none \
    -o "$OUT/uplay_r1_loader.dll" dll/uplay.c dll/link.c dll/hook.c dll/overlay.c src/chkstk.c dll/uplay.def \
    -L"$OUT/lib" -lkernel32 -lws2_32 -luser32 -lgdi32 -lmsvcrt
echo "built $OUT/uplay_r1_loader.dll"
sha256sum "$OUT/uplay_r1_loader.dll"
