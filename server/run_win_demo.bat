@echo off
REM Start login + room servers, then the Win32 client UI.
set ROOT=%~dp0
set BUILD=%ROOT%build
set MINGW_BIN=%MINGW_BIN%
if "%MINGW_BIN%"=="" set MINGW_BIN=C:\msys64\mingw64\bin

if not exist "%BUILD%\login_server.exe" (
  echo Build first:
  echo   cmake -B build -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH=C:/msys64/mingw64
  echo   cmake --build build
  exit /b 1
)

call "%ROOT%deploy_runtime_dlls.bat"
if errorlevel 1 exit /b 1
set PATH=%BUILD%;%MINGW_BIN%;%PATH%

mkdir "%ROOT%data" 2>nul
mkdir "%ROOT%logs" 2>nul

echo Starting login_server / room_server / online_client_ui ...
start "pf-login" /D "%ROOT%" "%BUILD%\login_server.exe" 27000 "%ROOT%data\account.db"
timeout /t 1 /nobreak >nul
start "pf-room" /D "%ROOT%" "%BUILD%\room_server.exe" 27001 27002
timeout /t 1 /nobreak >nul
start "pf-client" /D "%ROOT%" "%BUILD%\online_client_ui.exe"
echo.
echo Started. Logs: %ROOT%logs\
echo UI: register/login - connect room server - create/join room - start match
echo Engine: match_start launches ..\lib\pf.exe  (build with: bash build_win.sh)
if not exist "%ROOT%..\lib\pf.exe" (
  echo WARNING: %ROOT%..\lib\pf.exe missing — lobby will show preview only until engine is built.
)