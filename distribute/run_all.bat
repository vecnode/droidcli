@echo off
rem -----------------------------------------------------------------------------
rem MetaAgent distribution launcher.
rem Points metaagent at the sibling folders of this dist and starts it. The app
rem also auto-discovers this layout by itself; the env vars below just make the
rem wiring explicit and let you edit paths in one place.
rem Copyright (c) vecnode 2026
rem -----------------------------------------------------------------------------
setlocal EnableExtensions
set "ROOT=%~dp0"

set "METAAGENT_MEDIA_PLAYER_DIR=%ROOT%media-player"
set "METAAGENT_MEDIA_RUN_CMD=media-player-cpp.exe"
set "METAAGENT_ADAPTER_DIR=%ROOT%adapter\deploy"
set "METAAGENT_ADAPTER_LAUNCH_CMD=deploy.bat"
set "METAAGENT_DATASET_DIR=%ROOT%datasets"
set "METAAGENT_MEDIA_DATA_DIR=%ROOT%media-player\data"

if not exist "%ROOT%metaagent\metaagent-app.exe" (
	echo [error] metaagent\metaagent-app.exe not found next to this script.
	pause
	exit /b 1
)

start "metaagent" /D "%ROOT%metaagent" "%ROOT%metaagent\metaagent-app.exe"
endlocal
