@echo off
setlocal
cd /d "%~dp0"

if not exist "lib\room_server.exe" (
    echo lib\room_server.exe not found. Build with: make room_server PLAT=WINDOWS
    pause
    exit /b 1
)

set "PATH=%~dp0lib;C:\msys64\mingw64\bin;%PATH%"
echo Room server UDP/KCP :40000  registering with lobby 127.0.0.1:27000
lib\room_server.exe --port 40000 --name demo --max 2 --seed 1 --lobby 127.0.0.1:27000 --host 127.0.0.1
if errorlevel 1 pause
