# Lockstep Commands

Player-visible orders are encoded as `struct ls_cmd` and batched per tick in `struct ls_player_frame` ([`src/lockstep/public/lockstep.h`](../src/lockstep/public/lockstep.h)).

Internal queues (`move_cmd`, `combat_cmd`) remain **local thread-safety** helpers. They are **not** the network command log.

## Frame layout

```
ls_player_frame {
    u32 tick
    u8  player_id
    u8  cmd_count          /* 0 .. LS_MAX_CMDS_PER_FRAME (64) */
    u16 reserved
    ls_cmd cmds[cmd_count]
}
```

Apply order for tick `N`: ascending `player_id`, then command index `0 .. cmd_count-1`.

## `ls_cmd` layout (packed)

```
u8  type
u8  flags
u16 ent_count              /* 0 .. 64 */
u32 ents[64]
f32 x, z                   /* world XZ */
u32 target_uid
i32 i0, i1                 /* type-specific ints */
```

Unused fields must be zeroed for stable hashing of recorded replays.

## Command types

| Enum | Name | Semantics | Apply backend (target) |
|------|------|-----------|-------------------------|
| 0 | `LS_CMD_NONE` | Padding / unused | — |
| 1 | `LS_CMD_MOVE` | Move selection to `(x,z)` | `G_Move_SetDest(uid, xz, false)` |
| 2 | `LS_CMD_ATTACK` | Attack-move if `target_uid==0`, else attack unit | `G_Move_SetDest(..., true)` or `G_Combat_AttackUnit` |
| 3 | `LS_CMD_STOP` | Stop combat / hold | `G_Combat_StopAttack` |
| 4 | `LS_CMD_SET_STANCE` | Stance in `i0` (`enum combat_stance`) | `G_Combat_SetStance` |
| 5 | `LS_CMD_BUILD` | Place / assign build (`i0` type, `x,z` site) | builder APIs |
| 6 | `LS_CMD_GATHER` | Gather resource `target_uid` | harvester APIs |
| 7 | `LS_CMD_GARRISON` | Enter transport/building `target_uid` | garrison APIs |
| 8 | `LS_CMD_PRODUCE` | Queue production (`i0` unit type) from `ents[0]` building | production APIs |
| 9 | `LS_CMD_CANCEL` | Cancel queue / construction (`i0` slot) | cancel APIs |
| 10 | `LS_CMD_CUSTOM` | Versioned extension (`i0` opcode, `i1` arg) | registered handlers |

Types 5–10 are reserved in the schema; apply paths land as systems are wired.

## Issue path (local)

1. UI / mouse / script wants an order.
2. If `LS_IsLockstep()`:
   - Validate every `ents[i]` with `LS_CanControlEntity` (faction == local player binding).
   - `LS_IssueCmd` / `LS_IssueMove` / `LS_IssueAttack` → pending local frame.
3. Else: apply immediately (SP).

Player move interception example: `move_order` in [`src/game/movement.c`](../src/game/movement.c) routes to `LS_IssueMove` when lockstep is active (local move marker may still show for feedback).

## Authority rules

- Only the owning peer may **originate** commands for its faction’s units.
- All peers **apply** every player’s frames (Fog of War affects display only).
- Host rejects / peers ignore frames that fail ownership checks when validating (ship harden).

## Empty frames

If the player issued nothing this tick, still send `cmd_count == 0` so the barrier can proceed.

## Schema versioning

`ls_cmd.type` values are append-only. Removing or reordering enums requires a new `NET_PROTOCOL_VERSION` and mutual reject in lobby.
