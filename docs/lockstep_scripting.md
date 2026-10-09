# Lockstep Scripting (Python)

Engine embeds **Python 2.7**. In single-player, scripts may call game APIs freely. In **MP lockstep**, anything that changes authoritative sim state must be deterministic and shared.

## Principle

| Kind | SP | MP lockstep |
|------|----|-------------|
| Camera, local UI, sounds, particles | OK | OK (local only) |
| Issue unit orders | Direct API OK | Must become `ls_cmd` / `LS_Issue*` so all peers apply |
| Spawn / kill / set HP / give resources | OK | Forbidden unless driven by shared command or shared deterministic tick |
| `SDL_GetTicks` / wall time for gameplay | Discouraged | **Forbidden** for sim branches |
| Unseeded random | Discouraged | **Forbidden** for sim |

## Allowed in MP

- Read-only queries for UI (selection highlights, local alerts) that do not mutate sim.
- Subscribing to events for **presentation** (play sound on `EVENT_ORDER_ISSUED`).
- Issuing orders through lockstep-aware APIs once exposed to Python (wrappers around `LS_IssueMove`, etc.).
- Deterministic tick callbacks registered by the engine that run on every peer with the same inputs (no local-only state).

## Forbidden in MP (authoritative)

- Direct `G_Move_SetDest` / `G_Combat_AttackUnit` / build / gather from script without going through the command stream.
- Branching gameplay on local mouse timing, frame index wall-clock, or peer-local RNG.
- Mutating Python interpreter state that affects future sim differently per peer (e.g. peer-only counters).
- Loading different scripts or map logic per peer after match start.

## Match start

All peers must load the **same** map script / session entry with the same `argv` and the seed from `START_MATCH`. Session pickle differences after start imply desync risk — prefer command-driven mutation over pickled peer-local objects for MP matches.

## Migration checklist for `scripts/rts/`

For each script that calls into C game APIs:

1. Does it change entities, combat, economy, or victory? → route via command or shared tick.
2. Does it read time? → use sim tick if shared; wall-clock only for UI.
3. Does it use `random`? → `LS_Rng*` via binding, or remove.

## Engine hooks (target)

- Python binding: `pf.lockstep_issue_move(...)`, `pf.lockstep_active()`, `pf.sim_tick()`.
- Guard: when `LS_IsLockstep()`, dangerous C APIs called from Python either enqueue commands or assert/log reject.

Until bindings land, treat RTS demo scripts as **SP-only** for multiplayer matches.

## Console

Interactive console is host-debug only in MP; commands that mutate sim must be broadcast as `LS_CMD_CUSTOM` or disabled.
