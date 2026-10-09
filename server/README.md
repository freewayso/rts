# Permafrost online servers (Login + Room Relay)

## Build (MinGW / MSVC / Linux)

```bash
cd server
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Windows MinGW example:

```bat
cd server
cmake -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe
cmake --build build
```

Binaries: `build/login_server`, `build/room_server`.

## Run

```bat
mkdir data
build\login_server.exe 27000 data\account.db
build\room_server.exe 27001 27002
```

## Storage

- Logic thread ↔ Storage thread via `req_queue` / `rsp_queue` (see `storage/`).
- Default backend: file DB (`FileAccountDb`) with same schema semantics.
- If SQLite amalgamation is placed under `third_party/sqlite/...`, CMake defines `PF_HAS_SQLITE`.

## Windows client UI

```bat
cmake --build build --target online_client_ui
run_win_demo.bat
```

Or manually:

1. `login_server.exe 27000 data\account.db`
2. `room_server.exe 27001 27002`
3. `online_client_ui.exe` — 注册/登录 → 进房间服 → 创建/刷新/加入/删除 → 开始对局

## Protocol


Wire: **Protocol Buffers** (`server/proto/*.proto`). Frame header `PFPB` + `msg_id` + payload. See `docs/server_login_room_relay.md` §3.4.

Regenerate:

```bat
protoc -I proto --cpp_out=proto/gen proto/pf_common.proto proto/pf_login.proto proto/pf_room.proto
```

Requires MinGW `protobuf` package (`pacman -S mingw-w64-x86_64-protobuf`).

