/* test_video.c - the game's own videoSettings.ini settings (motion blur) in fusion.c.
 * gcc -fsanitize=address,undefined -g -Isrc -o /tmp/tv test/test_video.c src/fusion.c && /tmp/tv */
#include "../src/fusion.h"
#include <assert.h>

static char *dup(const char *s) { char *r = malloc(strlen(s) + 1); strcpy(r, s); return r; }

int main(void)
{
    /* the shape of the game's file: "Key=Value", CRLF, no comments */
    const char *game = "[Version]\r\nVersion=63\r\n[WinDrv.WindowsClient]\r\nWindowStyleFinal=0\r\nUseVSync=1\r\n"
                       "UseMotionBlur=1\r\nUseBloom=1\r\nOcclusion_Off=2\r\nSSAO_Radius=25.0000\r\n[SystemValidation]\r\nvalidationRequired=0\r\n";
    int blur = -1, fov = -1;
    for (int i = 0; i < FF_NSETTINGS; i++) {
        if (!strcmp(FF_SETTINGS[i].key, "UseMotionBlur")) blur = i;
        if (!strcmp(FF_SETTINGS[i].key, "FOVFactor")) fov = i;
        assert(strcmp(FF_SETTINGS[i].key, "Occlusion_Off")); /* taken out: 0 showed the mouse pointer */
        if (FF_SETTINGS[i].video) assert(!FF_SETTINGS[i].shared); /* never sent to friends */
    }
    assert(blur >= 0 && fov >= 0);
    char v[FF_VAL];
    assert(ff_normalize(blur, "0", v, sizeof v) && !strcmp(v, "0"));
    assert(!ff_normalize(blur, "2", v, sizeof v));

    /* what the window sends on Save: Fusion Fix's FOV and motion blur off */
    uint8_t m[] = { (uint8_t)fov, 4, '1', '.', '2', '0', (uint8_t)blur, 1, '0' };

    /* the game's file gets only its own key, written its own way; nothing else changes */
    char *vid = dup(game);
    assert(ff_apply(&vid, m, sizeof m, FF_VIDEO) == 1);
    char *want = dup(game);
    want[strstr(want, "UseMotionBlur=1") - want + 14] = '0';
    assert(!strcmp(vid, want));
    assert(ff_apply(&vid, m, sizeof m, FF_VIDEO) == 0); /* again: nothing to change */
    free(want);
    free(vid);

    /* Fusion Fix's .ini never gets the game's keys */
    char *ff = dup("[MAIN]\r\nFOVFactor = 1.0  // x\r\n");
    assert(ff_apply(&ff, m, sizeof m, FF_ALL) == 1);
    assert(ff_apply(&ff, m, sizeof m, FF_PERSONAL) == 0);
    assert(!strstr(ff, "UseMotionBlur") && !strstr(ff, "WinDrv"));
    free(ff);

    /* the host's co-op message never carries it, and a friend's file never takes it from one */
    char *host = dup("[UNLOCKS]\r\nUnlockDLC = 1\r\n");
    uint8_t pk[512];
    int pn = ff_pack_shared(host, pk, sizeof pk);
    assert(pn > 0);
    for (int k = 0; k < pn; k += 2 + pk[k + 1]) assert(!FF_SETTINGS[pk[k]].video);
    char *fr = dup(game);
    uint8_t evil[] = { (uint8_t)blur, 1, '0' };
    assert(ff_apply_shared(&fr, evil, sizeof evil) == 0 && !strcmp(fr, game));
    free(fr);
    free(host);
    puts("all video settings tests passed");
    return 0;
}
