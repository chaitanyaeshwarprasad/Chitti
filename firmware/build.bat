@echo off
rem Build (and optionally flash) Chitti's firmware.  Usage: build.bat [flash] [anim] [monitor]
rem Needs ESP-IDF v5.5 (see README). It is found in this order:
rem   1. the IDF_PATH you already set   2. firmware\idf_path.txt (one line: the esp-idf folder)
rem   3. %USERPROFILE%\esp\esp-idf      4. C:\Espressif\frameworks\esp-idf-v5.5.6
setlocal
cd /d "%~dp0"
if not defined IDF_PATH if exist idf_path.txt set /p IDF_PATH=<idf_path.txt
if not defined IDF_PATH if exist "%USERPROFILE%\esp\esp-idf\export.bat" set "IDF_PATH=%USERPROFILE%\esp\esp-idf"
if not defined IDF_PATH if exist "C:\Espressif\frameworks\esp-idf-v5.5.6\export.bat" set "IDF_PATH=C:\Espressif\frameworks\esp-idf-v5.5.6"
if not exist "%IDF_PATH%\export.bat" (
  echo   ESP-IDF not found. Install ESP-IDF v5.5 and put its folder path in firmware\idf_path.txt
  exit /b 1
)
rem tools live next to esp-idf (e.g. ...\esp\tools) unless already set
if not defined IDF_TOOLS_PATH if exist "%IDF_PATH%\..\tools" set "IDF_TOOLS_PATH=%IDF_PATH%\..\tools"
rem use the Python environment ESP-IDF already installed (newer Pythons on the PC would confuse it)
if not defined IDF_PYTHON_ENV_PATH for /d %%d in ("%IDF_TOOLS_PATH%\python_env\idf5.5_py*_env") do set "IDF_PYTHON_ENV_PATH=%%~fd"
if defined IDF_PYTHON_ENV_PATH set "PATH=%IDF_PYTHON_ENV_PATH%\Scripts;%PATH%"
call "%IDF_PATH%\export.bat" >nul || exit /b 1
where idf.py >nul 2>&1 || (echo   ESP-IDF did not load. Re-run its installer. & exit /b 1)
if not exist sdkconfig idf.py set-target esp32s3 || exit /b 1

rem The board's USB port is found automatically; set CHITTI_PORT=COM5 (for example) to force one.
set PORTARG=
if defined CHITTI_PORT set "PORTARG=-p %CHITTI_PORT%"
if "%1"=="anim" (
  python -m esptool --chip esp32s3 %PORTARG% -b 921600 write_flash 0x610000 prebuilt\anim.bin