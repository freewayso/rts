@echo off
REM Build libpermafrost.so with NDK cmake (no Android Studio required).
REM Set ANDROID_NDK to NDK r26+ root, or defaults to %%LOCALAPPDATA%%\Android\Sdk\ndk\26.1.10909125
if "%ANDROID_NDK%"=="" (
  if exist "%LOCALAPPDATA%\Android\Sdk\ndk\26.3.11579264\build\cmake\android.toolchain.cmake" (
    set "ANDROID_NDK=%LOCALAPPDATA%\Android\Sdk\ndk\26.3.11579264"
  ) else (
    set "ANDROID_NDK=%LOCALAPPDATA%\Android\Sdk\ndk\26.1.10909125"
  )
)
if not exist "%ANDROID_NDK%\build\cmake\android.toolchain.cmake" (
  echo ANDROID_NDK not found: %ANDROID_NDK%
  echo Install NDK r26+ or set ANDROID_NDK.
  exit /b 1
)
set ROOT=%~dp0..
set ABI=arm64-v8a
set API=26
set BUILD=%ROOT%\android\build-ndk\%ABI%
set MAKE=%ANDROID_NDK%\prebuilt\windows-x86_64\bin\make.exe
set LINK_ENGINE=OFF
set ENGINE_BATCH=1
if /I "%1"=="engine" (
  set LINK_ENGINE=ON
  set ENGINE_BATCH=2
)

echo NDK=%ANDROID_NDK%
echo LINK_ENGINE=%LINK_ENGINE% BATCH=%ENGINE_BATCH%

cmake -S "%ROOT%\android" -B "%BUILD%" -G "Unix Makefiles" ^
  -DCMAKE_TOOLCHAIN_FILE="%ANDROID_NDK%\build\cmake\android.toolchain.cmake" ^
  -DCMAKE_MAKE_PROGRAM="%MAKE%" ^
  -DANDROID_ABI=%ABI% ^
  -DANDROID_PLATFORM=android-%API% ^
  -DANDROID_STL=c++_shared ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DPF_ANDROID_LINK_ENGINE=%LINK_ENGINE% ^
  -DPF_ANDROID_ENGINE_BATCH=%ENGINE_BATCH%
if errorlevel 1 exit /b 1
cmake --build "%BUILD%" -j %NUMBER_OF_PROCESSORS%
if errorlevel 1 exit /b 1
echo Built: %BUILD%\libpermafrost.so
REM Confirm Lobby_Host / Net_HostCreate are linked when engine+batch2
if exist "%BUILD%\libpermafrost.so" if "%LINK_ENGINE%"=="ON" (
  "%ANDROID_NDK%\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-nm.exe" "%BUILD%\libpermafrost.so" 2>nul | findstr /C:"Lobby_Host" /C:"Net_HostCreate"
)
