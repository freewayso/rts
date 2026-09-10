# Permafrost 帧同步方案（KCP + 追帧）

把当前单机 RTS 循环改成「确定性逻辑锁步 + 本地实时渲染」。传输用 KCP。掉线/重连用「快照 + 指令回放追帧」，不从开局整场重打。

光照、阴影、镜头、UI 仍在本机实时算，不进锁步。

---

## 1. 目标与非目标

**目标**

- 2～8 人同图对战，逻辑结果一致。
- 网上只传操作（选人、移动、攻击、技能、暂停），不传每个单位的位置。
- 短暂掉线、卡顿、切后台后能追帧跟上，不必整局重开。
- 渲染帧率与逻辑 tick 解耦：逻辑固定 10Hz，画面仍 60 帧插值。

**非目标**

- 不做王者那种状态同步（每帧同步血量坐标）。
- 不把 Phong 光照、阴影贴图、迷雾 GPU 上传、镜头插值纳入校验。
- 第一期不做反外挂、匹配、房间大厅的完整产品化。

---

## 2. 现状（必须改掉的点）

| 模块 | 现状 | 对锁步的影响 |
|---|---|---|
| 逻辑时钟 | `src/game/timer_events.c` 用 SDL 墙钟 60Hz 定时器拆 20/10/5/1Hz | 各机器帧率/卡顿不同，tick 对不齐 |
| 移动 | CPU + `shaders/compute/movement.glsl` | GPU 浮点与调度顺序不确定 |
| 战斗 | `src/game/combat.c`，默认约 10Hz | 可对齐到逻辑 tick，但依赖墙钟 |
| 脚本 | Python 2.7 单位 AI / 刷怪 | dict 顺序、GC、随机源都会导致不同步 |
| 存档 | `pf.save_session` / `load_session` | 可作追帧快照基础 |
| 网络 | 无 | 需新增 |
| 光照 | 实时 Phong + 方向光阴影 | **不用改**，继续本机算 |

结论：锁步的工作量在「确定性模拟 + 命令通道」，不在光照。

---

## 3. 总体架构

```
┌─────────────┐     操作命令      ┌─────────────┐
│  本机输入    │ ───────────────► │  命令缓冲    │
│ 选/走/打/技能│                  │ frame N+Delay│
└─────────────┘                  └──────┬──────┘
                                       │ KCP 可靠有序
                                       ▼
┌─────────────┐   齐帧后执行      ┌─────────────┐
│ 确定性模拟   │ ◄─────────────── │  帧门闩      │
│ 移动/战斗/FOW│   固定 tick      │  等齐全员    │
└──────┬──────┘                  └─────────────┘
       │ 逻辑状态（位置、血量、指令队列）
       ▼
┌─────────────┐
│ 本地渲染     │  插值、Phong、阴影、迷雾贴图、UI
└─────────────┘
```

三条时间线：

1. **逻辑 tick**（例如 10Hz，`TickId` 从 0 递增）。所有端同一 `TickId` 执行同一批命令。
2. **网络**。KCP 保证命令帧可靠、有序。
3. **渲染**。继续跟显示器走，在相邻两个逻辑 tick 之间插值单位位置。

建议逻辑步长与现有战斗频率对齐：**10Hz（100ms）**。移动不要再用独立的 20Hz 墙钟；每逻辑 tick 走固定 `dt = 0.1s`。

输入延迟：`COMMAND_DELAY = 2` 个 tick（约 200ms）。本机在 tick `T` 发出的命令，在 `T+2` 才执行。延迟换齐帧，这是星际/魔兽那一套。

---

## 4. 为什么用 KCP

锁步要求：**命令不能丢、不能乱序、延迟尽量低**。

| | TCP | 裸 UDP | KCP |
|---|---|---|---|
| 可靠 | 有 | 无 | 有（ARQ） |
| 队头阻塞 | 严重 | 无 | 可调，弱于 TCP |
| 握手/NAT | 现成 | 自己做 | 需自建或 STUN，包更小 |
| RTS 适配 | 卡一下后面全堵 | 要自写重传 | 适合指令流 |

