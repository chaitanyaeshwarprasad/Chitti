@echo off
title Chitti - Wi-Fi setup
echo.
echo   Gives Chitti your Wi-Fi name and password through the USB cable.
echo   1. Plug Chitti into this PC with the USB-C cable.
echo   2. Type the Wi-Fi name and password below. It must be 2.4 GHz Wi-Fi,
echo      the SAME network this PC is on.
echo   The password only goes to the board through the cable.
echo.
if not exist "%~dp0..\server\.venv\Scripts\python.exe" (
  echo   Run "Start Chitti.bat" once first.
  pause & exit /b 1
)
"%~dp0..\server\.venv\Scripts\python.exe" "%~dp0..\server\set_wifi.py"
echo.
pause
