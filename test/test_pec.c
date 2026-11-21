/* test_pec.c - the Balaclava PEC.ini (src/pec.c).
 * gcc -fsanitize=address,undefined -g -Isrc -o /tmp/tpec test/test_pec.c src/pec.c && /tmp/tpec [PEC-original.ini]
 * The game's own config is never in the repo: the checks below use a made-up file of the same
 * shape; given a path, the real one saved by the game DLL is checked too. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pec.h"

static char *add(char *s, const char *t) { strcat(s, t); return s; }

/* a made-up config of the game's shape: 3 sections, the head lines the game has */
static char *fake(void)
{
    char *s = calloc(1, 400000);
    /* the game's own line counts: 9, 1436 (1419 + 17 heads), 28 */
    strcpy(s, "\xEF\xBB\xBF[Engine.PECInventoryManager]\r\nm_DefaultMeshComp=(m_iHeadID=255)\r\n");
    for (int i = 0; i < 8; i++) add(s, "m_eDefaultGadgets[0]=GDT_A\r\n");
    add(s, "[Echelon.EPECInventoryManager]\r\n");
    for (int i = 0; i < 1417; i++) add(s, "m_arSuitMaterial=(m_PresetType=ePresetType_SPSuitA, m_Material=\"X\")\r\n");
    add(s, "m_arrAdvBodyParts=(m_PresetType=ePresetType_Balaclava,m_MeshName=\"CHARM-CCS-Balaclava.Balaclava_head\",\tm_BodyPart=eAdvMeshBodyPart_Head)\r\n");
    add(s, "m_arrAdvBodyParts=(m_PresetType=ePresetType_HubSam,m_MeshName=\"CHARM-CCS-Sam.Sam_head_AIR\",\tm_BodyPart=eAdvMeshBodyPart_Head)\r\n");
    add(s, "m_arrAdvBodyParts=(m_PresetType=ePresetType_DirtySam,m_MeshName=\"CHARM-CCS-Sam.Sam_head_dirt\",\tm_BodyPart=eAdvMeshBodyPart_Head)\r\n");
    add(s, "m_arrAdvBodyParts=(m_PresetType=ePresetType_DirtySamNoGoggles,m_MeshName=\"CHARM-CCS-Sam.Sam_head_dirt_nohair\",\tm_BodyPart=eAdvMeshBodyPart_Head)\r\n");
    add(s, "m_arrAdvBodyParts=(m_PresetType=ePresetType_CineSam,m_MeshName=\"CHARM-CCS-Sam.Sam_head_highres_nohair\",\tm_BodyPart=eAdvMeshBodyPart_Head)\r\n");
    add(s, "m_arrAdvBodyParts=(m_PresetType=ePresetType_COOPBriggs,m_MeshName=\"CHARM-CCS-Briggs.Briggs_head\",\tm_BodyPart=eAdvMeshBodyPart_Head)\r\n");
    add(s, "m_arrAdvBodyParts=(m_PresetType=ePresetType_CineBriggs,m_MeshName=\"CHARM-CCS-Briggs.Briggs_head\",\tm_BodyPart=eAdvMeshBodyPart_Head)\r\n");
    for (char c = 'A'; c <= 'L'; c++) {
        char l[200];
        snprintf(l, sizeof l, "m_arrAdvBodyParts=(m_PresetType=ePresetType_SPSuit%c,m_MeshName=\"CHARM-CCS-Sam.Sam_head\",      m_BodyPart=eAdvMeshBodyPart_Head)\r\n", c);
        add(s, l);
    }
    add(s, "[Echelon.EPECInventory]\r\nm_cUpgradesPerGadget=2\r\n");
    for (int i = 0; i < 27; i++) add(s, "m_arGadgets=(m_eName=GDT_A)\r\n");
    return s;
}

static int count(const char *s, const char *w) { int n = 0; for (const char *p = strstr(s, w); p; p = strstr(p + 1, w)) n++; return n; }

static void check_out(const char *orig, char *out)
{
    assert(out);
    assert(!strncmp(out, "\xEF\xBB\xBF[Engine.PECInventoryManager]\r\n", 33));
    assert(pec_ours(out) && !pec_ours(orig));
    assert(count(out, "CHARM-CCS-Sam.Sam_head\"") == 0);
    assert(count(out, "CHARM-CCS-Sam.Sam_head_dirt\"") == 0);
    assert(count(out, "CHARM-CCS-Sam.Sam_head_dirt_nohair\"") == 0);
    assert(count(out, "CHARM-CCS-Balaclava.Balaclava_head\"") == 13);       /* 12 suits + the game's own */
    assert(count(out, "CHARM-CCS-Sam.Sam_head_") == count(orig, "CHARM-CCS-Sam.Sam_head_") - 2); /* cutscene heads stay */
    /* the mark sits right under the suit section's name, as its own line */
    assert(strstr(out, "[Echelon.EPECInventoryManager]\r\n" PEC_MARK "\r\n"));
    /* nothing else changed: put the heads back, drop the mark, and it's the original again */
    size_t n = strlen(out) + 1;
    char *back = malloc(n);
    char *o = back;
    for (const char *p = out; *p;) {
        static const char *const F[][2] = {
            { "CHARM-CCS-Balaclava.Balaclava_head_dirt_nohair\"", "CHARM-CCS-Sam.Sam_head_dirt_nohair\"" },
            { "CHARM-CCS-Balaclava.Balaclava_head_dirt\"", "CHARM-CCS-Sam.Sam_head_dirt\"" },
            { "ePresetType_Balaclava,m_MeshName=\"CHARM-CCS-Balaclava.Balaclava_head\"", "ePresetType_Balaclava,m_MeshName=\"CHARM-CCS-Balaclava.Balaclava_head\"" },
            { "CHARM-CCS-Balaclava.Balaclava_head\"", "CHARM-CCS-Sam.Sam_head\"" },
            { PEC_MARK "\r\n", "" },
        };
        int hit = -1;
        for (int i = 0; i < 5 && hit < 0; i++) if (!strncmp(p, F[i][0], strlen(F[i][0]))) hit = i;
        if (hit < 0) { *o++ = *p++; continue; }
        strcpy(o, F[hit][1]); o += strlen(F[hit][1]); p += strlen(F[hit][0]);
    }
    *o = 0;
    const char *ob = !strncmp(orig, "\xEF\xBB\xBF", 3) ? orig + 3 : orig;
    assert(!strcmp(back + 3, ob));
    free(back);
}

