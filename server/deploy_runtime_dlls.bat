@echo off
REM Copy MinGW/protobuf runtime DLLs next to exes.
set ROOT=%~dp0
set MINGW_BIN=%MINGW_BIN%
if "%MINGW_BIN%"=="" set MINGW_BIN=C:\msys64\mingw64\bin
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%ROOT%deploy_runtime_dlls.ps1" -MingwBin "%MINGW_BIN%"
exit /b %ERRORLEVEL%