KCP 只做传输。上面再封一层「帧协议」：

- 每条业务包带 `TickId` + `PlayerId` + `Payload`。
- 心跳、快照、追帧请求走同一条 KCP 连接的不同 `msg_type`。
- KCP 参数建议（可再调）：`nodelay(1, 10, 2, 1)`，`wndsize(128, 128)`，`mtu=470`（弱网）或 1400（局域网）。

主机模型第一期用 **Listen Server（房主权威只做转发 + 快照）**：每端仍各自模拟，房主负责集齐命令、广播、存快照。不做独立 dedicated 也可先跑通。

---

## 5. 协议

### 5.1 包头

```
uint8  magic = 0x50
uint8  msg_type
uint32 tick          // 逻辑帧号
uint8  player_id
uint16 payload_len
uint32 checksum      // 该 tick 模拟后的状态摘要（见 7.3）
bytes  payload
```

### 5.2 消息类型

| msg_type | 方向 | 含义 |
|---|---|---|
| `CMD_FRAME` | C→Host→All | 某玩家在某 tick 的命令列表（可为空） |
| `CMD_EMPTY` | 同上 | 该 tick 无操作，也必须发，否则门闩等不到 |
| `HEARTBEAT` | 双向 | 带本地 `TickId`、RTO |
| `HASH_REPORT` | C→Host | 本 tick 模拟校验和 |
| `DESYNC_ALERT` | Host→All | 校验和不一致 |
| `SNAP_REQ` | C→Host | 掉线重连：请求快照 |
| `SNAP_BEGIN` | Host→C | 快照元数据：`snap_tick`、块数、校验 |
| `SNAP_CHUNK` | Host→C | 快照分片（KCP 流式） |
| `CMD_REPLAY` | Host→C | `snap_tick+1 … host_tick` 的命令日志 |
| `CATCHUP_DONE` | C→Host | 本地已追到 `host_tick`，重新进锁步 |
| `LEAVE` / `KICK` | — | 退出 |

### 5.3 命令 payload（逻辑输入）

只编码玩家意图，不编码结果。

```
Move      { uid_list, xz }
Attack    { uid_list, target_uid | xz }
Stop      { uid_list }
Hold      { uid_list }
Select    不进模拟（纯 UI，可不上网）
Spawn     { type, xz, faction }   // 仅主机脚本/裁判，或开局种子
Pause     { on/off }              // 需全员确认
```

空帧也要发：锁步门闩按「全员该 tick 的包都到了」才推进。

随机：开局交换 `seed`。模拟内禁止 `rand()` 直接调系统时间；统一 `SimRand(tick, uid)`。

---

## 6. 锁步推进

每个客户端：

```
while running:
    收集本机输入 → 打成 CMD_FRAME(tick = local_tick + DELAY) → KCP 发出
    若 buffer[exec_tick] 已齐全员命令:
        按 player_id 排序后依次 Apply
        StepSimulation(dt = 0.1)
        exec_tick += 1
        计算 checksum，发给 Host
    渲染：用 exec_tick-1 与 exec_tick 的状态插值
```

**齐帧超时**（例如 150ms）：缺谁的包就向 Host 重要；KCP 自己也会重传。连续缺席超过 `STALL_TICKS`（如 30 tick = 3s）视为掉线，进入第 8 节。

**追赶时不渲染逐步动画**：`catching_up == true` 时只跑 `StepSimulation`，跳过阴影/骨骼/UI，直到 `exec_tick == host_tick`。

---

## 7. 确定性改造（本引擎落地清单）

没有这一步，KCP 传得再可靠也会「看起来在同步、打着打着兵走位不一样」。

### 7.1 时钟

- 删除模拟路径对 `SDL_GetTicks` / `timer_callback` 的依赖。
- 新增 `Sim_AdvanceTick()`，只在门闩放行时调用一次。
- `EVENT_10HZ_TICK` 改为由 `Sim_AdvanceTick` 派发，不再由墙钟拆出来。
- 渲染插值仍可用渲染帧时间，但不得写回单位逻辑坐标。

