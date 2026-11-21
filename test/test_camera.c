/* test_camera.c - the Crosshair choice (src/camera.c).
 * gcc -fsanitize=address,undefined -g -Isrc -o /tmp/tcam test/test_camera.c src/camera.c && /tmp/tcam [captured-block camera-original.ini]
 * The game's own file is never in the repo: the checks use a made-up file of the same shape;
 * given paths, a block caught from the game and its known text are checked too. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "camera.h"

static char *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = calloc(1, (size_t)l + 1);
    assert(fread(b, 1, (size_t)l, f) == (size_t)l);
    fclose(f);
    if (n) *n = (size_t)l;
    return b;
}

static int count(const char *s, const char *w) { int n = 0; for (const char *p = strstr(s, w); p; p = strstr(p + 1, w)) n++; return n; }

/* a made-up camera config of the game's shape: 730 sections, the reticle lines where the game has them */
static char *fake(void)
{
    char *s = calloc(1, 200000);
    strcpy(s, "// This file uses 8 as Tab stop value. Please respect this guideline when editing it.\r\n\r\n[Echelon.ECamera]\r\nm_runThreshold=241\r\n");
    const char *named[][2] = {
        { "[Default]", "reticle=\tt==\tv=1\t\t\t// display reticle?\r\n" },
        { "[_Zoom]", "FOV=\tt=*\tv=0.5\r\n" },
        { "[_Scope]", "reticle=\tt==\tv=0\r\n" },
        { "[Back2Wall]", "autoAlignTimeAfterGeoJump=\tt==\tv=0.5\r\n" },
        { "[DownedShooting]", "reticle=\tt==\tv=1\t\t\t// display reticle?\r\n" },
        { "[RemoteSniper]", "reticle=\tt==\tv=1\t\t\t// display reticle?\r\n" },
        { "[TurretCam]", "reticle=\tt==\tv=1\t\t\t// display reticle?\r\n" },
        { "[COOP_LDE_Initiator]", "reticle=\tt==\tv=0\r\nFOV=\tt=*\tv=0.5\r\nreticle=\tt==\tv=1\r\n" },   /* one section, two lines (as the game) */
        { "[COOP_LDE_Confirmer]", "reticle=\tt==\tv=0\r\nFOV=\tt=*\tv=0.5\r\nreticle=\tt==\tv=1\r\n" },
        { "[ModeX1]", "FOV=\tt=*\tv=0.5\r\n" },
        { "[ModeX2]", "FOV=\tt=*\tv=0.5\r\n" },
        { "[Hub]", "reticle=\tt==\tv=1\r\n" },
        { "[HubClose]", "reticle=\tt==\tv=1\r\n" },
    };
    int k = 1;
    for (size_t i = 0; i < sizeof named / sizeof named[0]; i++, k++) { strcat(s, "\r\n"); strcat(s, named[i][0]); strcat(s, "\r\n"); strcat(s, named[i][1]); }
    for (; k < 730; k++) { char l[64]; snprintf(l, sizeof l, "\r\n[Mode%d]\r\nFOV=\tt=*\tv=0.5\r\n", k); strcat(s, l); }
    return s;
}

static void check_off(const char *orig, const char *off)
{
    assert(off && cam_ours(off) && !cam_ours(orig));
    /* 6 turned off, 2 added; the drone, turret, remote sniper ones stay on */
    assert(count(off, "v=1") == count(orig, "v=1") - 6);
    assert(count(off, "(SCBLLink: crosshair off)") == 2);
    assert(strstr(off, "[_Zoom]\r\nreticle=\tt==\tv=0"));
    assert(strstr(off, "[Back2Wall]\r\nreticle=\tt==\tv=0"));
    /* back the other way it's the original again */
    char *b = strdup(off + strlen(CAM_MARK) + 2);
    for (char *p; (p = strstr(b, "reticle=\tt==\tv=0\t\t\t// display reticle? (SCBLLink: crosshair off)\r\n"));)
        memmove(p, p + strlen("reticle=\tt==\tv=0\t\t\t// display reticle? (SCBLLink: crosshair off)\r\n"), strlen(p) - strlen("reticle=\tt==\tv=0\t\t\t// display reticle? (SCBLLink: crosshair off)\r\n") + 1);
    assert(strlen(b) == strlen(orig));
    int diffs = 0;
    for (size_t i = 0; b[i]; i++) if (b[i] != orig[i]) { assert(b[i] == '0' && orig[i] == '1'); diffs++; }
    assert(diffs == 6);
    free(b);
}

int main(int argc, char **argv)
{
    /* the split-in-place text comes back */
    const char split[] = "\x01\x00\x00\x00\xf2\xff\x04\x00// head\x00\n\r\n[Echelon.ECamera\x00\x00\nm_a\x00" "0.2\x00 //c\x00\nm_b\x00" "241\x00\n// note\x00\n\x00\x00";
    char *u = cfg_unsplit((const uint8_t *)split, sizeof split - 1, "[Echelon.ECamera");
    if (!u || strcmp(u, "// head\r\n\r\n[Echelon.ECamera]\r\nm_a=0.2  //c\r\nm_b=241\r\n// note\r\n")) {
        for (const char *q = u; q && *q; q++) printf(*q == '\r' ? "\\r" : *q == '\n' ? "\\n\n" : "%c", *q);
        assert(!"unsplit");
    }
    free(u);
    assert(!cfg_unsplit((const uint8_t *)split, sizeof split - 1, "[Nothing"));

    char *o = fake();
    char *off = cam_crosshair_off(o);
    check_off(o, off);
    assert(!cam_crosshair_off(off));                 /* ours is never a source */
    free(off);
    char *b = strdup(o);                             /* a section short */
    memcpy(strstr(b, "[Mode700]"), "xMode700]", 9);
    assert(!cam_crosshair_off(b)); free(b);
    b = strdup(o);                                   /* the Hub's crosshair already off */
    *(strstr(b, "[Hub]\r\nreticle=\tt==\tv=1") + 20) = '0';
    assert(!cam_crosshair_off(b)); free(b);
    b = strdup(o);                                   /* not the game's file */
    b[3] = 'X';
    assert(!cam_crosshair_off(b)); free(b);
    assert(!cam_crosshair_off(0) && !cam_ours(0));
    free(o);

    if (argc > 2) {   /* a block caught from the game, and the text rebuilt from it before */
        size_t n;
        char *blk = slurp(argv[1], &n), *known = slurp(argv[2], 0);
        char *t = cfg_unsplit((const uint8_t *)blk, n, "[Echelon.ECamera");
        assert(t && !strcmp(t, known));
        char *x = cam_crosshair_off(t);
        check_off(t, x);
        printf("real: %zu bytes caught, %zu of text, %zu written\n", n, strlen(t), strlen(x));
        free(x); free(t); free(blk); free(known);
    }
    puts("all camera tests passed");
    return 0;
}
