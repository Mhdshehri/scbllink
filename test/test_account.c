#include "../src/account.h"
#include "../src/plat.h"
#include <stdio.h>
#include <string.h>
int main(void)
{
    plat_init();
    remove("scbllink_accounts.txt");
    Account a, b, c, d;
    int ok = 1;
    ok &= account_ensure(1001, "Mhdshehri", &a);
    printf("1: %s %s %s\n", a.name, a.pass, a.ubi);
    ok &= account_ensure(1002, "Mhdshehri", &b);      /* another player wanting the same name */
    printf("2: %s\n", b.name);
    ok &= strcmp(b.name, "Mhdshehri2") == 0;
    ok &= account_ensure(1001, "Mhdshehri", &c);      /* the first again: same account */
    ok &= strcmp(c.name, a.name) == 0 && strcmp(c.pass, a.pass) == 0;
    ok &= account_ensure(1001, "NewName", &d);        /* renamed: a new name, same password */
    ok &= ubi_ok(a.ubi) && strcmp(a.ubi, b.ubi) != 0 && !strcmp(c.ubi, a.ubi); /* an id of its own, kept */
    printf("4: %s\n", d.name);
    ok &= strcmp(d.name, "NewName") == 0 && strcmp(d.pass, a.pass) == 0;
    char cl[17]; name_clean("Abu Fahad!! <script>", cl); printf("clean: %s\n", cl);
    ok &= name_ok(cl) && !name_ok("a\"b") && !name_ok("ab");
    printf(ok ? "all account tests passed\n" : "FAILED\n");
    return !ok;
}
