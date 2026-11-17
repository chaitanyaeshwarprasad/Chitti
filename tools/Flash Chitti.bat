@echo off
title Chitti - flash firmware
setlocal
cd /d "%~dp0.."
set PY=server\.venv\Scripts\python.exe
set FW=firmware\prebuilt
if not exist "%PY%" (
  echo   Run "Start Chitti.bat" once first.
  pause & exit /b 1
)
for %%f in (bootloader.bin partition-table.bin chitti.bin anim.bin) do if not exist "%FW%\%%f" (
  echo   Missing %FW%\%%f - download the full project again.
  pause & exit /b 1
)
echo.
echo   Writes Chitti's ready-made firmware to the Waveshare ESP32-S3-Touch-AMOLED-1.75C.
echo   1. Plug the board into this PC with a USB-C DATA cable.
echo   2. Close anything else using the board, like Arduino IDE or a serial monitor.
echo.
echo   USB ports found:
"%PY%" -c "from serial.tools import list_ports; ps=list(list_ports.comports()); [print('    ', p.device, '-', p.description, '  <- looks like the board' if p.vid==0x303A else '') for p in ps] or print('     none - check the cable')"
echo.
set PORT=
set /p PORT=  Type the port, e.g. COM3, or press Enter to find it automatically:
set PORTARG=
if not "%PORT%"=="" set "PORTARG=--port %PORT%"

echo.
set ANS=
set /p ANS=  Optional: save a backup of the board's original software first? Takes about 6 minutes. [y/N]:
if /i not "%ANS%"=="y" goto erase
if not exist backup mkdir backup
echo   Saving backup\factory_backup.bin ...
"%PY%" -m esptool --chip esp32s3 %PORTARG% -b 921600 read_flash 0 0x2000000 backup\factory_backup.bin || goto failed
echo   Backup saved. Keep it if you ever want the original demo back.

:erase
echo.
set ANS=
set /p ANS=  First time flashing Chitti on this board? Erase it fully first - recommended the first time. [Y/n]:
if /i "%ANS%"=="n" goto write
echo   Erasing the board...
"%PY%" -m esptool --chip esp32s3 %PORTARG% erase_flash || goto failed

:write
echo.
echo   Writing Chitti - about 1 to 2 minutes...
"%PY%" -m esptool --chip esp32s3 %PORTARG% -b 921600 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_freq 80m --flash_size 32MB 0x0 %FW%\bootloader.bin 0x8000 %FW%\partition-table.bin 0x10000 %FW%\chitti.bin 0x610000 %FW%\anim.bin || goto failed
echo.
echo   Done! Chitti restarts by herself.
echo   Next: toolsConnect Chitti to Wi-Fi.bat, or the dashboard
echo.
pause
exit /b 0

:failed
echo.
echo   Flashing failed. Try this:
echo     - Use a different USB-C cable - some cables only charge.
echo     - Close Arduino IDE, serial monitors and other programs using the port.
echo     - Hold the BOOT button, tap RESET, let go of BOOT, then run this file again.
echo.
pause
exit /b 1
