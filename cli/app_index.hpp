#pragma once

#include "droidcli_core.h"

namespace droidcli::cli {

// A discovered installed application: a human-readable name and the best
// guess at its main executable's path.
struct InstalledApp {
	core::String name;
	core::String path;
};

// Part of droidcli-infra (ARCHITECTURE.md's Modules diagram) - one of the
// modules that executes OS-specific commands directly (registry reads),
// not droidcli-tools.
//
// Scans the machine for installed applications so open_application() can
// resolve a name the user knows (e.g. "Blender", "KiCad") even when the app
// never registered itself on PATH or in the App Paths registry key - most
// installers only ever register an Add/Remove Programs ("Uninstall")
// registry entry, which is what this scans: HKEY_LOCAL_MACHINE (both the
// native and WOW6432Node 32-bit views) and HKEY_CURRENT_USER, under
// SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall. For each entry, the
// executable path is taken from DisplayIcon (most reliable - installers
// almost always point this directly at the main exe) or, failing that, a
// shallow scan of InstallLocation for a plausibly-named .exe. Entries with
// no usable name, no resolvable path, or marked SystemComponent (updates,
// redistributables, driver packages) are skipped.
//
// Windows-only; returns an empty array on other platforms. Meant to be
// called once at startup and cached - this walks potentially hundreds of
// registry keys and touches disk, not something to redo per lookup.
core::Array<InstalledApp> scan_installed_applications();

} // namespace droidcli::cli
