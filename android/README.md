# Permafrost Engine — Android (libpermafrost.so)

## Requirements

- Android Studio Hedgehog+ / AGP 8.2
- NDK r26+ and CMake 3.22.1 (SDK Manager)
- Device/emulator: **arm64-v8a**, OpenGL ES 3.0

## Build

```bash
cd android
./gradlew :app:assembleDebug
# APK: app/build/outputs/apk/debug/app-debug.apk
```

Windows:

```bat
cd android
gradlew.bat :app:assembleDebug
```

## What this host does

1. Packs `assets/` + `shaders_es/` into the APK.
2. On first launch, copies them into `filesDir` (writable `g_basepath`).
3. Loads `libpermafrost.so` and pumps `pf_android_frame` on a **single GLES3** thread.
4. Forwards touch to `pf_android_touch` (RTS slice: tap select / tap move / drag camera).
5. `onPause` / `onResume` → `pf_android_pause` / `pf_android_resume`.
6. Quality via `PfNative.setQuality(0|1|2)` (low disables water reflection/refraction).

## Native API

See `jni/pf_android.h`. Phase 1 uses clear-color + triangle; Phase 2+ hooks `android_engine.c` when `PF_ANDROID_FULL` is defined (default in CMake).

## Optional multi-ABI

In `app/build.gradle.kts`, extend `abiFilters` with `armeabi-v7a` (needs ARMv7 fiber later).

## SDL2

The current host uses `GLSurfaceView` + NDK GLES3 (lighter than embedding SDL2 Android project).
To switch to SDL2 Android: build SDL2 for NDK, link in `android/CMakeLists.txt`, and call `pf_android_main` / `SDL_main` instead of the Activity frame pump.
