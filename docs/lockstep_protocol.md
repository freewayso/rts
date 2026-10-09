# Lockstep Protocol

Binary control-plane and lockstep wire formats. Version gate: `NET_PROTOCOL_VERSION` (currently **1**).

Default port: **27750** (`NET_DEFAULT_PORT`). Max peers: **8**. Max UDP payload: **1200** bytes (`NET_MAX_PACKET`).

## Transport

[`src/net/`](../src/net/) provides an ENet-style API without requiring an external ENet build:

| Channel | Use |
|---------|-----|
| `NET_CH_RELIABLE` | Lobby, command frames, hashes, disconnect notices |
| `NET_CH_UNRELIABLE` | Optional future: voice/debug; not required for lockstep |

Packet magic (internal transport header): `0x50464E54` (`PFNT`).

Peers are identified by slot `0 .. NET_MAX_PEERS-1`. Host is typically slot **0** in the lobby.

## Lobby messages

Lobby packets: `lobby_hdr` + payload.

```
struct lobby_hdr {
    uint8_t  type;         /* enum lobby_msg_type */
    uint8_t  reserved;
    uint16_t payload_len;
};
```

| Type | Name | Direction | Payload |
|------|------|-----------|---------|
| 1 | `LOBBY_MSG_HELLO` | Client→Host | protocol, faction_id, name[32] |
| 2 | `LOBBY_MSG_WELCOME` | Host→Client | assigned_slot, host_slot, protocol |
| 3 | `LOBBY_MSG_PLAYER_LIST` | Host→All | array of player wire structs |
| 4 | `LOBBY_MSG_CHAT` | Either | UTF-8 text (≤200 bytes recommended) |
| 5 | `LOBBY_MSG_START_MATCH` | Host→All | start wire (see below) |
| 6 | `LOBBY_MSG_PING` | Either | opaque |
| 7 | `LOBBY_MSG_PONG` | Either | echo |
| 8 | `LOBBY_MSG_KICK` | Host→Client | reason |
| 9 | `LOBBY_MSG_REJECT` | Host→Client | reason byte (1=protocol, 2=full) |

### START_MATCH payload

```
protocol_version : u32
rng_seed         : u64
sim_hz           : u32      /* default 10 */
command_delay    : u32      /* default 2 */
local_player_id  : i32      /* filled locally on receive */
num_players      : i32
map_path         : char[256]
```

After START_MATCH, each peer builds `ls_config` and calls `LS_Init`.

## Lockstep net messages

Lockstep packets: `ls_net_hdr` + payload.

```
struct ls_net_hdr {
    uint8_t  type;         /* enum ls_net_msg_type */
    uint8_t  player_id;
    uint16_t payload_len;
    uint32_t tick;
};
```

| Type | Name | Meaning |
|------|------|---------|
| 1 | `LS_NET_CMD_FRAME` | Serialized `ls_player_frame` for `tick` |
| 2 | `LS_NET_HASH` | `u64` state hash for `tick` |
| 3 | `LS_NET_DESYNC` | Host detected hash mismatch |
| 4 | `LS_NET_PEER_LEFT` | Player slot gone; treat as empty thereafter |
| 5 | `LS_NET_SNAPSHOT_META` | Late-join: tick + session path/meta |
| 6 | `LS_NET_EMPTY_FRAME` | Explicit empty (optional; zero cmd_count also OK) |

### Command frame body

Serialized `ls_player_frame` (see [lockstep_commands.md](lockstep_commands.md)):

```
tick       : u32
player_id  : u8
cmd_count  : u8
reserved   : u16
cmds[cmd_count] : packed ls_cmd
```

**Rule:** every alive player must produce exactly one frame per issue tick (may be `cmd_count == 0`).

### Host relay

1. Client sends `LS_NET_CMD_FRAME` to Host.
2. Host stores and **broadcasts** the frame to all peers (including originator, or originator stores locally before send — implementations must not double-apply).
3. Each peer waits until all alive players’ frames for `exec_tick` are present, then simulates.

## Hash exchange

- Interval: every `LS_HASH_INTERVAL` ticks (default **1**).
- Payload: little-endian `uint64_t` hash from `LS_ComputeStateHash()` (or registered provider).
- Host compares all reported hashes for the tick; on mismatch emits `LS_NET_DESYNC` and peers set desync flag.

## Version / map checks (ship harden)

Before START_MATCH, Host should reject clients when:

- `protocol_version` ≠ Host
- Engine / content CRC mismatch (map file CRC recommended)
- Incompatible `sim_hz` / command schema version (future field)

## Endianness

All multi-byte integers on the wire are **little-endian** (native on typical Windows/Linux x86_64 targets). Cross-endian platforms are out of scope for v1.
