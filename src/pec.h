/* pec.h - the Balaclava choice: the game's suit config (a loose src\SYSTEM\PEC.ini) with
 * Sam's head swapped for the balaclava the game already has.
 * SCBLLink never ships the game's config: the game DLL saves the game's own one from
 * memory on this PC (dll/pec.c), and pec_balaclava() changes only the head lines.
 * Portable (tested on Linux). By Mhdshehri. */
#pragma once
#include "mini.h"

/* the file the game DLL writes (in %APPDATA%\SCBLLink) and the one the game reads (src\SYSTEM) */
#define PEC_ORIG_FILE "PEC-original.ini"
#define PEC_FILE "PEC.ini"
/* in the PEC.ini we write, so we only ever remove our own */
#define PEC_MARK "// Written by SCBLLink: Balaclava. Switch it off in SCBLLink's Game settings to remove this file."

/* sam: Sam's head in every suit; briggs: Briggs' in co-op (at least one).
   A loose PEC.ini replaces the game's whole config, so anything missing breaks the game (an
   invisible Sam, saves that crash the game later). orig must be the game's own config, whole:
   its 3 sections in order, every key named, the head lines exactly as the game has them.
   Returns the new file (malloc'd, UTF-8 with BOM) or NULL when orig isn't that. */
char *pec_balaclava(const char *orig, bool sam, bool briggs);

/* the text is a PEC.ini SCBLLink wrote */
bool pec_ours(const char *text);
