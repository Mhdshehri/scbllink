/* ownsrv.h - runs SCBLLink's own game server (srv.c) on this PC's SCBLLink address:
 * its two UDP ports and its two small web ports. Polled from the main loop. By Mhdshehri. */
#pragma once
#include "mini.h"
/* bind_ip: where to listen (the host's SCBLLink address). public_ip: what games are told. */
bool own_start(uint32_t bind_ip, uint32_t public_ip, bool (*login)(const char *name, const char *password),
               bool (*ubi_name)(const char *ubi, char name[64]), void (*log)(const char *line), bool verbose);
void own_stop(void);
bool own_running(void);
void own_poll(void);
void own_invite(const char *from_name, const char *to_name); /* see srv_invite */
