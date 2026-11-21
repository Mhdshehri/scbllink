/* camera.h - the Crosshair choice: the game's camera config (BlacklistCamera.ini) with the
 * crosshair ("reticle=") switched off where Sam aims on foot, kept for drones, turrets and the
 * remote sniper. SCBLLink never ships the game's file: the game DLL saves the game's own one on
 * this PC (dll/cfgcap.c), and cam_crosshair_off() changes only those lines.
 * Portable (tested on Linux). By Mhdshehri. */
#pragma once
#include "mini.h"

#define CAM_ORIG_FILE "camera-original.ini"             /* in %APPDATA%\SCBLLink */
#define CAM_FILE "update\\blacklistcamera.ini"          /* under src\SYSTEM: the game takes it instead of its own */
#define CAM_MARK "// Written by SCBLLink: Crosshair off. Switch the crosshair on in SCBLLink's Game settings to remove this file."

/* The game splits its config text in place while it reads it: a 0 byte where '=', ']', the line's
   '\r' and the blank after a value were. Puts the text back from such a buffer around `marker`
   (the start of a line, e.g. "[Echelon.ECamera"). malloc'd, or NULL when marker isn't in buf. */
char *cfg_unsplit(const uint8_t *buf, size_t n, const char *marker);

/* The camera config with the crosshair off (see above). orig must be the game's own, whole:
   NULL when it isn't exactly what the game has. */
char *cam_crosshair_off(const char *orig);

bool cam_ours(const char *text);