### 7.2 移动 / 战斗 / 迷雾

- 移动：锁步模式下关掉 compute shader 路径，走单一 CPU 顺序（按 `uid` 排序）。
- 战斗：每 tick 一次，`dt` 固定；目标选择、伤害结算按 `uid` 稳定排序。
- 迷雾：引用计数与 stamp 可以留，但遍历顺序要稳定；不要用「本帧 GPU 是否上传成功」影响逻辑可见性判定。
- 禁止「这一帧时间预算不够就少算几个单位」。

### 7.3 校验和

每 tick 对逻辑状态做 hash，例如：

```
h = hash(tick)
for uid in sorted(uids):
    h = hash(h, uid, pos_x_bits, pos_z_bits, hp, stance, target)
```

浮点先量化到固定精度（如 1/1024 世界单位）再 hash。Host 对比各端 `HASH_REPORT`，不一致则 `DESYNC_ALERT`，打日志并可选暂停。

### 7.4 Python

原则：**模拟不要过 Python**。

- 单位 AI、自动攻击、刷怪改 C，或只在 tick 边界用「确定性脚本沙箱」（禁止 `dict` 迭代作为逻辑顺序、禁止 `time`/`random`）。
- UI、设置窗、中文按钮、控制台可以继续 Python，因为不上锁步。
- 现有 `scripts/rts/spawn_mobs.py` 随机刷怪必须改为：Host 在 tick 上发出 `Spawn` 命令，或用开局 seed 在各端同步生成。

### 7.5 现有存档

`save_session` / `load_session` 作为快照编解码基础。追帧快照需要额外带上：

- `snap_tick`
- 开局 seed
- 各玩家 `player_id` 映射
- 该 tick 之后尚未执行的命令队列

逻辑快照不要包含摄像机、字体图集、阴影贴图。

---

## 8. 掉线与追帧

### 8.1 判定

- KCP `timeout`（如 5s 无包）或连续 `STALL_TICKS` 未交命令 → `DISCONNECT`。
- 短暂超时先当「卡住」：其它人可以选「全体暂停等他」或「用空帧继续」（该玩家本 tick 视为无操作）。第一期建议 **暂停等待**，避免空帧把战局打飞后再追。
- 超过 `DROP_SECONDS`（如 30s）仍未回来 → 踢出或交 AI（第二期）。

### 8.2 重连流程

```
重连客户端                    Host
    │  KCP 握手 + 局内 token
    │  SNAP_REQ(last_local_tick)
    │─────────────────────────►
    │                          取最近关键快照 S（snap_tick ≤ host_tick）
    │  SNAP_BEGIN / SNAP_CHUNK
    │◄─────────────────────────
    │  CMD_REPLAY(S+1 .. host_tick)
    │◄─────────────────────────
    │  load(S)
    │  for t in S+1 .. host_tick:
    │      Apply(cmds[t]); StepSimulation()   // 追帧，不渲染
    │  CATCHUP_DONE(host_tick)
    │─────────────────────────►
    │  恢复齐帧锁步 + 正常渲染
```

快照间隔建议每 **50 tick（5s）** 在 Host 内存留一份环形缓冲（保留最近 12 份 ≈ 1 分钟）。命令日志保留「最老快照之后」的全部 `CMD_FRAME`。

### 8.3 追帧性能

- 目标：1 分钟缺口在 1～2 秒内追上（600 tick 的 10Hz 模拟，关掉渲染/阴影/Python UI）。
- 单 tick 预算在追帧模式放宽，可连续 `StepSimulation` 直到追上或本帧超时（如 8ms），剩余下一渲染帧继续追。
- 追上前输入进入本地队列，但 **不得提前执行**；显示「正在同步…」。

### 8.4 为什么不从第 0 帧重放

整局重放实现简单，但 15 分钟对局 = 9000 tick，重连要卡很久。快照 + 尾部命令是 RTS 标配。本引擎已有 session 序列化，应复用而不是新做一套世界格式。

### 8.5 掉线期间其它玩家

