/* srv_udp.c - runs src/srv.c on Linux for tests: UDP on 127.0.0.1, ports from the command line.
 * usage: srv_udp auth_port secure_port */
#include "../src/srv.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
static int fd[3];
static void out(int which, uint32_t ip, uint16_t port, const uint8_t *d, int n)
{
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(port), .sin_addr.s_addr = ip };
    sendto(fd[which], d, (size_t)n, 0, (struct sockaddr *)&a, sizeof a);
}
static void logline(const char *s) { fprintf(stderr, "%s\n", s); }
static void rnd(uint8_t *o, int n) { FILE *f = fopen("/dev/urandom", "rb"); if (fread(o, 1, (size_t)n, f) != (size_t)n) exit(2); fclose(f); }
static uint64_t unix_time(void) { return (uint64_t)time(0); }
static uint64_t ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000; }
static bool ubi_name(const char *ubi, char name[64]) { if (strcmp(ubi, "7e577e577e577e577e577e577e577e57")) return false; strcpy(name, "test"); return true; }
static bool login(const char *name, const char *pass) { return strcmp(name, "Stranger") != 0 && pass[0]; }
int main(int argc, char **argv)
{
    if (argc < 3) return 1;
    for (int i = 0; i < 3; i++) { /* the third: test commands ("INVITE from to") */
        fd[i] = socket(AF_INET, SOCK_DGRAM, 0);
        int portno = i < 2 ? atoi(argv[1 + i]) : atoi(argv[2]) + 100;
        struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons((uint16_t)portno), .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
        if (bind(fd[i], (struct sockaddr *)&a, sizeof a)) { perror("bind"); return 1; }
    }
    SrvHost h = { .ip = inet_addr("10.237.51.200"), .send = out, .log = logline, .random = rnd, .login = login, .ubi_name = ubi_name, .unix_time = unix_time, .verbose = true };
    srv_start(&h);
    for (;;) {
        struct pollfd p[3] = { { fd[0], POLLIN, 0 }, { fd[1], POLLIN, 0 }, { fd[2], POLLIN, 0 } };
        poll(p, 3, 50);
        if (p[2].revents & POLLIN) {
            char b[200], from[64], to[64];
            ssize_t n = recv(fd[2], b, sizeof b - 1, 0);
            if (n > 0) { b[n] = 0; if (sscanf(b, "INVITE %63s %63s", from, to) == 2) srv_invite(from, to, ms()); }
        }
        for (int i = 0; i < 2; i++) {
            if (!(p[i].revents & POLLIN)) continue;
            uint8_t b[2000];
            struct sockaddr_in a; socklen_t al = sizeof a;
            ssize_t n = recvfrom(fd[i], b, sizeof b, 0, (struct sockaddr *)&a, &al);
            if (n > 0) srv_packet(i, a.sin_addr.s_addr, ntohs(a.sin_port), b, (int)n, ms());
        }
        srv_tick(ms());
    }
}
