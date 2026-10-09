# Permafrost 重构功能点清单

> 基于当前仓库状态整理：桌面单机可玩；Android / 联机 / 后端均为未完成。  
> 相关文档：`docs/lockstep_*.md`、`docs/server_login_room_relay.md`、`android/README.md`。  
> 本文档回答：**要重构/补齐哪些功能点、优先级、依赖与验收标准**。

---

## 1. 总览

当前需要系统性重构/落地的工作分成四条主线：

| 主线 | 目标 | 现状 |
|------|------|------|
| **A. Android 移植** | arm64 `libpermafrost.so` + GLES3 + 无 Python 可玩 | 工程/JNI/三角存根有；未链全引擎、未真机验收 |
| **B. 联机 Lockstep** | 指令真同步 RTS 对战 | `src/net` + `src/lockstep` 骨架有；未进 Makefile、未接主循环 |
| **C. 引擎可移植性** | 平台抽象、图形/脚本/路径解耦 | 桌面 GL3.3+GLEW+Py2.7 耦合深 |
| **D. 联机后端** | 登录服 + 房间指令转发服（TCP + UDP/KCP） | **S0–S2 + 客户端 API 已落地**（`server/`）；KCP/联调待续 |

建议：**先 C 打底 → D 登录/存储 → B+D 经 Relay 锁步 → A 出画面与手机 RTS**。  
正式联机以 **Room Relay** 转发指令；P2P Host 仅保留调试。

---

## 2. 主线 A — Android / GLES / 无 Python

### A1. 构建与宿主（Phase 0）

| ID | 功能点 | 说明 | 验收 |
|----|--------|------|------|
| A1.1 | NDK CMake 编 `libpermafrost.so` | ABI 主推 `arm64-v8a`；后续可选 `armeabi-v7a` | 产出 so |
| A1.2 | Gradle APK + Activity | 加载 so，生命周期，触控转发 | 安装启动不崩 |
| A1.3 | Assets 打包与解压 | `assets/`、`shaders_es/` → `filesDir`，设 `g_basepath` | 可读写路径存在 |
| A1.4 | JNI / C API 稳定面 | `pf_android_init/frame/touch/pause/resume/shutdown/set_quality` | Logcat 每帧心跳 |

**现状**：`android/`、`jni/` 已有骨架。  
**缺口**：本机 SDK/NDK、实际编包、真机跑通。

### A2. 图形栈（Phase 1 / 3）

| ID | 功能点 | 说明 | 验收 |
|----|--------|------|------|
| A2.1 | 去掉 GLEW 依赖路径 | `render.c` 走 `gles_loader` / `PF_GLES` | Android 可 init GL |
| A2.2 | EGL / ES 3.0 Context | 现 `GLSurfaceView`；可选换 SDL2 Android | 清屏稳定 |
| A2.3 | 最小绘制 | 清屏 + 三角形 | ~60fps |
| A2.4 | Shader ES 迁移 | `shaders_es/`（`#version 300 es` + precision） | 地形/实体程序可链 |
| A2.5 | 禁用不支持特性 | 无 geometry；compute 默认关；`PF_GLES` 处理 clip 等 | 编译/运行不炸 |
| A2.6 | 水特效降级 | 默认关反射/折射（`PF_GLES_WATER_FX=0`） | 手机可接受帧率 |
| A2.7 | 渲染线程模型 | 先单 GL 线程执行 `R_PushCmd`；后再双线程 | 无 EGL 线程错乱 |

**涉及目录**：`src/render/*`、`shaders/` → `shaders_es/`、`src/render/pf_gles.h`。

### A3. 启动与脚本旁路（Phase 2）

| ID | 功能点 | 说明 | 验收 |
|----|--------|------|------|
| A3.1 | `PF_NO_PYTHON` | 旁路 `S_Init` / `S_RunFileAsync` | 无 libpython 可启 |
| A3.2 | C Bootstrap 加载地图 | `G_Init` → 内置 PFMAP（如 `demo.pfmap`）→ 相机 | 进主循环 |
| A3.3 | 拆分 `engine_init` | 桌面 `main` 与 `PF_Android_Run` 共用核心 | 双端不复制逻辑 |
| A3.4 | CMake 链入引擎源 | 现 so 仅 jni 存根，需分批加入 game/map/render | 能链、能跑 |

**涉及**：`src/main.c`、`jni/android_engine.c`、`android/CMakeLists.txt`。

### A4. 玩法 C 切片 + 触控（Phase 4）

