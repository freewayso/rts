@echo off
setlocal
cd /d "%~dp0"
echo Starting lobby directory on http://127.0.0.1:27000
echo Browser can open that URL to see the room list.
echo.
py -3 tools\lobby_server.py 27000
if errorlevel 1 python tools\lobby_server.py 27000
if errorlevel 1 pause
