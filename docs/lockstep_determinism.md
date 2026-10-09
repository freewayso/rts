# Lockstep Determinism

Authoritative simulation must be **bit-identical** across peers given the same seed and command stream. Render, audio, and local UI may diverge.

## SimClock

| API | Role |
|-----|------|
| `LS_SimTick()` | Current logical tick |
| `LS_SimHz()` | Fixed rate (default 10) |
| `LS_FixedDt()` | `1.0 / hz` |
| `LS_SimTimeMs()` | `tick * 1000 / hz` for gameplay timers |

**Rule:** In `GAME_MODE_MP_LOCKSTEP`, gameplay timing must use `LS_SimTimeMs()` / tick counters — **not** `SDL_GetTicks()`.

Known wall-clock hotspots to migrate:

- [`src/game/combat.c`](../src/game/combat.c) — attack period / `attack_start_tick`
- Formation / harvester / builder / projectile paths that stamp wall time
- Python `SDL_GetTicks` exposure for **gameplay** branches (see scripting doc)

Camera, animation presentation, and loading screens may keep wall-clock.

## RNG

- Match seed from lobby `START_MATCH.rng_seed`.
- Use `LS_RngSeed` / `LS_RngNext` / `LS_RngFloat` only for sim-affecting randomness.
- Ban unseeded `rand()` / `random()` in authoritative paths.

Do **not** call `LS_RngNext` inside hash computation (consumes stream and diverges hash from sim).

## Ordering

Hash maps (`khash`) iteration order is **not** stable across runs/platforms.

For any loop that changes sim state:

1. Collect keys into an array.
2. Sort by stable key (usually entity `uid`).
3. Process in sorted order.

Same for merging parallel job results: reduce by sorted `uid`.

## Parallelism and GPU

| System | MP policy (v1) |
|--------|----------------|
| Nav / ClearPath / combat jobs | Prefer single-threaded logic, or deterministic reduce-by-uid |
| GPU crowd / compute that moves authoritative units | **Off** in MP, or CPU deterministic path |
| Fiber scheduling order | Must not affect sim outcomes |

Performance parallelism is restored only after replay hashes stay green.

## Python

Script that mutates entities, resources, or combat must be command-driven or registered deterministic tick callbacks. See [lockstep_scripting.md](lockstep_scripting.md).

## Acceptance: replay hash

1. Record a command stream (`LS_ReplayBeginRecord` / end).
2. Replay twice on one machine → `LS_ComputeStateHash()` equal every tick.
3. Replay on a second machine/OS (same arch endian) → hashes equal.

**Do not open public matchmaking until step 2–3 pass.**

## Hash contents (baseline → production)

Baseline (`LS_ComputeStateHash`): mixes `sim_tick` + player-controllable faction mask.

Production should register `LS_SetHashProvider` that mixes, in sorted uid order:

- position / HP / combat state / build progress / resources
- RNG stream counter (not a raw `RngNext` draw)
- anything else that affects future sim

## Floats

v1 keeps IEEE floats with identical build flags (`-fno-strict-aliasing`, same optimization). Cross-compiler float divergence is a known risk; fixed-point is a later hardening option if desyncs appear only across toolchains.