int main(int argc, char **argv)
{
    char *o = fake();
    char *out = pec_balaclava(o, true, false);
    check_out(o, out);
    /* our own file is never a source */
    assert(!pec_balaclava(out, true, false));
    free(out);

    /* Briggs (co-op): only his co-op head; his cutscene head and Sam's stay */
    const char *coop = "ePresetType_COOPBriggs,m_MeshName=\"CHARM-CCS-Briggs.Briggs_head\"";
    const char *coop_b = "ePresetType_COOPBriggs,m_MeshName=\"CHARM-CCS-Balaclava.Balaclava_head\"";
    char *br = pec_balaclava(o, false, true);
    assert(br && pec_ours(br) && strstr(br, coop_b) && !strstr(br, coop));
    assert(strstr(br, "ePresetType_CineBriggs,m_MeshName=\"CHARM-CCS-Briggs.Briggs_head\""));
    assert(count(br, "CHARM-CCS-Sam.Sam_head\"") == 12 && count(br, "CHARM-CCS-Balaclava.Balaclava_head\"") == 2);
    free(br);
    char *both = pec_balaclava(o, true, true);       /* both: Sam's 14 and Briggs' 1, plus the game's own */
    assert(both && strstr(both, coop_b) && count(both, "CHARM-CCS-Balaclava.Balaclava_head\"") == 14 && !strstr(both, "CHARM-CCS-Sam.Sam_head\""));
    free(both);
    assert(!pec_balaclava(o, false, false));          /* neither: nothing to write */
    char *nb = strdup(o);                             /* Briggs' co-op line not as the game has it */
    memcpy(strstr(nb, "ePresetType_COOPBriggs") + 12, "XOOP", 4);
    assert(!pec_balaclava(nb, false, true) && !pec_balaclava(nb, true, false)); free(nb);

    /* anything not exactly the game's shape: nothing */
    char *b = strdup(o);
    strstr(b, "[Echelon.EPECInventory]")[1] = 'X';                    /* a section missing */
    assert(!pec_balaclava(b, true, false)); free(b);
    b = strdup(o);
    memcpy(strstr(b, "m_cUpgradesPerGadget"), "KEY_1234", 8);          /* a key without its name */
    assert(!pec_balaclava(b, true, false)); free(b);
    b = strdup(o);
    memcpy(strstr(b, "SPSuitL,m_MeshName=\"CHARM-CCS-Sam.Sam_head\"") + 18, "CHARM-CCS-Xam", 13); /* 11 heads, not 12 */
    assert(!pec_balaclava(b, true, false)); free(b);
    b = strdup(o);
    char *cut = strstr(b, "m_arSuitMaterial");                       /* the suit section cut short */
    memmove(cut, strstr(cut + 900 * 70, "m_arrAdvBodyParts"), strlen(strstr(cut + 900 * 70, "m_arrAdvBodyParts")) + 1);
    assert(!pec_balaclava(b, true, false)); free(b);
    b = calloc(1, strlen(o) + 1000);                                   /* entries from elsewhere after the gadgets (seen) */
    strcpy(b, o);
    for (int i = 0; i < 3; i++) strcat(b, "m_arSuitMaterial=(m_PresetType=ePresetType_SPSuitK, m_Material=\"Y\")\r\n");
    assert(!pec_balaclava(b, true, false)); free(b);
    b = calloc(1, strlen(o) + 300);                                    /* already a modded head */
    strcpy(b, o);
    strcpy(strstr(b, "[Echelon.EPECInventory]"), "m_arrAdvBodyParts=(m_PresetType=ePresetType_X,m_MeshName=\"CHARM-CCS-Balaclava.Balaclava_head\")\r\n[Echelon.EPECInventory]\r\nm_cUpgradesPerGadget=2\r\n");
    assert(!pec_balaclava(b, true, false)); free(b);
    assert(!pec_balaclava(0, true, false) && !pec_balaclava("", true, false) && !pec_ours(0));
    free(o);

    if (argc > 1) {   /* the real one, from this PC (never committed) */
        FILE *f = fopen(argv[1], "rb");
        assert(f);
        fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        char *r = calloc(1, (size_t)n + 1);
        assert(fread(r, 1, (size_t)n, f) == (size_t)n);
        fclose(f);
        char *ro = pec_balaclava(r, true, false);
        check_out(r, ro);
        char *rb = pec_balaclava(r, true, true);     /* and with Briggs too */
        assert(rb && strstr(rb, "ePresetType_COOPBriggs,m_MeshName=\"CHARM-CCS-Balaclava.Balaclava_head\""));
        free(rb);
        printf("real file: %ld bytes in, %zu out\n", n, strlen(ro));
        free(ro); free(r);
    }
    puts("all pec tests passed");
    return 0;
}
