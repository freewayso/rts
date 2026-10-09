# 联机后端实现方案：登录服 + 房间指令转发服

> 状态：**实现中（S0–S2 已落地骨架）** — 见仓库 `server/` 与 `src/net/online_client.c`  
> 关联：[`refactor_backlog.md`](refactor_backlog.md)、[`lockstep_overview.md`](lockstep_overview.md)、[`lockstep_protocol.md`](lockstep_protocol.md)  
> 网络设计参考：[luabus](https://github.com/trumanzhao/luabus)（基于消息名的序列化 RPC / 事件回调风格）；房间内指令同步通道使用 **UDP + KCP**，其余走 **TCP**。

### 实现进度（对照里程碑）

| 里程碑 | 状态 | 位置 |
|--------|------|------|
| S0 存储双队列 | **已实现** | `server/storage/`（FileAccountDb；SQLite 可选） |
| S1 登录服 | **已实现** | `server/login/` → `login_server.exe` |
| S2 房间 list/query/create/delete/join | **已实现** | `server/room/` → `room_server.exe` |
| S3 KCP | 部分 | UDP 转发已通；正式 KCP 库待换入 |
| S4 客户端 API | **已实现** | `src/net/public/online_client.h` |
| S5–S6 | 待做 | DESYNC / 回收硬化 |

---

## 1. 目标

搭建两套轻量服务端，支撑客户端从「注册/登录 → 进房 → 锁步指令同步」：

| 服务 | 职责 | 传输 |
|------|------|------|
| **登录服 (Login)** | 注册、登录、发会话令牌、查/改基础用户资料 | **TCP**（luabus 风格消息） |
| **房间指令转发服 (Room Relay)** | 建房/进房/退房、房内状态、**锁步指令帧转发**、开局参数下发 | 大厅/控制面 **TCP**；对局指令 **UDP+KCP** |

客户端（桌面 / 后续 Android）仍跑本地模拟；服务器**不跑 RTS 逻辑**，房间服只做：

- 鉴权与房间编排  
- 指令帧可靠有序转发（KCP）  
- 可选：校验哈希收集 / 不同步广播（权威仍在各端模拟）

用户数据用 **SQLite** 落盘；**单独一个存储线程**串行写库，避免多线程抢 SQLite。

---

## 2. 总体架构

```text
┌─────────────┐   TCP (luabus msg)    ┌──────────────┐
│   Client    │ ─────────────────────►│  Login Server│
│ (Engine)    │ ◄─────────────────────│  + Auth      │
└──────┬──────┘   token / profile     └──────┬───────┘
       │                                     │
       │                              ┌──────▼───────┐
       │                              │ SQLite       │
       │                              │ (storage     │
       │                              │  thread)     │
       │                              └──────────────┘
       │
       │  TCP: lobby / room control
       │  UDP+KCP: CommandFrame / Hash
       ▼
┌──────────────────┐
│  Room Relay      │
│  - rooms map     │
│  - peer sessions │
│  - cmd broadcast │
└──────────────────┘
```

### 2.1 与现有 Lockstep 的关系

| 原 P2P Host 方案 | 本方案 |
|------------------|--------|
| 某玩家做 Host 收齐指令再广播 | **专用 Room Relay** 做转发中心 |
| 大厅+指令可同 UDP 通道 | **控制面 TCP**，**指令面 KCP** |
| 无账号 | Login + SQLite 用户名注册 |

客户端 `LS_*` / `Net_*` 适配为：连上 Room Relay 后，把原「发给 Host」的 `CommandFrame` 改为发往 Relay；Relay 按房间广播给同房所有 peer（含发送者可选 echo，建议 **echo 回发送者** 便于统一收包路径）。

---

## 3. 网络通信约定（luabus 风格 + 双通道）

### 3.1 设计取自 luabus

参考 [trumanzhao/luabus](https://github.com/trumanzhao/luabus)：

- 进程内持有 `socket_mgr`，主循环 `wait(timeout_ms)` 驱动 IO  
- **消息名 + 参数** 的发送：`stream.send("on_login", user, pass)`  
- 对端 `on_recv(msg, ...)` 按消息名分发  
- 连接生命周期：`on_accept` / `on_connect` / `on_error` / `close`  
- 实现语言不限死 Lua：可用 **C++/Lua 同构协议**（服务端可用 Lua 或 C++ 实现同等语义）；客户端 C 侧提供薄封装即可  

**本项目扩展**：

- 保留 luabus 的 **TCP 消息通道**用于登录与房间控制  
- 另建 **KCP over UDP** 会话，专用于房间内锁步指令（高频、要可靠有序、忌队头阻塞与 TCP 重传延迟）

### 3.2 通道划分

| 流量 | 通道 | 原因 |
|------|------|------|
| 注册 / 登录 / 改名 / 心跳 / 踢人 | TCP | 低频、要清晰会话、实现简单 |
| 建房 / 进房 / 退房 / 房间列表 / 开局 `START_MATCH` | TCP | 控制面可靠即可 |
| **锁步 `CommandFrame` / 空帧 / Hash / Ping 时戳** | **UDP + KCP** | 低延迟可靠有序，适合 10Hz 指令屏障 |
| 可选：聊天、调试日志 | TCP | 不占指令通道 |

### 3.3 KCP 要点（房间指令）

- 每客户端进房成功后，TCP 下发：`room_id`、`kcp_conv`、`udp_port`、`player_slot`  
- 客户端对 Relay 的 UDP 口建立 KCP（`conv` 唯一标识会话）  
- KCP 参数建议（可配置）：小 `nodelay`、适度 `interval`（如 10–20ms）、`wnd` 按 `LS_CMD_BUFFER_FRAMES` 与 peer 数估算  
- 应用层仍带 `tick + player_id + payload`（与 `lockstep_protocol.md` 对齐），KCP 只保证到达与顺序  
- Relay **不做模拟**，只做：校验 token/slot → 校验房间 → 广播给同房其他（或全部）KCP 会话  

### 3.4 网络协议：Protocol Buffers

**所有 TCP / UDP 应用层消息改用 protobuf**（schema 在 [`server/proto/`](../server/proto/)）。

#### 帧格式（非 PB）

```text
magic       u32   = 0x50465042 ('PFPB')
version     u16   = 1
msg_id      u16   = pf.net.MsgId
payload_len u32
payload     bytes = Serialize(对应 protobuf message)
```

实现：`server/common/pb_frame.*`；生成代码：`server/proto/gen/`（`protoc` 生成）。

#### 消息 ↔ MsgId（摘录）

| MsgId | Message | 方向 |
|-------|---------|------|
| `MSG_REG_REQ/RSP` | `RegReq` / `RegRsp` | 登录 |
| `MSG_LOGIN_REQ/RSP` | `LoginReq` / `LoginRsp` | 登录 |
| `MSG_AUTH_REQ/RSP` | `AuthReq` / `AuthRsp` | 房间 |
| `MSG_ROOM_LIST_*` | `RoomListReq/Rsp` | **查询房间列表** |
| `MSG_ROOM_QUERY_*` | `RoomQueryReq/Rsp` | **查询单个房间** |
| `MSG_ROOM_CREATE_*` | `RoomCreateReq/Rsp` | **创建房间** |
| `MSG_ROOM_DELETE_*` | `RoomDeleteReq/Rsp` + `RoomDeleteNtf` | **删除房间** |
| `MSG_ROOM_JOIN/LEAVE_*` | … | 进出房 |
| `MSG_CMD_FRAME` / `MSG_HASH` | `CmdFrame` / `HashReport` | UDP/KCP 锁步 |

重新生成：

```bash
cd server
protoc -I proto --cpp_out=proto/gen proto/pf_common.proto proto/pf_login.proto proto/pf_room.proto
```

> 旧版「消息名 + 字符串参数」luabus 文本帧已废弃；仍保留 luabus 式 **主循环 wait / 回调分发** 编程模型，载荷改为 PB。

### 3.5 （历史）消息命名 — 已由 MsgId + PB 替代

下列名称仅作语义对照，线上以 `MsgId` 为准：

| 旧消息名 | MsgId |
|----------|-------|
| `reg_req` | `MSG_REG_REQ` |
| `login_req` | `MSG_LOGIN_REQ` |
| `room_list_req` | `MSG_ROOM_LIST_REQ` |
| `room_create_req` | `MSG_ROOM_CREATE_REQ` |
| `room_delete_req` | `MSG_ROOM_DELETE_REQ` |
| `room_query_req` | `MSG_ROOM_QUERY_REQ` |

KCP 应用层消息（二进制，非 luabus 字符串消息亦可）：

| 类型 | 内容 |
|------|------|
| `CMD_FRAME` | `tick, player_id, cmd_count, cmds[]` |
| `HASH` | `tick, player_id, hash64` |
| `DESYNC` | Relay 转发检测到的不同步通知（可选） |

---

## 4. 登录服（Login Server）

### 4.1 功能范围（v1 从简）

- **注册**：用户名 + 密码（明文仅传输层；库内存 **加盐哈希**，如 SHA-256 + salt）  
- **登录**：校验通过后发 **token**（随机 32B hex / UUID），带过期时间  
- **资料**：v1 仅 `uid / username / created_at / last_login`；用户名注册即可，不做邮箱手机  
- **校验接口**：房间服可用 TCP 内网消息 `token_validate` 问登录服，或共享 SQLite 只读（推荐 **登录服 RPC 校验**，房间服不直连写库）

### 4.2 非目标（v1）

- OAuth / 第三方登录  
- 复杂权限、好友、商城  
- 多登录服集群（单进程足够）

### 4.3 进程模型：逻辑线程 ↔ 存储线程（队列分离）

登录服（房间服若落库同样遵循）强制 **双线程 + 双向队列**，禁止逻辑线程直接碰 SQLite：

```text
                    ┌─────────────────────────────────────┐
                    │           Logic Thread               │
                    │  (网络 IO / luabus wait / 业务状态)   │
                    │                                     │
                    │  on_recv → 组 StorageRequest         │
                    │       │                             │
                    │       ▼                             │
                    │  req_queue  ══════════╗             │
                    └───────────────────────║─────────────┘
                                            ║
                    ┌───────────────────────║─────────────┐
                    │           Storage Thread             │
                    │  唯一 sqlite3_* 调用方               │
                    │       ║                             │
                    │       ▼                             │
                    │  执行 SQL → 组 StorageResponse       │
                    │       │                             │
                    │       ▼                             │
                    │  rsp_queue  ══════════╝             │
                    └─────────────────────────────────────┘
                                            ║
                    Logic Thread 从 rsp_queue 取回结果
                    → 继续协议回包 / 改内存状态
```

| 队列 | 方向 | 内容 |
|------|------|------|
| `req_queue` | Logic → Storage | `op, req_id, payload`（注册/登录/查 session 等） |
| `rsp_queue` | Storage → Logic | `req_id, errcode, result` |

约定：

1. **Logic Thread**：只做 socket、会话表（内存）、组包拆包、房间内存态；把持久化需求丢进 `req_queue`，在主循环里非阻塞 `try_pop(rsp_queue)`。  
2. **Storage Thread**：阻塞/`wait` 取 `req_queue`，串行访问 DB，结果写入 `rsp_queue`；**从不**直接 `stream.send`。  
3. 用 `req_id`（或 correlation id）把异步结果对回原来的连接/回调。  
4. 队列需线程安全（mutex + condvar 或无锁环，v1 mutex 即可）。  
5. 关服顺序：停 listen → 排空 `req_queue` → join Storage → 关 DB。

房间服 v1 房间态可纯内存；若日后房间元数据要落库，**同一模式**：Logic 投递、Storage 落盘，不混线程。

---

## 5. 用户存储（SQLite + 存储线程）

### 5.1 原则

- SQLite **仅** Storage Thread 访问  
- Logic / Storage 只通过 **`req_queue` / `rsp_queue`** 通信（见 §4.3）  
- 文件建议：`data/account.db`（路径可配置）

### 5.2 表结构（v1）

```sql
CREATE TABLE IF NOT EXISTS users (
    uid         INTEGER PRIMARY KEY AUTOINCREMENT,
    username    TEXT    NOT NULL UNIQUE COLLATE NOCASE,
    pass_salt   BLOB    NOT NULL,
    pass_hash   BLOB    NOT NULL,
    created_at  INTEGER NOT NULL,  -- unix sec
    last_login  INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS sessions (
    token       TEXT    PRIMARY KEY,
    uid         INTEGER NOT NULL REFERENCES users(uid),
    expire_at   INTEGER NOT NULL,
    created_at  INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_sessions_uid ON sessions(uid);
```

### 5.3 存储线程任务类型

| Op | 说明 |
|----|------|
| `USER_CREATE` | 注册 |
| `USER_AUTH` | 登录校验 → 写 session |
| `SESSION_GET` | token → uid |
| `SESSION_TOUCH` / `SESSION_DEL` | 续期 / 登出 |
| `USER_GET` | 查资料 |

失败以错误码回主线程（用户名已存在、密码错误、token 过期等）。

### 5.4 并发与性能

- v1 QPS 低，单 Storage Thread + WAL（`PRAGMA journal_mode=WAL`）足够  
- **禁止** Logic Thread 直接 `sqlite3_*`  
- 批量可在 Storage Thread 内合并，v1 不必  

---

## 6. 房间指令转发服（Room Relay）

### 6.1 职责

1. TCP：鉴权（token）、**查询/创建/删除房间**、加入/离开、`room_update` 推送  
2. 开局：校验房主 → `rng_seed` / `sim_hz` / `command_delay` / 地图 / slot → `match_start_ntf`  
3. 下发 KCP 参数；等待全员 `kcp_ready`（或超时）  
4. KCP：收 `CMD_FRAME` / `HASH` → **按 room 转发**  
5. 可选：hash 不一致 → `DESYNC`  

房间列表与 CRUD 跑在 **Logic Thread 内存**（v1）；不经 Storage，除非后续要持久化房间。

### 6.2 房间状态机

```text
OPEN ──match_start──► STARTING ──all kcp_ready──► IN_MATCH ──settle/leave──► CLOSED
         │                 │
         └─ delete/empty ──┴─ timeout ──► CLOSED（可从列表移除）
```

### 6.3 转发规则（指令）

- 仅 `IN_MATCH` 接受 `CMD_FRAME`  
- 校验：`token↔uid`、`uid↔slot`、`tick` 粗检  
- 广播给同房已 `kcp_ready` 的 peer  
- Relay **不合并指令、不模拟**；齐票在客户端 `LS_*`  

### 6.4 服务端房间管理接口（必做）

服务器对外（TCP luabus 消息）必须提供：

| 接口 | 请求 | 成功响应 | 权限 / 规则 |
|------|------|----------|-------------|
| **查询房间列表** | `room_list_req` | `room_list_rsp`：`rooms[]` | 已 `auth`；默认可只返回 `OPEN` |
| **查询单个房间** | `room_query_req(room_id)` | `room_query_rsp` | 已 `auth`；不存在则 err |
| **创建房间** | `room_create_req(name, map_path, max_players)` | `room_create_rsp(room_id, room)` | 已 `auth`；创建者 = 房主；自动加入 slot0 |
| **删除房间** | `room_delete_req(room_id)` | `room_delete_rsp` + 对成员 `room_delete_ntf` | **仅房主**；或 `IN_MATCH` 禁止删（返回 busy） |

`room` 快照字段建议：

```text
room_id, name, map_path, max_players, player_count,
owner_uid, state (OPEN|STARTING|IN_MATCH|CLOSED),
players[]: { uid, username, slot }
```

错误码建议：`OK=0`, `UNAUTHORIZED`, `NOT_FOUND`, `FULL`, `NOT_OWNER`, `BAD_STATE`, `ALREADY_IN_ROOM`。

### 6.5 与客户端 API 一一对应

客户端须实现**同名语义**的封装（C API 建议如下，放在例如 `src/net/public/room_client.h`），TCP 消息与上表对齐：

```c
/* 连接与鉴权 */
bool RoomClient_Connect(const char *host, uint16_t tcp_port);
bool RoomClient_Auth(const char *token);          /* → auth_req */

/* 房间查询 / 创建 / 删除（与服务器接口对称） */
bool RoomClient_ListRooms(void);                  /* → room_list_req */
bool RoomClient_QueryRoom(uint32_t room_id);      /* → room_query_req */
bool RoomClient_CreateRoom(const char *name,
                           const char *map_path,
                           int max_players);      /* → room_create_req */
bool RoomClient_DeleteRoom(uint32_t room_id);     /* → room_delete_req */

/* 进出房与对局 */
bool RoomClient_JoinRoom(uint32_t room_id);       /* → room_join_req */
bool RoomClient_LeaveRoom(uint32_t room_id);      /* → room_leave_req */
bool RoomClient_StartMatch(...);                  /* 房主 → match_start_req */

/* 异步结果：在 Net/Room 泵里回调或填出参事件 */
typedef void (*RoomClient_OnList)(const struct room_info *rooms, int n, void *ud);
typedef void (*RoomClient_OnRoom)(const struct room_info *room, int err, void *ud);
typedef void (*RoomClient_OnDeleted)(uint32_t room_id, int reason, void *ud);
```

| 服务器 | 客户端 |
|--------|--------|
| `room_list_req/rsp` | `RoomClient_ListRooms` + `OnList` |
| `room_query_req/rsp` | `RoomClient_QueryRoom` + `OnRoom` |
| `room_create_req/rsp` | `RoomClient_CreateRoom` + `OnRoom` |
| `room_delete_req/rsp` + `room_delete_ntf` | `RoomClient_DeleteRoom` + `OnDeleted` |

UI / 脚本只调 `RoomClient_*`，不直接拼消息名。

### 6.6 与旧 P2P Host 映射

| 客户端原 Host 行为 | 现行为 |
|--------------------|--------|
| `Net_HostCreate` | `RoomClient_CreateRoom` |
| `Net_Connect` | `RoomClient_JoinRoom` |
| （无）查房 | `RoomClient_ListRooms` / `QueryRoom` |
| （无）删房 | `RoomClient_DeleteRoom` |
| 发 CommandFrame 给 Host | KCP → Relay |
| Host 广播 | Relay 广播 |

桌面可保留 `local_only` / LAN P2P 调试；**正式联机走 Relay**。
---

## 7. 推荐目录与技术栈（实现时）

```text
server/
  login/           # 登录服进程
  room/            # 房间转发服进程
  common/          # 协议常量、token、错误码
  storage/         # SQLite + storage thread（登录服内，或独立 lib）
  third_party/
    luabus/        # 或自研同语义 TCP msg 层
    kcp/           # skywind3000/kcp
```

| 组件 | 建议 |
|------|------|
| 语言 | 服务端 C++17（对齐 luabus）或 Lua 业务 + C++ 网络；与引擎客户端解耦 |
| TCP 消息层 | luabus 或兼容其 `send(msg,...)` / `on_recv` 语义 |
| KCP | 官方 kcp + 自管 UDP socket |
| DB | SQLite3 |
| 配置 | 端口、db 路径、token TTL、房间人数上限、KCP 参数 |

默认端口（可配）：

| 服务 | TCP | UDP(KCP) |
|------|-----|----------|
| Login | 27000 | — |
| Room  | 27001 | 27002 |

---

## 8. 安全（v1 基线）

- 密码：**salt + hash** 存储，禁止明文入库  
- Token：随机、TTL（如 24h），登出删除；Room 每次进房校验  
- 房间服不信任客户端自报的 `player_id`：以服务器分配 **slot** 为准  
- TCP 可先内网/明文；后期再 TLS（非 v1 阻塞）  
- 限频：注册/登录/建房简单 IP 或 uid 节流  

---

## 9. 实现里程碑与验收

| 里程碑 | 内容 | 验收 |
|--------|------|------|
| **S0** | SQLite + **Logic/Storage 双队列** | 单测 reg/login；逻辑线程零直接 SQL |
| **S1** | Login TCP（luabus 风格） | 客户端可 reg/login 拿到 token |
| **S2** | Room：**list / query / create / delete** + join | 客户端 `RoomClient_*` 对称调通 |
| **S3** | KCP 握手 + echo ping | 双向稳定 RTT |
| **S4** | `CMD_FRAME` 转发 + 客户端改连 Relay | 两人锁步移动一致 |
| **S5** | Hash 收集 / DESYNC 通知 | 人为改一端可检出 |
| **S6** | 断线退房、删房回收、token 过期 | 无僵尸房间 |

---

## 10. 功能点清单（纳入实现 backlog）

### D1. 登录服

| ID | 功能点 |
|----|--------|
| D1.1 | TCP luabus 风格接入与主循环 `wait` |
| D1.2 | `reg_req` / `login_req` / `heartbeat` / `profile_get` |
| D1.3 | Token 签发与过期 |
| D1.4 | 供 Room 的 `token_validate` |

### D2. 存储（与逻辑线程队列分离）

| ID | 功能点 |
|----|--------|
| D2.1 | SQLite `users` / `sessions` |
| D2.2 | **Logic Thread ↔ Storage Thread**：`req_queue` / `rsp_queue` |
| D2.3 | Logic 禁止直接 SQL；`req_id` 异步回包 |
| D2.4 | WAL、用户名唯一、错误码 |

### D3. 房间转发服

| ID | 功能点 |
|----|--------|
| D3.1 | TCP：`auth` + **`room_list` / `room_query` / `room_create` / `room_delete`** |
| D3.2 | `room_join` / `leave` / `room_update` / `match_start` |
| D3.3 | UDP+KCP 会话（conv、slot） |
| D3.4 | `CMD_FRAME` / `HASH` 房间广播 |
| D3.5 | 房间状态机、删房/空房回收 |
| D3.6 | 可选 DESYNC 广播 |

### D4. 客户端（与服务器接口对称）

| ID | 功能点 |
|----|--------|
| D4.1 | Login 客户端（TCP 消息） |
| D4.2 | `RoomClient_ListRooms` / `QueryRoom` / `CreateRoom` / `DeleteRoom` |
| D4.3 | `Join` / `Leave` / `StartMatch` + 回调 |
| D4.4 | Room TCP + KCP；`LS_Service` 经 Relay |
| D4.5 | 保留 local P2P / `local_only` 调试开关 |

---

## 11. 明确非目标（本阶段）

- 服务器权威模拟 / 反外挂完整方案  
- 跨房间匹配撮合（先手动建房进房）  
- 登录服与房间服分布式集群、多 SQLite 分片  
- 将 luabus 原样无改塞进引擎主线程（引擎侧用薄客户端封装即可）

---

## 12. 结论

需要落地的后端是两进程（可同机）：

1. **登录服**：TCP + luabus 风格消息；**Logic / Storage 双线程用队列分离**；SQLite 仅存储线程访问；用户名注册即可。  
2. **房间指令转发服**：TCP 提供 **查询 / 创建 / 删除房间**（及进房开局）；**UDP+KCP** 转发锁步指令；不跑玩法。  
3. **客户端**：`RoomClient_ListRooms` / `QueryRoom` / `CreateRoom` / `DeleteRoom` 与服务器消息一一对应。  

网络模型对齐 [luabus](https://github.com/trumanzhao/luabus)；指令面用 KCP。齐票与模拟仍在客户端 lockstep。