| ID | 功能点 | 说明 | 验收 |
|----|--------|------|------|
| A4.1 | 触控映射 | 点选、拖相机、双指缩放 | 操作手感可用 |
| A4.2 | 选中 / 移动 | 调 `G_Sel_*` / `G_Move_*`（非仅 LOG） | 单位可移动 |
| A4.3 | 战斗 / 建造切片 | `G_Combat_*` / builder API | 小规模可玩 |
| A4.4 | 性能档 | `set_quality`：限单位、关水、降 RT | 中端机可玩 |

**现状**：`android_rts_slice.c` / `android_rts_combat.c` 多为意图日志。  
**缺口**：真正接到 `G_*`。

### A5. 硬化（Phase 5）

| ID | 功能点 | 验收 |
|----|--------|------|
| A5.1 | pause/resume 停模拟、EGL 丢失重建 | 切后台不崩 |
| A5.2 | 画质持久化 | 重启保持档位 |
| A5.3 | 多 ABI（可选） | armeabi-v7a 可装 |
| A5.4 | 崩溃堆栈 / 日志 | 可定位 |

---

## 3. 主线 B — 联机 Lockstep

> 设计文档已在 `docs/lockstep_*.md`。此处只列**待落地重构/接线功能点**。

### B1. 工程接入（必须先做）

| ID | 功能点 | 说明 | 验收 |
|----|--------|------|------|
| B1.1 | Makefile / VS 工程编入 `src/net`、`src/lockstep` | 当前未进构建 | 链接成功 |
| B1.2 | `main` 主循环调用 `Net_Service` / `LS_Service` | 当前未接 | 每帧泵网络与锁步 |
| B1.3 | 模式切换 `GAME_MODE_SP` / `MP_LOCKSTEP` | UI 或启动参数 | 可进联机模式 |

### B2. 传输与大厅

| ID | 功能点 | 说明 | 验收 |
|----|--------|------|------|
| B2.1 | 可靠 UDP 通道稳定化 | `src/net`：可靠/不可靠、ACK、断线 | LAN 互通 |
| B2.2 | Lobby Host / Join | 玩家槽、名字、地图、派系 | 两人进同一房间 |
| B2.3 | `Lobby_StartMatch` | 广播 `rng_seed`、`sim_hz`、`command_delay`、地图 | 双方同配置开局 |

### B3. 锁步核心

| ID | 功能点 | 说明 | 验收 |
|----|--------|------|------|
| B3.1 | SimClock 固定步长 | 默认 10Hz，与渲染解耦 | 同 tick 推进 |
| B3.2 | Command delay buffer | 默认 delay=2，掩盖 RTT | 不因一帧延迟抖停 |
| B3.3 | 指令集统一入口 | 移动/攻击/建造等只走 `LS_Issue*` | SP/MP 行为一致 |
| B3.4 | 全玩家指令拦截 | `movement` 已有部分；combat/build/生产等需统一 | 无「本地直改权威」旁路 |
| B3.5 | 帧屏障 | 齐票才 `TryAdvanceSim` | 不同步不单端狂奔 |
| B3.6 | 校验哈希 + 不同步处理 | 周期性 hash，desync 停局打日志 | 可复现定位 |

### B4. 确定性重构（高风险）

| ID | 功能点 | 说明 | 验收 |
|----|--------|------|------|
| B4.1 | 去墙钟驱动玩法 | 战斗/移动/AI 计时改 SimClock | 同输入同结果 |
| B4.2 | 确定性 RNG | 全部玩法随机走 `LS_Rng*` | 录像可复现 |
| B4.3 | 遍历/集合顺序 | 禁止依赖指针地址、hashmap 无序 | 双端 hash 一致 |
| B4.4 | 浮点约定 | 统一编译选项 / 关键路径定点或一致 FPU | CI 双端 hash |
| B4.5 | Python MP 规则 | MP 下禁非确定性脚本或迁 C/Lua | 文档见 `lockstep_scripting.md` |

### B5. 会话与运维

| ID | 功能点 | 验收 |
|----|--------|------|
| B5.1 | 本地 fake dual lockstep | 单机双逻辑玩家验证确定性 |
| B5.2 | 录像 / 回放 | 指令日志可回放 |
| B5.3 | 断线 / 踢人 | 干净结束或暂停（v1 可不做迁移 Host） |
| B5.4 | 晚加入（可选后期） | 快照 + session 追帧 |

### B6. Android 联机（依赖 A+B）

| ID | 功能点 | 说明 |
|----|--------|------|
| B6.1 | 移动网络权限与后台 | 已有 `INTERNET`；需处理切后台 |
| B6.2 | NAT / 仅 LAN 策略 | 手机联机优先同 Wi‑Fi；公网需后续打洞/中继 |

