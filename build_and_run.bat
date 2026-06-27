@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM -----------------------------------------------------------------------------
REM MetaAgent desktop app — configure (only when needed), build, and run.
REM
REM Usage: build_and_run.bat [Debug^|Release] [--configure] [--clean] [--no-run]
REM   Debug^|Release  build configuration (default: Debug)
REM   --configure     force the CMake configure step
REM   --clean         delete the CMake cache, then re-configure
REM   --no-run        build only, do not launch the app
REM
REM The CMake configure step runs automatically only when build-msvc has no cache
REM yet (or with --configure/--clean). On later runs it is skipped — the Visual
REM Studio generator re-runs CMake itself if CMakeLists.txt changed.
REM Copyright (c) vecnode 2026
REM -----------------------------------------------------------------------------
pushd "%~dp0"

set "BUILD_DIR=build-msvc"
set "CONFIG=Debug"
set "FORCE_CONFIGURE=0"
set "CLEAN_BUILD=0"
set "DO_RUN=1"

:parse
if "%~1"=="" goto after_parse
if /I "%~1"=="Debug"       set "CONFIG=Debug"
if /I "%~1"=="Release"     set "CONFIG=Release"
if /I "%~1"=="--configure" set "FORCE_CONFIGURE=1"
if /I "%~1"=="-c"          set "FORCE_CONFIGURE=1"
if /I "%~1"=="--clean"     set "CLEAN_BUILD=1"
if /I "%~1"=="--no-run"    set "DO_RUN=0"
shift
goto parse
:after_parse

REM --- Locate the MSVC toolchain so this works outside a Developer Prompt ---
set "VCVARS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "delims=" %%I in ('"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find VC\Auxiliary\Build\vcvars64.bat') do (
    if not defined VCVARS set "VCVARS=%%I"
  )
)
if defined VCVARS (
  set "INCLUDE="
  set "LIB="
  set "LIBPATH="
  call "%VCVARS%" >nul 2>&1
)

REM --- Locate cmake ---
set "CMAKE_EXE="
if exist "%ProgramFiles%\CMake\bin\cmake.exe" set "CMAKE_EXE=%ProgramFiles%\CMake\bin\cmake.exe"
if not defined CMAKE_EXE for /f "delims=" %%I in ('where cmake 2^>nul') do if not defined CMAKE_EXE set "CMAKE_EXE=%%I"
if not defined CMAKE_EXE (
  echo [error] cmake.exe not found. Install CMake or open a Developer Command Prompt.
  goto error
)

REM --- Clean cache if requested ---
if "%CLEAN_BUILD%"=="1" (
  if exist "%BUILD_DIR%\CMakeCache.txt" del /f /q "%BUILD_DIR%\CMakeCache.txt"
  set "FORCE_CONFIGURE=1"
)

REM --- Configure only when there is no cache yet, or when forced ---
if not exist "%BUILD_DIR%\CMakeCache.txt" set "FORCE_CONFIGURE=1"
if "%FORCE_CONFIGURE%"=="1" (
  echo [1/3] Configuring %BUILD_DIR% ^(Visual Studio 2022 x64, METAAGENT_BUILD_APP=ON^)
  "%CMAKE_EXE%" -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -DMETAAGENT_BUILD_APP=ON
  if errorlevel 1 goto error
) else (
  echo [1/3] Reusing existing %BUILD_DIR% cache ^(pass --configure to force^).
)

echo [2/3] Building metaagent-app ^(%CONFIG%^)
"%CMAKE_EXE%" --build "%BUILD_DIR%" --target metaagent-app -j --config %CONFIG%
if errorlevel 1 goto error

set "APP_EXE=%BUILD_DIR%\app\%CONFIG%\metaagent-app.exe"
if not exist "%APP_EXE%" (
  echo [error] Built executable not found: %APP_EXE%
  goto error
)

if "%DO_RUN%"=="0" (
  echo [3/3] Skipping run ^(--no-run^). Built: %APP_EXE%
  goto end
)

echo [3/3] Running %APP_EXE%
"%APP_EXE%"
goto end

:error
echo Build or run failed.
popd
endlocal
exit /b 1

:end
popd
endlocal
