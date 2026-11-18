/* account.h - players' names and their game accounts (kept by the host). */
#pragma once
#include "mini.h"
#include "ui.h"

#define NAME_MIN_LEN 3
#define NAME_MAX_LEN 16

void name_clean(const char *in, char out[NAME_MAX_LEN + 1]);
bool name_ok(const char *n);                 /* 3-16 of A-Z a-z 0-9 _ - . */
void name_load(const char *steam_name, char out[NAME_MAX_LEN + 1]); /* saved one, else from the Steam name */
bool name_save(const char *n);
bool secret_ok(const char *s, int min, int max); /* lowercase hex, min..max long */
bool ubi_ok(const char *s);                      /* a game account id (see account.c) */

typedef struct {
    u64 steam;
    char name[NAME_MAX_LEN + 1];
    char pass[65];
    char ubi[65];
} Account;

/* Host: the game account for this Steam account, named `wanted` (or wanted2,
   wanted3... if another player has it). Kept in scbllink_accounts.txt. */
bool account_ensure(u64 steam, const char *wanted, Account *out);
