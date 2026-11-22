/* ui.h - what the SCBLLink engine (scbllink.c) needs from the window
 * (ui_win.c). The engine runs on its own thread; every function here is
 * thread-safe. Same design as SCCTLink's window. By Mhdshehri. */
#pragma once
#include "mini.h"

#define SCBLLINK_VERSION "0.4.1"

typedef enum {
    CMD_NONE,
    CMD_HOST,        /* start hosting */
    CMD_JOIN,        /* start looking for a hosting friend */
    CMD_JOIN_LOBBY,  /* arg = lobby id */
    CMD_LAUNCH,      /* start Splinter Cell: Blacklist (only once the server answers) */
    CMD_TEST,        /* check the host's server now */
    CMD_DETAILS,     /* write the link's details to the log */
    CMD_KICK,        /* host: arg = friend's Steam id */
    CMD_FIX_INSTALL, /* put SCBLLink's game DLL in the game folder; download Fusion Fix, check it, install it */
    CMD_FIX_REMOVE,  /* put the game's own uplay_r1_loader.dll back */
    CMD_SET_NAME,    /* the window saved a new name (scbllink_name.txt) */
    CMD_FF_SAVE,
    CMD_ACCEPT,      /* accept the pending invitation (until the in-game overlay does it) */
    CMD_REFRESH,     /* have the game read the friend list again */
    CMD_DEV_SOLO,    /* developer build: switch "Record alone" */
    CMD_DEV_TWO,     /* developer build: switch "2nd game" (a second copy of the game on this PC) */  /* developer build: switch between the downloaded server and SCBLLink's own */
     /* the game settings window saved: ui_take_ff() has the values */
    CMD_STOP,        /* back to the start */
    CMD_QUIT,
} CmdKind;

typedef struct { CmdKind kind; u64 arg; } Cmd;

enum { MODE_IDLE = 0, MODE_HOST = 1, MODE_JOIN = 2 };

typedef struct {
    int mode;
    u64 lobby;            /* host: own lobby; join: lobby joined */
    bool linked;          /* join: the host welcomed us. host: a friend is linked */
    bool ready;           /* the host's server answers through the link */
    bool game_found;      /* Blacklist's folder was found */
    bool fix_ok;          /* SCBLLink's game DLL and Fusion Fix are in place */
    bool dll_ok;          /* SCBLLink's game DLL (this version's) is in place: until then everything else is locked */
    bool fix_old;         /* an older SCBLLink game DLL is in the game folder: "Update SCBL Fix" */
    bool game_running;
    char my_name[20];     /* this player's name in the game */
    char my_ip[16];       /* this PC's SCBLLink address, "" when the adapter is off */
    char status[160];     /* one line for the big status label */
    bool ff_ok;           /* Fusion Fix is installed: its settings can be changed */
    bool ff_host_rules;   /* joined: the host's co-op settings are in use */
    char ff_ini[400];     /* Fusion Fix's .ini (the settings window reads it) */
    char video_ini[400];  /* the game's own videoSettings.ini, "" until the game made it */
    bool game_linked;     /* SCBLLink's own game DLL is connected */
    char invite_from[20]; /* a pending invitation, "" = none */
    bool dev_two;         /* developer build: a second game on this PC is chosen */
    bool dev_solo;        /* developer build: "Record alone" is on */
} UiState;

#define UI_NAME 72
typedef struct { u64 id, lobby; char name[UI_NAME]; } UiPerson;

enum { LIST_LINKED = 0 /* host: friends linked now */, LIST_HOSTING = 1 /* join: friends hosting */ };

void ui_log(const char *line);   /* lines starting with "!" are warnings */
void ui_alert(const char *what); /* flash the window in the taskbar, short sound */
void ui_state(const UiState *s);
void ui_list(int which, int n, const UiPerson *p);
bool ui_poll(Cmd *c);
/* CMD_FF_SAVE: the settings the window saved (fusion.h's idx/len/value list) */
int ui_take_ff(uint8_t *out, int cap);

/* engine entry (scbllink.c): runs until CMD_QUIT */
void engine_run(void);
