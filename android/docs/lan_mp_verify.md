# LAN multiplayer verify (Android)

## Automated / local smoke (done in CI-ish CLI)

1. `android\build_so.bat engine` → `libpermafrost.so` exports `Lobby_Host` and `Net_HostCreate` (`llvm-nm`).
2. `gradlew.bat :app:assembleDebug` with `PF_ANDROID_ENGINE_BATCH=2` succeeds.
3. APK contains `lib/arm64-v8a/libpermafrost.so`.

## Two-device checklist (manual)

See [android/README.md](../README.md) § LAN multiplayer.

| Step | Host | Client |
|------|------|--------|
| 1 | Install debug APK | Install same APK |
| 2 | Same Wi‑Fi | Same Wi‑Fi |
| 3 | Lobby → Host (port 27750) | Enter host LAN IP → Join |
| 4 | See 2 players | See 2 players |
| 5 | Start Match | Auto lockstep |
| 6 | Issue move orders | Orders apply on both |

Default map: `maps/demo.pfmap` (set by host in `pf_android_mp_host`).
