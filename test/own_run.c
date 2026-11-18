/* own_run.c - runs ownsrv.c (all four ports) on 127.0.0.1 for tests. Needs root for port 80. */
#include "../src/ownsrv.h"
#include "../src/plat.h"
#include <arpa/inet.h>
static void logline(const char *s) { fprintf(stderr, "%s\n", s); }
static bool login(const char *n, const char *p) { return n[0] && p[0]; }
int main(void)
{
    if (!own_start(inet_addr("127.0.0.1"), inet_addr("10.237.51.200"), login, 0, logline, false)) return 1;
    for (;;) { own_poll(); sleep_ms(2); }
}
