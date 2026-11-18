/* gamepipe.h - SCBLLink's end of the pipe to its game DLL (dll/link.h has the
 * line format). One game at a time, this PC only. By Mhdshehri. */
#pragma once
#include "mini.h"

void gp_start(void);                   /* starts waiting for the game */
bool gp_connected(void);
bool gp_take(char *line, int n);       /* a line from the game ("" + false when none) */
void gp_send(const char *fmt, ...);    /* a line to the game (no \n needed) */
/* the same for pipe i: 0 = the game, 1 = a second game on this PC (developer build) */
void gpx_start(int i);
bool gpx_connected(int i);
bool gpx_take(int i, char *line, int n);
void gpx_send(int i, const char *fmt, ...);