---

## 4. 主线 C — 跨端可移植性（支撑 A/B）

| ID | 功能点 | 说明 |
|----|--------|------|
| C1 | 平台宏统一 | `__ANDROID__` / `PF_GLES` / `PF_NO_PYTHON` / `PF_ANDROID_FULL` |
| C2 | Fiber / 调度 | 桌面 x86_64 + Android AArch64；可先单线程再开 fiber |
| C3 | 分配器 | Android 先关 mimalloc/CoW 或换系统分配 |
| C4 | 音频 | OpenAL Soft Android 构建与生命周期 |
| C5 | 路径与 I/O | 统一经 `g_basepath` + SDL_RWops / 预解压 |
| C6 | 脚本策略 | 短期无 Py；长期若要脚本优先 **Lua**，不复活 Py2.7 |
| C7 | 配置/画质 | 桌面 Settings 与 Android `QualityPrefs` 语义对齐 |

---

## 5. 建议实施顺序（里程碑）

```text
M0  环境：Android SDK/NDK；桌面把 net/lockstep 编进工程
M1  Android 三角 APK；桌面 LS fake dual + 指令录像 hash
M2  Android 链 game/map，无 Python 出地形；桌面 LAN 两人移动同步
M3  ES 地形+单位；锁步战斗/建造指令齐全 + hash
M4  Android 触控 RTS 切片；桌面断线/录像工具
M5  画质/生命周期硬化；可选公网与多 ABI
```

---

## 6. 明确「暂不重构 / 非目标」

| 项 | 原因 |
|----|------|
| 嵌入 Python 2.7 到 Android | ABI/包体/确定性地狱 |
| 状态同步全量刷实体 | RTS 不适用；已定 lockstep |
| Host 迁移（v1） | 复杂度高，先干净退局 |
| 完整反作弊 / 加密协议（v1） | 先 hash + 踢人 |
| 一次到位全特效 GLES | 先地形实体，水/阴影后置 |

---

## 7. 模块 ↔ 功能点速查

| 模块路径 | 主要重构点 |
|----------|------------|
| `android/`、`jni/` | A1–A5 宿主与 so |
| `src/render/`、`shaders_es/` | A2 GLES |
| `src/main.c` | A3 启动拆分；B1 主循环接入 |
| `src/game/*` | A4 / B3 指令统一入口、确定性 |
| `src/net/`、`src/lockstep/` | B2–B5 联机 |
| `src/sched/` | C2 AArch64 fiber |
| `scripts/rts/` | A3 旁路；B4 MP 脚本约束；长期迁 C/Lua |

---

## 8. 主线 D — 登录服 + 房间指令转发服（摘要）

完整设计见 **[`server_login_room_relay.md`](server_login_room_relay.md)**。

| ID | 功能点 | 要点 |
|----|--------|------|
| D1 | 登录服 | TCP；luabus 风格；注册/登录发 token |
| D2 | SQLite 存储 | 用户名注册；**Logic↔Storage 双队列分离** |
| D3 | 房间转发服 | TCP：查询/创建/删除/进房/开局；**UDP+KCP** 指令转发 |
| D4 | 客户端 | `RoomClient_List/Query/Create/Delete` 与服务器对称；`LS_*` 经 Relay |

通道约定：**房间指令同步用 UDP+KCP，其它用 TCP**；**载荷统一 Protocol Buffers**（`server/proto/`）。

---

## 9. 文档关系

| 文档 | 用途 |
|------|------|
| **本文** `docs/refactor_backlog.md` | 重构功能点总清单与优先级 |
| `docs/server_login_room_relay.md` | 登录服 / 房间转发服 / SQLite / TCP·KCP 实现方案 |
| `docs/lockstep_*.md` | 客户端锁步设计细则 |
| `android/README.md` | Android 工程构建说明 |
| Cursor 计划 `android_so_port_*.plan.md` | Android 分阶段方案（勿当实现进度） |

---

## 10. 一句话结论

**要落地的是四块能力：**  
① 引擎从桌面 GL+Python 解耦到可 GLES / 无脚本启动；  
② 玩法输入收束进 lockstep 并保证确定性；  
③ Android so + 触控跑通 C 玩法切片；  
④ **登录服 + 房间 KCP 转发服**（账号 SQLite，指令不经玩家 Host）。  

当前仓库：**客户端骨架与后端设计文档已有，服务端与真机/联机验收尚未完成。**
