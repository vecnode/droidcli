@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM -----------------------------------------------------------------------------
REM MetaAgent — build everything and stage a portable distribution folder + zip.
REM
REM Produces:  dist\metaagent-<version>\
REM              droidcli\       droidcli.exe (Release) + FFmpeg DLLs
REM              media-player\   media-player-cpp.exe + DLLs + data\
REM              adapter\        pre-training deploy code + the trained LoRA
REM                               adapter (~40 MB; no base/fused model weights)
REM              datasets\       corpus CSVs
REM              run_all.bat     launcher (see distribute\run_all.bat)
REM
REM media-player\ and adapter\ are staged unconditionally, same as before this
REM change - droidcli's connectors config (config/connectors.example.json) can
REM point at them, but the dist layout for the peer apps themselves is
REM unrelated to the connector/task-queue refactor.
REM
REM Usage: build_and_distribute.bat [--skip-media] [--no-zip]
REM
REM Configuration (env vars, all optional):
REM   DIST_MEDIA_DIR    media-player-cpp project dir to build/stage.
REM                     Default: external\media-player-cpp (submodule). Point it
REM                     at your openFrameworks working copy to include bin\data
REM                     media (media files are git-ignored in the submodule).
REM   DIST_OF_ROOT      openFrameworks root, required when DIST_MEDIA_DIR is not
REM                     inside an OF tree (passed to make as OF_ROOT).
REM   DIST_ADAPTER_DIR  pre-training repo root. Default: external\pre-training.
REM   DIST_ADAPTER_WEIGHTS_DIR  Trained LoRA adapter dir to stage (small, ~40 MB;
REM                     this is YOUR trained model, not downloaded from anywhere).
REM                     Default: %DIST_ADAPTER_DIR%\training\runs\llava15_lora\final_adapter.
REM   DIST_DATASET_DIR  folder with the corpus CSVs. Default: %DIST_ADAPTER_DIR%\output.
REM   MSYS2_ROOT        MSYS2 install. Default: C:\msys64.
REM
REM The ~14 GB base model (llava-hf/llava-1.5-7b-hf) is intentionally NOT copied
REM (light dist): deploy/infer.py downloads it once from Hugging Face Hub into
REM training/hf_cache/ next to the adapter (git-ignored, resolved relative to
REM deploy/ so this works in the dist layout too) the first time the adapter
REM server starts. Ship a fused model instead by copying it to
REM adapter\deploy\merged_model yourself (skips the LoRA + base-model download).
REM Copyright (c) vecnode 2026
REM -----------------------------------------------------------------------------
pushd "%~dp0"
set "ROOT=%CD%"

set "SKIP_MEDIA=0"
set "DO_ZIP=1"
:parse
if "%~1"=="" goto after_parse
if /I "%~1"=="--skip-media" set "SKIP_MEDIA=1"
if /I "%~1"=="--no-zip"     set "DO_ZIP=0"
shift
goto parse
:after_parse

if not defined MSYS2_ROOT set "MSYS2_ROOT=C:\msys64"
if not defined DIST_MEDIA_DIR set "DIST_MEDIA_DIR=%ROOT%\external\media-player-cpp"
if not defined DIST_ADAPTER_DIR set "DIST_ADAPTER_DIR=%ROOT%\external\pre-training"
if not defined DIST_ADAPTER_WEIGHTS_DIR set "DIST_ADAPTER_WEIGHTS_DIR=%DIST_ADAPTER_DIR%\training\runs\llava15_lora\final_adapter"
if not defined DIST_DATASET_DIR set "DIST_DATASET_DIR=%DIST_ADAPTER_DIR%\output"

REM --- Version from src\version.hpp ---
set "VER_MAJOR=0" & set "VER_MINOR=0" & set "VER_PATCH=0"
for /f "tokens=3" %%v in ('findstr /C:"#define METAAGENT_VERSION_MAJOR" src\version.hpp') do set "VER_MAJOR=%%v"
for /f "tokens=3" %%v in ('findstr /C:"#define METAAGENT_VERSION_MINOR" src\version.hpp') do set "VER_MINOR=%%v"
for /f "tokens=3" %%v in ('findstr /C:"#define METAAGENT_VERSION_PATCH" src\version.hpp') do set "VER_PATCH=%%v"
set "DIST_NAME=metaagent-%VER_MAJOR%.%VER_MINOR%.%VER_PATCH%"
set "DIST_DIR=%ROOT%\dist\%DIST_NAME%"

echo === MetaAgent distribute: %DIST_NAME% ===
echo   media source:   %DIST_MEDIA_DIR%
echo   adapter source: %DIST_ADAPTER_DIR%
echo   adapter weights: %DIST_ADAPTER_WEIGHTS_DIR%
echo   dataset source: %DIST_DATASET_DIR%
echo.

