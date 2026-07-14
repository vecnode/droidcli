@echo off
rem -----------------------------------------------------------------------------
rem droidcli distribution launcher.
rem Starts droidcli.exe headless, pointed at the connectors.json next to this
rem script (copy config\connectors.example.json to connectors.json and edit it
rem to point at whatever http_peer/launched_process connectors you want).
rem droidcli has no window - it is an HTTP agent daemon with a default TUI;
rem use the routes below or curl to drive it, or --headless for scripted use.
rem Copyright (c) vecnode 2026
rem -----------------------------------------------------------------------------
setlocal EnableExtensions
set "ROOT=%~dp0"

if not exist "%ROOT%droidcli.exe" (
	echo [error] droidcli.exe not found next to this script.
	pause
	exit /b 1
)

if not exist "%ROOT%connectors.json" (
	echo [warn] connectors.json not found next to this script - starting with no connectors.
	echo        Copy config\connectors.example.json to connectors.json and edit it, or register
	echo        connectors at runtime via POST /api/connectors.
)

"%ROOT%droidcli.exe" --port 30080 --config "%ROOT%connectors.json"
endlocal
