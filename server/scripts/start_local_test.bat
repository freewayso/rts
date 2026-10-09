@echo off
REM Double-click friendly launcher for local online test
cd /d "%~dp0\.."
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0start_local_test.ps1" %*
if errorlevel 1 pause
