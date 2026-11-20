/* sys.h - Windows pieces SCBLLink needs beyond plat.h: running a command
 * without a window, keeping a started program's handle, the registry. */
#pragma once
#include "mini.h"

/* Run a command line hidden and wait for it. Returns its exit code, or -1
   if it could not start or did not finish within timeout_ms. */
int run_hidden(const char *cmdline, int timeout_ms);

typedef void *proc_t;
proc_t proc_start(const char *exe, const char *args, const char *dir, bool hidden);
bool proc_alive(proc_t p);
void proc_stop(proc_t p);      /* ends it and frees the handle */

/* HKEY_LOCAL_MACHINE string value, 32- or 64-bit view. */
bool reg_read_hklm(const char *subkey, const char *value, char *out, int n);

char *file_read_all(const char *path, int *len); /* malloc'd, 0-terminated; NULL if missing */
bool documents_dir(char *out, int n); /* the user's Documents folder (where the game keeps its settings) */

/* Run fn(arg) on its own thread (for checks that may block on a socket). */
bool thread_start(void (*fn)(void *), void *arg);
bool file_copy(const char *from, const char *to);
bool random_bytes(void *p, int n);    /* from Windows' generator (RtlGenRandom) */
