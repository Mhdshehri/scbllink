/* fusion.h - ThirteenAG's Fusion Fix for Splinter Cell: Blacklist: the
 * settings SCBLLink shows, and editing its .ini. Portable (tested on Linux).
 * By Mhdshehri. */
#pragma once
#include "mini.h"

enum { FF_BOOL = 'b', FF_INT = 'i', FF_FLOAT = 'f', FF_CHOICE = 'c' };

typedef struct {
    const char *sec, *key;  /* [SEC] Key = value */
    const char *label;      /* what SCBLLink's window says */
    char kind;              /* FF_BOOL / FF_INT / FF_FLOAT / FF_CHOICE */
    double min, max;        /* numbers */
    const char *choices;    /* FF_CHOICE: "A|B|C", stored as written (or as 0,1,2 when numbered) */
    bool numbered;          /* FF_CHOICE stored as its index */
    bool shared;            /* co-op: the host's value goes to the friends */
    bool zero_ok;           /* FF_INT: 0 is allowed below min (0 = off) */
    const char *def;        /* Fusion Fix's own default */
    const char *help;       /* the window's explanation (shown on the mouse) */
} FfSetting;

extern const FfSetting FF_SETTINGS[];
extern const int FF_NSETTINGS;

#define FF_VAL 24

/* Value of [sec] key in an .ini text (trimmed, without a // or ; comment). */
bool ff_ini_get(const char *ini, const char *sec, const char *key, char *out, int n);

/* Set [sec] key = val in the .ini text (malloc'd, may be realloc'd); keeps the
   line's comment and the rest of the file. Adds the key (and section) if missing. */
bool ff_ini_set(char **ini, const char *sec, const char *key, const char *val);

/* A setting's value made safe: checked against its kind and range, written
   the way the .ini wants it. false when it isn't a valid value. */
bool ff_normalize(int idx, const char *in, char *out, int n);

/* Settings SCBLLink always sets: Fusion Fix never starts a server program. */
bool ff_harden(char **ini);

/* Co-op settings, host -> friends: idx byte, len byte, value. */
int ff_pack_shared(const char *ini, uint8_t *out, int cap);
enum { FF_ALL, FF_SHARED, FF_PERSONAL };
/* Writes idx/len/value settings (only the co-op ones, only each player's own, or all). */
int ff_apply(char **ini, const uint8_t *m, int n, int which);
/* Writes the host's co-op settings; anything not valid is skipped. Returns
   how many were applied, -1 when the message is malformed. */
int ff_apply_shared(char **ini, const uint8_t *m, int n);
