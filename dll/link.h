/* link.h - the game DLL's side of the pipe to SCBLLink on this PC, and what it
 * learns from it. By Mhdshehri.
 *
 * The pipe \\.\pipe\scbllink-game carries text lines ("WORD\tfield\tfield\n").
 * SCBLLink -> DLL:
 *   ACCOUNT name password account_id my_ip server_ip   the game account, this PC's SCBLLink address, the host's server
 *   FRIENDS / F id name online session hex / END   the people in the SCBLLink lobby
 *   INVITE name                              someone invited this player (the overlay shows it)
 *   ACCEPT name                              the player accepted name's invitation
 *   REFRESH                                  the player asked for the friend list to be read again
 * DLL -> SCBLLink:
 *   HELLO version pid / INVITE account_id / SESSION id invite_only hex / SESSION 0 / BYE */
#pragma once
#include "w32.h"

#define SL_NAME 64
#define SL_ID 64
#define SL_PASS 64
#define SL_SESSION_DATA 496 /* the game's session block, byte for byte */
#define SL_MAX_FRIENDS 16

typedef struct {
    char id[SL_ID];
    char name[SL_NAME];
    bool online;
    uint32_t session;
    bool has_data;
    uint8_t data[SL_SESSION_DATA];
} SlFriend;

/* events for UPLAY_GetNextEvent */
enum { SL_EV_FRIENDS_CHANGED = 10000, SL_EV_INVITE_ACCEPTED = 10002 };
typedef struct { int type; char name[SL_NAME]; } SlEvent;

void sl_log(const char *fmt, ...);
void sl_start(void);                 /* connect (waits a moment for the account) */
void sl_stop(void);
bool sl_linked(void);
bool sl_account(char *name, char *pass, char *id); /* false: SCBLLink isn't there */
uint32_t sl_my_ip(void);             /* network order; 0 = unknown */
void sl_server_text(char out[16]);   /* the host's server */
int sl_friends(SlFriend *out, int max, uint32_t *version); /* copy of the list */
bool sl_next_event(SlEvent *e);
void sl_send(const char *fmt, ...);  /* one line to SCBLLink (the \n is added) */
const char *sl_dir(void);            /* the game's folder (for the log) */
bool sl_second(void);                /* the second game on this PC (developer test, -scbllink2) */
bool sl_bind_own(void);              /* sockets go on this game's own SCBLLink address */
