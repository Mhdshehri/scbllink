/* test stand-ins for sys.h on Linux */
#include "../src/sys.h"
#include <stdio.h>
#include <stdlib.h>
bool random_bytes(void *p, int n) { FILE *f = fopen("/dev/urandom", "rb"); if (!f) return false; size_t r = fread(p, 1, (size_t)n, f); fclose(f); return r == (size_t)n; }
char *file_read_all(const char *path, int *len)
{
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    char *b = malloc(1 << 20); int n = (int)fread(b, 1, (1 << 20) - 1, f); fclose(f); b[n] = 0; if (len) *len = n; return b;
}