REM --- [1/6] Build droidcli (Release, reuse build_and_run.bat) ---
echo [1/6] Building droidcli (Release)
call "%ROOT%\build_and_run.bat" Release --no-run
if errorlevel 1 goto error

REM --- [2/6] Build media player (MSYS2 MinGW64) ---
if "%SKIP_MEDIA%"=="1" (
  echo [2/6] Skipping media player build ^(--skip-media^).
) else (
  echo [2/6] Building media-player-cpp ^(make Release^)
  if not exist "%MSYS2_ROOT%\msys2_shell.cmd" (
    echo [error] MSYS2 not found at %MSYS2_ROOT% ^(set MSYS2_ROOT^).
    goto error
  )
  set "OF_ARG="
  if defined DIST_OF_ROOT (
    set "OF_UNIX=%DIST_OF_ROOT:\=/%"
    set "OF_ARG=OF_ROOT='!OF_UNIX!'"
  )
  pushd "%DIST_MEDIA_DIR%"
  call "%MSYS2_ROOT%\msys2_shell.cmd" -defterm -no-start -mingw64 -here -c "make -j Release !OF_ARG!"
  popd
)
if not exist "%DIST_MEDIA_DIR%\bin\media-player-cpp.exe" (
  echo [error] %DIST_MEDIA_DIR%\bin\media-player-cpp.exe not found after build.
  echo         Point DIST_MEDIA_DIR at a buildable working copy, or build it once manually.
  goto error
)

REM --- [3/6] Stage dist folder ---
echo [3/6] Staging %DIST_DIR%
if exist "%DIST_DIR%" rmdir /s /q "%DIST_DIR%"
mkdir "%DIST_DIR%\droidcli" "%DIST_DIR%\media-player" "%DIST_DIR%\adapter" "%DIST_DIR%\datasets"

robocopy "%ROOT%\build-msvc\Release" "%DIST_DIR%\droidcli" droidcli.exe *.dll /XF *.pdb *.ilk *.lib *.exp /NFL /NDL /NJH /NJS >nul
if errorlevel 8 goto error

robocopy "%DIST_MEDIA_DIR%\bin" "%DIST_DIR%\media-player" /E /XF *.pdb *_debug.exe /NFL /NDL /NJH /NJS >nul
if errorlevel 8 goto error

REM --- [4/6] Adapter code + trained LoRA weights (no base/fused model) ---
echo [4/6] Staging adapter code
robocopy "%DIST_ADAPTER_DIR%\deploy" "%DIST_DIR%\adapter\deploy" /E /XD merged_model __pycache__ /XF *.safetensors /NFL /NDL /NJH /NJS >nul
if errorlevel 8 goto error
for %%F in (pyproject.toml uv.lock uv_bootstrap.bat main.bat LICENSE) do (
  if exist "%DIST_ADAPTER_DIR%\%%F" copy /y "%DIST_ADAPTER_DIR%\%%F" "%DIST_DIR%\adapter\" >nul
)

if exist "%DIST_ADAPTER_WEIGHTS_DIR%" (
  echo         Staging trained LoRA adapter from %DIST_ADAPTER_WEIGHTS_DIR%
  robocopy "%DIST_ADAPTER_WEIGHTS_DIR%" "%DIST_DIR%\adapter\training\runs\llava15_lora\final_adapter" /E /NFL /NDL /NJH /NJS >nul
  if errorlevel 8 goto error
) else (
  echo [warn] Trained adapter not found at %DIST_ADAPTER_WEIGHTS_DIR%.
  echo        The adapter server will fail to start until this exists or
  echo        adapter\deploy\merged_model is populated - see README.txt.
)

REM --- [5/6] Datasets + launcher + readme ---
echo [5/6] Staging datasets and launcher
copy /y "%DIST_DATASET_DIR%\*.csv" "%DIST_DIR%\datasets\" >nul
if errorlevel 1 (
  echo [warn] No CSVs found in %DIST_DATASET_DIR% — datasets\ will be empty.
)
copy /y "%ROOT%\distribute\run_all.bat" "%DIST_DIR%\run_all.bat" >nul
copy /y "%ROOT%\distribute\README_DIST.txt" "%DIST_DIR%\README.txt" >nul

REM --- [6/6] Zip ---
if "%DO_ZIP%"=="1" (
  echo [6/6] Zipping dist\%DIST_NAME%.zip
  powershell -NoProfile -Command "Compress-Archive -Path '%DIST_DIR%' -DestinationPath '%ROOT%\dist\%DIST_NAME%.zip' -Force"
  if errorlevel 1 goto error
) else (
  echo [6/6] Skipping zip ^(--no-zip^).
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
