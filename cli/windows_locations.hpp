#pragma once

#include "droidcli_core.h"

namespace droidcli::cli {

// Part of droidcli-infra (ARCHITECTURE.md's Modules diagram) - executes
// OS-specific enumeration (COM: IKnownFolderManager, IShellLink) directly,
// not droidcli-tools.
//
// One real, launchable Windows location - a known folder ("Downloads",
// "Recycle Bin"), an Administrative Tools shortcut ("Event Viewer"), or one
// of the small hardcoded exceptions (an ms-settings: deep link, a deferred
// Control Panel applet, a stable admin one-liner) - see scan_windows_locations.
struct WindowsLocationEntry {
	core::String alias;
	core::String display_name;
	core::String path_or_name;
	core::String args;
};

// Scans the OS for real, discoverable Windows locations - known folders
// (IKnownFolderManager, queried against a small fixed allowlist of
// KNOWNFOLDERIDs - which folders are worth exposing is still curated, but
// each one's current real display name/shell path is asked of Windows, not
// hand-typed) and Administrative Tools Start Menu shortcuts (.lnk
// enumeration via IShellLink) - plus a small, explicitly-justified
// hardcoded exception list for the two categories with no discoverable API
// at all (ms-settings: deep links - Microsoft exposes no list of valid
// sub-pages) or not yet automated (5 deferred Control Panel applets, whose
// genuinely general replacement needs meaningfully fiddlier Shell
// PIDL/IEnumIDList enumeration; 4 stable admin one-liners - Task Manager,
// Device Manager, Disk Management, Control Panel itself - not confidently
// guaranteed to appear as standalone Admin Tools shortcuts on every Windows
// install). See "Windows execution ruleset" in ARCHITECTURE.md.
//
// Meant to be called once at startup (DroidHost::initialize(), mirroring
// scan_installed_applications()) and cached - this does real COM/registry
// work, not something to redo per lookup. Never throws or crashes on a COM
// failure partway through a scan - returns whatever was collected so far,
// same resilience discipline as every other startup scan in this codebase.
DROIDCLI_API core::Array<WindowsLocationEntry> scan_windows_locations();

} // namespace droidcli::cli
