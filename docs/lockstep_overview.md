# Lockstep Overview

Permafrost multiplayer uses **command lockstep** (指令真同步): every peer runs the same simulation from the same inputs. Peers exchange **player commands**, not entity state.

This document is the architecture entry point. Details live in sibling docs:

| Doc | Topic |
|-----|--------|
| [lockstep_protocol.md](lockstep_protocol.md) | Wire formats, lobby, channels |
| [lockstep_determinism.md](lockstep_determinism.md) | SimClock, RNG, ordering rules |
| [lockstep_commands.md](lockstep_commands.md) | `PlayerCommand` / `ls_cmd` catalog |
| [lockstep_scripting.md](lockstep_scripting.md) | Python rules in MP |
| [lockstep_ops.md](lockstep_ops.md) | Host/join, late join, disconnect |

## Why lockstep (not state sync)

- RTS unit counts make frequent full-state sync expensive.
- Classic RTS netcode (Age / StarCraft-era) is command lockstep.
- Bandwidth stays low: sparse orders + empty frames; simulation stays local.

## Modes

| Mode | Meaning |
|------|---------|
| `GAME_MODE_SP` | Single-player; orders apply immediately (legacy path). |
| `GAME_MODE_MP_LOCKSTEP` | Lockstep active; orders go through the command buffer. |

APIs: `LS_SetMode` / `LS_GetMode` / `LS_IsLockstep` in [`src/lockstep/public/lockstep.h`](../src/lockstep/public/lockstep.h).

## Topology

```
Peer1 ──CmdFrame N──► Host ──CmdFrame N (all)──► Peer1
Peer2 ──CmdFrame N──► Host ──CmdFrame N (all)──► Peer2
                         │
                    hash check / desync
```

- One **Host** runs lobby, command relay, hash compare, late-join snapshots.
- **Every peer** (including Host) simulates locally after the per-tick input barrier.
- Transport: reliable UDP (ENet-style stack under [`src/net/`](../src/net/)).

## Logic clock vs render

- **Sim tick** (`sim_tick`): fixed rate, default **10 Hz** (`LS_DEFAULT_HZ`).
- **Command delay**: default **2** sim ticks (`LS_DEFAULT_DELAY`) to hide RTT.
- Render / camera / UI keep using wall-clock; they must not drive authoritative gameplay in MP.

Simulation for tick `N` runs only when every alive player's `CommandFrame` for `N` is present.

## Module layout

| Path | Role |
|------|------|
| `src/net/` | UDP transport, lobby Host/Join |
| `src/lockstep/` | SimClock, RNG, command buffer, scheduler, hash, replay |
| `src/session.c` | Full save used for late join / reconnect snapshots |
| `src/game/movement.c` | Player move orders intercepted when `LS_IsLockstep()` |

## Lifecycle (high level)

1. Lobby: Host or Join → player slots + faction binding.
2. `Lobby_StartMatch` → broadcast `rng_seed`, `sim_hz`, `command_delay`, map path.
3. `LS_Init(ls_config)` → seed RNG, prefill delay frames with empties.
4. Each engine frame: `LS_Service()` / `LS_TryAdvanceSim()` → submit local frame → barrier → apply cmds in player order → advance SimClock → optional hash.
5. Desync → pause, dump command log + snapshot (see ops doc).

## Non-goals (v1)

- Client-side prediction that mutates authoritative state.
- Host migration (Host drop ends match or clean exit).
- Full anti-cheat / encrypted protocol (baseline: hash + kick).

## Milestone order

1. Connect + seed sync  
2. Deterministic replay hashes  
3. Local fake dual lockstep  
4. LAN two-machine match  
5. Late join / disconnect  
6. Public net harden  
