# 本地编译 / 启动 / 联机测试

适用环境：**Windows + MSYS2 MinGW64**。仓库根目录下文记为 `ENGINE_ROOT`（例如 `D:\study\permafrost-engine-master`）。

同步到 Redmine Wiki：配置 `REDMINE_URL` / `REDMINE_KEY` / `REDMINE_PROJECT` 后执行 `tools/publish_redmine_wiki.ps1`。

---

## 0. 前置

1. 安装 [MSYS2](https://www.msys2.org/)，默认路径 `C:\msys64`
2. 在 MSYS2 中安装工具链（若尚未安装）：

```bash
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-make mingw-w64-x86_64-cmake mingw-w64-x86_64-protobuf
```

3. PowerShell 里把 MinGW 放进 PATH（每次新开终端建议执行一次）：

```powershell
$env:PATH = "C:\msys64\mingw64\bin;C:\msys64\usr\bin;" + $env:PATH
```

---

## 1. 编译引擎 (`pf.exe`)

```powershell
cd ENGINE_ROOT
$env:PATH = "C:\msys64\mingw64\bin;C:\msys64\usr\bin;" + $env:PATH
# 首次或依赖变更时：
make deps PLAT=WINDOWS
# 日常改 C 代码后：
make pf PLAT=WINDOWS
```

产物：`lib\pf.exe`（以及 `lib\pf-mi.exe` 等）。

只改单个目标时示例：

```powershell
make PLAT=WINDOWS obj/game/selection.o lib/pf.exe
```

---

## 2. 编译登录服 / 房间服 / 大厅 UI

```powershell
cd ENGINE_ROOT\server
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe `
  -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe
cmake --build build
```

产物（均在 `server\build\`）：

| 文件 | 作用 |
|------|------|
| `login_server.exe` | 登录 / 账号 |
| `room_server.exe` | 房间 + 指令转发 |
| `online_client_ui.exe` | 大厅客户端 UI |

---

## 3. 一键启动双端联机测试

```powershell
cd ENGINE_ROOT\server
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\start_local_test.ps1 -Clients 2
```

脚本会：

1. 停掉旧的 `login_server` / `room_server` / `online_client_ui` / `pf`
2. 启动登录服 `tcp:27000`，房间服 `tcp:27001` / `udp:27002`
3. 打开 **2** 个大厅 UI 窗口

常用参数：

```powershell
.\scripts\start_local_test.ps1 -Stop          # 只停止
.\scripts\start_local_test.ps1 -NoUi          # 只起服务器
.\scripts\start_local_test.ps1 -Clients 1     # 只开 1 个 UI
```

也可双击：`server\scripts\start_local_test.bat`。

---

## 4. 测试流程（大厅 → 对局）

在两个 `online_client_ui` 窗口中：

1. **注册 / 登录**（账号存在 `server\data\account.db`）
2. 进入 **Online / 房间列表**
3. **客户端 A**：创建房间  
4. **客户端 B**：刷新列表 → 加入同一房间  
5. 房主点 **开始对局** → 两端拉起 `pf.exe`，加载 `scripts/rts/match_online.py`
6. 验证要点：
   - 本方部队标识为**蓝色**，敌方为**深红**（非绿）
   - A 移动 / 攻击后，B 能看到对应单位与战斗
   - 飘血数字节奏适中；河岸观感可接受
   - 控制台无明显 lockstep 超时 / mimalloc 崩溃

停止：

```powershell
cd ENGINE_ROOT\server
.\scripts\start_local_test.ps1 -Stop
```

---

## 5. 改代码后的最小重编清单

| 改动位置 | 重编命令 |
|----------|----------|
| `src/**/*.c`（引擎） | `make PLAT=WINDOWS lib/pf.exe` |
| `shaders/**/*.glsl` | 一般无需重编，**重启 `pf.exe`** 即加载 |
| `scripts/rts/*.py` | 无需重编，**重新开局**即可 |
| `server/**` | `cmake --build server/build` |
| 联机全链路 | 重编后执行 `start_local_test.ps1` |

---

## 6. 端口与路径速查

| 项 | 值 |
|----|-----|
| 登录 TCP | `27000` |
| 房间 TCP | `27001` |
| 房间 UDP | `27002` |
| 账号库 | `server/data/account.db` |
| 引擎根（UI 用） | 环境变量 `PF_ENGINE_ROOT` = `ENGINE_ROOT` |
| 对局脚本 | `scripts/rts/match_online.py` |

---

## 7. 常见问题

- **缺 exe / DLL**：确认 PATH 含 `C:\msys64\mingw64\bin`，并在 `server` 下重新 `cmake --build build`。
- **UI 起得来但对局起不来**：确认 `lib\pf.exe` 存在；看 UI / 引擎工作目录与 `PF_ENGINE_ROOT`。
- **一端看不见另一端指令**：确认两端都进入同一房间且已 Start Match；查 lockstep 日志是否仍有超时。
- **`fatal: not a git repository`**：`make` 打印该警告可忽略（本机可用 `GIT_VERSION=nogit`）。

相关文档：[`server/README.md`](../server/README.md)、[`server_login_room_relay.md`](server_login_room_relay.md)、[`lockstep_ops.md`](lockstep_ops.md)。
