#!/usr/bin/env bash
# Optional: build only libpermafrost.so with NDK cmake (no Android Studio).
# Usage: ANDROID_NDK=/path/to/ndk ./android/build_so.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NDK="${ANDROID_NDK:-${NDK_ROOT:-}}"
if [[ -z "$NDK" || ! -d "$NDK" ]]; then
  echo "Set ANDROID_NDK to your NDK r26+ path"
  exit 1
fi
ABI=arm64-v8a
API=26
BUILD="$ROOT/android/build-ndk/$ABI"
cmake -S "$ROOT/android" -B "$BUILD" \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI="$ABI" \
  -DANDROID_PLATFORM="android-$API" \
  -DANDROID_STL=c++_shared \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" -j
echo "Built: $BUILD/libpermafrost.so"
