@echo off
REM Build libpermafrost.so with NDK cmake (no Android Studio required).
REM Set ANDROID_NDK to NDK r26+ root first.
if "%ANDROID_NDK%"=="" (
  echo Set ANDROID_NDK to your NDK path
  exit /b 1
)
set ROOT=%~dp0..
set ABI=arm64-v8a
set API=26
set BUILD=%ROOT%\android\build-ndk\%ABI%
cmake -S "%ROOT%\android" -B "%BUILD%" ^
  -DCMAKE_TOOLCHAIN_FILE="%ANDROID_NDK%\build\cmake\android.toolchain.cmake" ^
  -DANDROID_ABI=%ABI% ^
  -DANDROID_PLATFORM=android-%API% ^
  -DANDROID_STL=c++_shared ^
  -DCMAKE_BUILD_TYPE=Release
cmake --build "%BUILD%" -j
echo Built: %BUILD%\libpermafrost.so
