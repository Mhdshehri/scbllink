# What the game says to its server

Facts read from SCBLLink's own recordings of Splinter Cell: Blacklist (developer build,
`tools/rmc.py capture.pcap`). `src/srv.c` is written from this page. By Mhdshehri.

## Ports

| Port | What |
|---|---|
| TCP 80 | `GET /OnlineConfigService.svc/GetOnlineConfig?...` → JSON with `SandboxUrl` = `prudp:/address=IP;port=21126` and `SandboxUrlWS` = `IP` |
| UDP 21126 | login (tickets) |
| UDP 21127 | everything else |
| TCP 8000 | `GET /mp_balancing.ini`; `PUT /ugc/...` (the game's own file, about 80 KB, sent with `Expect: 100-continue`) |

## Packets (UDP)

`src dst type|flags<<3 session signature(4) sequence(2) [connection signature(4): SYN, CONNECT] [fragment(1): DATA] [size(2): flag 8] payload checksum(1)`

- `src`/`dst`: stream bytes. The server is `0x31`; the game uses `0x3F` (and `0x3E` for its second login, "Tracking").
- types: 0 SYN, 1 CONNECT, 2 DATA, 3 DISCONNECT, 4 PING. flags: 1 ACK, 2 RELIABLE, 4 NEED_ACK, 8 HAS_SIZE.
- checksum: add the packet as little-endian 32-bit words, add that sum's four bytes, add the bytes left over, add the bytes of `yl4NG7qZ`; keep 8 bits.
- SYN → answer (flags 9) carries the server's connection signature. CONNECT carries the game's. After that
  every packet's `signature` is the *other* side's connection signature.
- The game numbers CONNECT 1 and its DATA from 2; the server's DATA starts at 1. Every DATA is acknowledged
  with an empty DATA (flag ACK) of the same number. PING every 10 s, answered with the same number.
- DATA payload: RC4 with key `CD&ML` (fresh for each packet) over `flag, bytes`. flag 0: plain. Otherwise a zlib stream
  (flag = unpacked/packed + 1). A long call is cut into pieces of at most ~1000 bytes, each packed on its own;
  fragment numbers 1, 2, ... and 0 for the last.

## Tickets

- Key for player `pid`: MD5 of `UbiDummyPwd`, applied 65000 + pid % 1024 times.
- A ticket: RC4(key){ session key(16), target pid(4), length(4), server's own blob } + HMAC-MD5(key) of those bytes.
  Only the server reads the blob.
- CONNECT on 21127 has a payload: RC4(`CD&ML`){ 0, length, blob, length, RC4(session key){ pid, connection id, challenge, 16 more bytes } }.
  The answer's payload: RC4(`CD&ML`){ 0, 4, challenge + 1 }.

## Calls

Request: `size(4) protocol|0x80 call id(4) method(4) values`. Answer: `size protocol 1 call id method|0x8000 values`,
or `size protocol 0 error(4) call id`. Protocol `0x7F` means the real number follows as 16 bits.
Text: 16-bit length (with the closing zero), bytes. Lists: 32-bit count.

Login, in order (recorded alone, 120 messages; all are answered by `src/srv.c`):

| Call | Name | Notes |
|---|---|---|
| 10.2 | LoginEx | name + `UbiAuthenticationLoginCustomData` { name, key, password } → 0x10001, pid, ticket, server's address text |
| 10.3 | RequestTicket | (pid, 4096) → ticket for 21127 |
| 11.4 / 11.1 | RegisterEx / Register | → 0x10001, connection id, the game's address as the server sees it |
| 5003.1 | server settings | 46 names with values (`UPLAY_MAX_RANK_LIMITED_MODE` = 5 is among them) |
| 39.2, 1001.1, 1001.2, 107.1, 106.5, 106.8, 106.9, 105.2, 35.1, 36.4, 5007.1, 5002.1, 29.12, 29.22, 49.2/6/7/9 | locale, tracking, clock, clan, challenges, privileges, news, account names, terms, Uplay actions | small fixed answers |
| 10.1 | Login("Tracking") | the game's second login: pid 105, target 2 |
| 53.1 / 53.9 | SearchContents / GetContentUrl | type 0x80000002 = the balancing file, owner 1119 |
| 53.6 / 53.7 | SaveContentAndGetUploadInfo / UploadEnd | type 0x80000003 = the player's own file |
| 55.2 | ReadStatsByPlayers | one result per board and context; no stored values → defaults (0; 0x7FFFFFFF for 10/213 and 23/154). At most 100 results came back |
| 42.21, 42.1, 42.8, 42.2, 42.3, 42.5, 42.23 | RegisterURLs, CreateSession, AddParticipants, UpdateSession, DeleteSession, LeaveSession, AbandonSession | sessions; attribute 113 = 1 marks the private-match lobby |

## Not seen yet

Versus matches, and finding sessions other than a friend's (42.6, 42.7, 42.12, 42.26 ...).
`srv.c` logs any call it cannot answer as "not built yet" with its bytes.

## Private match, as the game really does it (seen 2026-11-18, two games)
1. Each game makes its anteroom (113=1) and adds itself.
2. The invitation arrives while the host has only its anteroom; it is bound to that.
3. The guest's SearchSessionsWithParticipants([host]) gets the anteroom with itself already in it,
   then it abandons/splits its own anteroom and calls JoinSession on the host's anteroom.
4. Starting co-op: the host makes the match room (113=0), adds itself as private (it gets 7003 too),
   then adds the guest as private; the guest gets 7003 {from host, about guest, room} and goes in.
   The guest makes no call for the match room itself.
5. Only the owner, someone it invited, or a member of its anteroom may be put in the match room.
6. Before joining, the guest asks NatTraversal.RequestProbeInitiationExt (3.3) {targets, its own
   address}; the server tells each target game InitiateProbe (3.2) {the guest's address}, so both
   send first (hole punching). On the SCBLLink network it isn't needed, but it's done the same way,
   and only for games that share a room with the guest or invited it.
