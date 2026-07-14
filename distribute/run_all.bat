@echo off
rem -----------------------------------------------------------------------------
rem droidcli distribution launcher.
rem Starts droidcli.exe headless, pointed at the connectors.json next to this
rem script (edit it to reference the sibling media-player\/adapter\ folders, or
rem any other http_peer/launched_process connector). droidcli has no window -
rem it is an HTTP agent daemon; use the routes below or curl to drive it.
rem Copyright (c) vecnode 2026
rem -----------------------------------------------------------------------------
setlocal EnableExtensions
set "ROOT=%~dp0"

if not exist "%ROOT%droidcli\droidcli.exe" (
	echo [error] droidcli\droidcli.exe not found next to this script.
	pause
	exit /b 1
)

if not exist "%ROOT%connectors.json" (
	echo [warn] connectors.json not found next to this script - starting with no connectors.
	echo        Copy config\connectors.example.json from the repo and edit it, or register
	echo        connectors at runtime via POST /api/connectors.
)

start "droidcli" /D "%ROOT%droidcli" "%ROOT%droidcli\droidcli.exe" --port 30080 --config "%ROOT%connectors.json"
endlocal