第一期：Host 广播 `PAUSE`，全员逻辑停在同一 tick。重连者追到该 tick 后 `RESUME`。

第二期：允许空帧推进；重连者快照必须是「当前 host_tick」，追帧量 ≈ 掉线时长。

---

## 9. 渲染与光照（明确不进锁步）

继续本机实时：

- Phong + `light_pos` / `ambient_color`
- 方向光阴影贴图（跟镜头走）
- 骨骼动画插值、迷雾纹理上传
- RTS 镜头、UI

逻辑只提供「单位在 tick T 的坐标/朝向/动画状态机」。渲染在 T 与 T+1 之间插值。两台机器阴影分辨率不同、FOV 不同，不影响校验和。

---

## 10. 模块划分（建议新增）

```
src/net/kcp_udp.c          KCP + UDP socket，更新 ikcp_update
src/net/protocol.c         包头编解码、分片快照
src/net/cmd_buffer.c       各玩家每 tick 命令槽、齐帧检测
src/sim/sim_clock.c        TickId、DELAY、暂停
src/sim/sim_hash.c         状态校验和
src/sim/catchup.c          禁渲染快进 StepSimulation
```

现有改动点：

- `timer_events.c`：模拟 tick 改由 `sim_clock` 驱动
- `movement.c`：锁步关闭 GPU compute
- `combat.c`：只在 `Sim_AdvanceTick` 里跑
- `scripts/rts/spawn_mobs.py`：改为同步 `Spawn` 命令或 seed

第三方：引入 `ikcp`（KCP 官方 C 实现），体积很小，适合放 `deps/kcp`。

---

## 11. 分阶段

**P0 单机假锁步**

- 逻辑与渲染时钟分离，固定 10Hz `StepSimulation`。
- 本机命令进 buffer，`DELAY=2` 后执行（单机也能感到 200ms 操作延迟）。
- 关掉 GPU 移动；校验和打到日志。
- 不接网络。

**P1 双机局域网**

- KCP 连上，`CMD_FRAME` 齐帧。
- `HASH_REPORT` 对不上就暂停并 dump。
- 先 1v1、小兵数量。

**P2 快照追帧**

- Host 周期快照（复用 session 序列化）。
- 模拟断线：杀掉 KCP → 重连 → `SNAP_*` + `CMD_REPLAY` → `CATCHUP_DONE`。
- 追帧时关闭阴影与 UI。

**P3 弱网 / 多人**

- 调 KCP nodelay、空帧策略。
- 4 人、刷怪改确定性。
- 超时踢人、暂停等待的 UI。

---

## 12. 风险

- **Python 逻辑**：不迁走，校验和几乎过不了。优先把战斗/寻路/刷怪留在 C。
- **浮点**：不同 CPU 的 SIMD 路径要统一（锁步关 AVX 特例或量化）。
- **快照体积**：整局 session 可能数 MB，必须分片 + KCP 流式；追帧前先压一次（只存逻辑实体）。
- **延迟**：10Hz + DELAY=2 ≈ 200ms，RTS 可接受；不要学 FPS 去堆预测。
- **KCP 不是银弹**：不解决不同步；只保证「同一 tick 的命令，大家都收到且顺序一样」。

---

## 13. 验收

1. 两台机器开同一张 `demo` 图，各操一队，打 3 分钟，每 tick 校验和一致。
2. 中途拔网 5 秒再连上：追帧进度提示出现，追上后校验和继续一致，战斗不回滚错乱。
3. 一台 30fps、一台 60fps，逻辑结果仍一致（画面流畅度可以不同）。
4. 关阴影、改分辨率，校验和不变。

---

## 14. 和当前 demo 的关系

- `run_demo.bat` / `scripts/rts/main.py` 仍作单机入口。
- 联机入口另加，例如 `scripts/rts/main_net.py` + `run_net.bat`，避免把 KCP 强行塞进现有演示。
- 随机刷怪（`spawn_mobs.py`）在联机模式必须改成 seed 或 `Spawn` 命令，否则一开局就会不同步。
