# Phase 2 — Online rooms / NAT (deferred)

Out of scope for the LAN P2P client shipped in batch 2.

## Intent

- Account / room list via TCP + protobuf (`src/net/online_client.cpp`, `server/`).
- Match found → inject host address / role into existing `Lobby_*` + `LS_*` (same as desktop `PF_MP_*` launch).
- Public internet: NAT hole-punch or UDP relay; room-server relay is **not** wired to lockstep today.

## Non-goals for phase 2 kickoff

- Rewriting lockstep wire protocol.
- Replacing LAN Host/Join UI (keep as fallback / debug).

## Suggested entry work when starting phase 2

1. Decide whether to build `online_client.cpp` with NDK C++ or a thin Kotlin TCP client that only fetches room metadata.
2. Map room “start game” payload → `pf_android_mp_join` / host parameters.
3. Add connectivity UX (Wi‑Fi vs cellular warning).
