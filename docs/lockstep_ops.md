# Lockstep Operations

LAN-first runbook: lobby, match start, late join, disconnect, desync response.

## Quick start (LAN)

### Host

1. Start engine with Host lobby (API: `Lobby_Host(port, name, faction_id, callbacks)`).
2. Default port `27750` if `port == 0`.
3. Set map: `Lobby_SetMap("assets/maps/....pfmap")`.
4. Optional: `Lobby_SetSeed(u64)`.
5. When players ready: `Lobby_StartMatch()` → peers receive seed/hz/delay/map → each calls `LS_Init`.

### Client

1. `Net_ParseAddress("192.168.x.x", 27750, &addr)`.
2. `Lobby_Join(addr, name, faction_id, callbacks)`.
3. Wait for `on_match_start` → `LS_Init` with `is_host=false`, `local_only=false`.

### Local-only test (fake dual)

`ls_config.local_only = true`, `num_players >= 2`: scheduler injects empty frames for non-local players. Use to validate command delay without sockets.

## CLI (planned / integrate in main)

Suggested args (document as implemented):

```
--mp-host[=port]
--mp-join=host:port
--mp-name=Player
--mp-faction=0
--mp-local-lockstep   # fake dual, no network
```

## During match

- Call `LS_Service()` / advance from the main loop every frame.
- If barrier incomplete: **freeze sim**, keep rendering/UI.
- Empty command frames are mandatory each issue tick.

## Late join

1. Host pauses at a safe tick (or between sim ticks).
2. Export session: `LS_RequestSnapshotExport(path)` → `Session_RequestSave`.
3. Send `LS_NET_SNAPSHOT_META` (tick, path/URL/size) + transfer snapshot bytes (out of band or chunked reliable).
4. Joiner loads session, sets `LS_SimClock` to snapshot tick, then applies subsequent command frames to catch up.
5. Resume when joiner hash matches Host for current tick.

## Disconnect

| Event | Policy (v1) |
|-------|-------------|
| Peer timeout / `NET_EVENT_DISCONNECT` | `LS_NotifyPeerDisconnected(player_id)`; slot marked dead; future ticks skip that player |
| Host disconnect | End match cleanly (no migration in v1) |
| Soft leave | Host broadcasts `LS_NET_PEER_LEFT` |

Optional later: AI takeover of abandoned slot.

## Desync response

1. Host emits `LS_NET_DESYNC`; peers set `LS_LastDesync()`.
2. Stop advancing sim.
3. Dump:
   - command log / replay file for the match
   - per-peer hash for the failing tick
   - session snapshot (`Session_RequestSave`)
4. Kick or leave; do not continue authoritative play.

Debug helper: register richer `LS_SetHashProvider` and bisect replay to first diverging tick.

## Chat / pause

- Chat: `Lobby_SendChat` (control plane; not sim).
- Pause: treat as a shared command or host-broadcast control message so all peers freeze the same tick (implement as `LS_CMD_CUSTOM` or dedicated net msg).

## Ship harden checklist

- [ ] Map + binary CRC at lobby join  
- [ ] Command rate limit / illegal target reject  
- [ ] NAT / relay for non-LAN  
- [ ] Desync dump UI path  
- [ ] Documented Python MP surface ([lockstep_scripting.md](lockstep_scripting.md))  
