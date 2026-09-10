@echo off
setlocal
cd /d "%~dp0"

if not exist "lib\pf.exe" (
    echo lib\pf.exe not found. Build the engine first.
    pause
    exit /b 1
)

set "PATH=%~dp0lib;C:\msys64\mingw64\bin;%PATH%"
lib\pf.exe ./ ./scripts/rts/main.py
if errorlevel 1 pause
