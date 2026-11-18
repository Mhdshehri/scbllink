/* wintun.h - the virtual network adapter (Wintun, by the WireGuard project).
 * wintun.dll is loaded at run time from SCBLLink's own folder. Functions are
 * declared by hand from Wintun's public documentation (no SDK headers). */
#pragma once
#include "mini.h"

/* Load wintun.dll and create (or reopen) the "SCBLLink" adapter.
   Returns NULL on success or an error message. */
const char *tun_open(void);
void tun_close(void);              /* ends the session and removes the adapter */
bool tun_is_open(void);

/* Next packet the PC sent into the adapter (an IPv4/IPv6 packet, no link
   header). Returns its size and sets *p, or 0 if there is none right now.
   Call tun_release(p) when done with it. */
int tun_recv(const uint8_t **p);
void tun_release(const uint8_t *p);
bool tun_send(const uint8_t *p, int n);  /* hand a packet to the PC */
void tun_wait(int ms);                   /* until a packet is ready or ms pass */
const char *tun_dll_hash(void);          /* SHA-256 of the wintun.dll that was loaded */
