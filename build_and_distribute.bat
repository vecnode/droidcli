@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM -----------------------------------------------------------------------------
REM Droidcli — build and stage a portable distribution folder + zip.
REM
REM Produces:  dist\droidcli-<version>\
REM              droidcli.exe + FFmpeg DLLs
REM              config\connectors.example.json
REM              run_all.bat     launcher (see distribute\run_all.bat)
REM
REM droidcli has no compiled-in knowledge of any peer app - point
REM config\connectors.json (copied from config\connectors.example.json) at
REM whatever http_peer or launched_process connectors you actually want.
REM
REM Usage: build_and_distribute.bat [--no-zip]
REM Copyright (c) vecnode 2026
REM -----------------------------------------------------------------------------
pushd "%~dp0"
set "ROOT=%CD%"

set "DO_ZIP=1"
:parse
if "%~1"=="" goto after_parse
if /I "%~1"=="--no-zip" set "DO_ZIP=0"
shift
goto parse
:after_parse

REM --- Version from src\version.hpp ---
set "VER_MAJOR=0" & set "VER_MINOR=0" & set "VER_PATCH=0"
for /f "tokens=3" %%v in ('findstr /C:"#define DROIDCLI_VERSION_MAJOR" src\version.hpp') do set "VER_MAJOR=%%v"
for /f "tokens=3" %%v in ('findstr /C:"#define DROIDCLI_VERSION_MINOR" src\version.hpp') do set "VER_MINOR=%%v"
for /f "tokens=3" %%v in ('findstr /C:"#define DROIDCLI_VERSION_PATCH" src\version.hpp') do set "VER_PATCH=%%v"
set "DIST_NAME=droidcli-%VER_MAJOR%.%VER_MINOR%.%VER_PATCH%"
set "DIST_DIR=%ROOT%\dist\%DIST_NAME%"

echo === Droidcli distribute: %DIST_NAME% ===
echo.

REM --- [1/3] Build droidcli (Release, reuse build_and_run.bat) ---
echo [1/3] Building droidcli (Release)
call "%ROOT%\build_and_run.bat" Release --no-run
if errorlevel 1 goto error

REM --- [2/3] Stage dist folder ---
echo [2/3] Staging %DIST_DIR%
if exist "%DIST_DIR%" rmdir /s /q "%DIST_DIR%"
mkdir "%DIST_DIR%" "%DIST_DIR%\config"

robocopy "%ROOT%\build-msvc\Release" "%DIST_DIR%" droidcli.exe *.dll /XF *.pdb *.ilk *.lib *.exp /NFL /NDL /NJH /NJS >nul
if errorlevel 8 goto error

copy /y "%ROOT%\config\connectors.example.json" "%DIST_DIR%\config\" >nul
copy /y "%ROOT%\distribute\run_all.bat" "%DIST_DIR%\run_all.bat" >nul
copy /y "%ROOT%\distribute\README_DIST.txt" "%DIST_DIR%\README.txt" >nul

REM --- [3/3] Zip ---
if "%DO_ZIP%"=="1" (
  echo [3/3] Zipping dist\%DIST_NAME%.zip
  powershell -NoProfile -Command "Compress-Archive -Path '%DIST_DIR%' -DestinationPath '%ROOT%\dist\%DIST_NAME%.zip' -Force"
  if errorlevel 1 goto error
) else (
  echo [3/3] Skipping zip ^(--no-zip^).
)

echo.
echo Done: %DIST_DIR%
if "%DO_ZIP%"=="1" echo Done: %ROOT%\dist\%DIST_NAME%.zip
popd
endlocal
exit /b 0

:error
echo Distribution build failed.
popd
endlocal
exit /b 1
