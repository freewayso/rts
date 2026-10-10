# Permafrost Engine — Android (libpermafrost.so)

## Current status (APK / .so)

| Question | Answer |
|----------|--------|
| Can you build an Android APK? | **Yes.** `gradlew :app:assembleDebug` |
| Full RTS linked into `.so`? | **Yes.** Gradle sets `-DPF_ANDROID_LINK_ENGINE=ON` |
| LAN multiplayer (P2P lobby + lockstep)? | **Yes (batch 2).** Real `net.c`/`lobby.c`; UI = Lobby panel |
| Boots map + scene on device? | **Yes (when assets present).** `G_LoadMap` / `Scene_Load` |
| Touch controls? | Drag = RTS middle-button pan; tap = left-click select / move |
| Caveats | Desktop GL stubbed on ES. Audio/Python stubbed. No online room server / NAT yet. |

Architecture:

```text
APK (GLSurfaceView GLES3 + Lobby overlay)
 └── JNI → libpermafrost.so (engine-linked, batch 2)
      ├── G_Init / R_InitFromCurrentContext / map load
      ├── Lobby_Host|Join → LS_Init on START_MATCH
      └── each frame: E_ServiceQueue → LS_Service → G_Update → G_Render → G_SwapBuffers
```

Defines: `PF_GLES`, `PF_ANDROID_FULL`, `PF_NO_PYTHON`, `PF_NO_MIMALLOC`, `PF_GLES_WATER_FX=0`, `PF_ANDROID_ENGINE_LINKED=1`, `PF_ANDROID_HAVE_NET=1`.

---

## Requirements

- Android Studio Hedgehog+ / AGP 8.x
- NDK r26+ and CMake 3.22.1
- Device/emulator: **arm64-v8a**, OpenGL ES 3.0
- `minSdk` 26 / `targetSdk` 34
- Same Wi‑Fi / LAN for multiplayer (UDP port **27750**)

## Build APK

CLI needs **JDK 17** (Studio’s JBR 25 is too new for Gradle 8.11). A local copy used by this machine lives under `android/.tools/jdk-17.0.2` after setup.

```bat
cd android
set JAVA_HOME=%CD%\.tools\jdk-17.0.2
gradlew.bat :app:assembleDebug
REM APK: app\build\outputs\apk\debug\app-debug.apk
```

Or open `android/` in Android Studio and Build → Make Project (Studio supplies its own JDK).

First launch copies `maps/`, `models/`, `shaders_es/`, … from the APK into `filesDir` (`g_basepath`).

## Build `.so` only

```bat
REM Engine-linked + net (same as Gradle default now)
android\build_so.bat engine

REM Slim stub only
android\build_so.bat
```

Output: `android/build-ndk/arm64-v8a/libpermafrost.so`.

## LAN multiplayer (verify checklist)

1. Install the same debug APK on **two arm64 devices** (or one device + desktop host using the same lobby protocol).
2. Both on the **same LAN**; allow UDP **27750**.
3. Device A: open Lobby → set name → **Host**.
4. Device B: enter A’s LAN IP + port **27750** → **Join** (retry if first attempt fails while HELLO is in flight).
5. Confirm both see two players in the roster text.
6. On host: **Start Match** → both should enter lockstep (`Lockstep playing` status; lobby panel hides).
7. Select units / tap to move; both sims should stay in sync.

Native entry points: `jni/android_mp.c` (`pf_android_mp_*`). Kotlin: `PfNative.mpHost` / `mpJoin` / `mpStartMatch` (always via `GLSurfaceView.queueEvent`).

### Smoke link check (CLI)

After a successful native build, `nm`/`llvm-nm` on `libpermafrost.so` should show real `Net_HostCreate` / `Lobby_Host` (not only stub object). Gradle assemble with `PF_ANDROID_ENGINE_BATCH=2` is the authoritative check.

## Touch

| Gesture | Engine mapping |
|---------|----------------|
| Drag | Middle-button RTS drag-pan |
| Tap | Left-click (select / issue move) |

## Native API

See `jni/pf_android.h`, `jni/android_mp.h`. Kotlin: `PfNative.kt`.

## Known gaps

1. Some GLES paths still no-op desktop-only GL (batch MDI, anim TBO, compute movement)
2. OpenAL Soft not linked
3. Nuklear UI may be limited without full font assets
4. If boot fails (missing map), Activity falls back to the stub triangle
5. **Phase 2 (not in this build):** `online_client.cpp` + room server TCP matchmaking, NAT traversal / UDP relay wired to `LS_*`

## Phase 2 (online rooms / NAT) — deferred

Separate iteration. Planned direction:

- Link or port `src/net/online_client.cpp` + protobuf TCP for login/room list.
- On match found, inject host IP/port/role into the same `Lobby_*` + `LS_*` path used for LAN.
- NAT: hole-punch or room-server UDP relay (relay protocol today is **not** connected to lockstep).
